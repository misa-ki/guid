/*
 * Create cross-platform GUI dialogs in a breeze for Linux, macOS and
 * Windows. Run `guid --help` for details.
 *
 * Copyright (C) 2021  Misaki F. <https://github.com/misa-ki/guid>
 * Copyright (C) 2014  Thomas Lübking <thomas.luebking@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Guid.h"

#include <QAction>
#include <QBoxLayout>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDate>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFontDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QStringBuilder>
#include <QStringList>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCodec>
#include <QTimer>
#include <QTimerEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#if QT_VERSION >= 0x050000
    // this is to hack access to the --title parameter in Qt5
    #include <QWindow>
#endif

#include <QtDebug>

#include <cfloat>

#ifdef Q_OS_UNIX
    #include <signal.h>
    #include <unistd.h>
#endif

/******************************************************************************
 * define
 ******************************************************************************/

/*******************
 * Qt
 *******************/

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    #define SKIP_EMPTY Qt::SkipEmptyParts
#else
    #define SKIP_EMPTY QString::SkipEmptyParts
#endif

/*******************
 * Output
 *******************/

#define QOUT \
    QTextStream qOut(stdout); \
    qOut.setCodec("UTF-8");

#define QOUT_ERR \
    QTextStream qOutErr(stderr); \
    qOutErr.setCodec("UTF-8");

/*******************
 * Dialogs
 *******************/

#define IF_IS(_TYPE_) \
    if (const _TYPE_ *t = qobject_cast<const _TYPE_*>(w))

#define NEXT_ARG \
    QString((++i < args.count()) ? args.at(i) : QString())

#define WARN_UNKNOWN_ARG(_KNOWN_) \
    if (args.at(i).startsWith("--") && args.at(i) != _KNOWN_) \
        qDebug().noquote() << m_prefixErr + "unspecific argument" << args.at(i);

#define NEW_DIALOG \
    QDialog *dlg = new QDialog; \
    QVBoxLayout *vl = new QVBoxLayout(dlg);

#define SHOW_DIALOG \
    m_dialog = dlg; \
    connect(dlg, SIGNAL(finished(int)), SLOT(dialogFinished(int))); \
    dlg->show(); \
    centerWidget(dlg);

#define FINISH_DIALOG(_BTNS_) \
    QDialogButtonBox *btns = new QDialogButtonBox(_BTNS_, Qt::Horizontal, dlg); \
    vl->addWidget(btns); \
    connect(btns, SIGNAL(accepted()), dlg, SLOT(accept())); \
    connect(btns, SIGNAL(rejected()), this, SLOT(afterCloseButtonClick()));

#define QTREEWIDGET_STYLE \
    "QHeaderView::section {border: 1px solid #E0E0E0; background: #F7F7F7; padding-left: 3px; font-weight: bold;}"

/*******************
 * Forms
 *******************/

#define SET_FORMS_COL1(LABEL_COL1, WIDGET_COL1) \
    labelCol1 = LABEL_COL1; \
    colsHBoxLayout->addWidget(WIDGET_COL1); \
    colsHBoxLayout->setAlignment(WIDGET_COL1, Qt::AlignTop);

#define SET_FORMS_COL2(LABEL_COL2, WIDGET_COL2) \
    setCol2Label(colsHBoxLayout, LABEL_COL2); \
    colsHBoxLayout->addWidget(WIDGET_COL2); \
    colsHBoxLayout->addStretch(); \
    colsHBoxLayout->setAlignment(WIDGET_COL2, Qt::AlignTop); \
    setFormsColumns(colsContainer, colsHBoxLayout, dlg->property("guid_separator").toString(), columnsAreSet); \
    if (!lastTabName.isEmpty()) \
        lastTabLayout->addRow(labelCol1, colsContainer); \
    else \
        fl->addRow(labelCol1, colsContainer);

#define SWITCH_FORMS_WIDGET(NEW_WIDGET) \
    if (lastWidget == "text-browser") \
        setTextInfo(lastTextBrowser, lastTextFilename, lastTextIsReadOnly, lastTextIsUrl, \
                    lastTextFormat, lastTextCurlPath, lastTextHeight); \
    else if (lastWidget == "text-info") \
        setTextInfo(lastTextInfo, lastTextFilename, lastTextIsReadOnly, lastTextIsUrl, \
                    lastTextFormat, lastTextCurlPath, lastTextHeight); \
    if (!lastVar.isEmpty()) { \
        if (lastWidget == "calendar") lastCalendar->setProperty("guid_var", lastVar); \
        else if (lastWidget == "checkbox") lastCheckbox->setProperty("guid_var", lastVar); \
        else if (lastWidget == "entry") lastEntry->setProperty("guid_var", lastVar); \
        else if (lastWidget == "password") lastPassword->setProperty("guid_var", lastVar); \
        else if (lastWidget == "spin-box") lastSpinBox->setProperty("guid_var", lastVar); \
        else if (lastWidget == "double-spin-box") lastDoubleSpinBox->setProperty("guid_var", lastVar); \
        else if (lastWidget == "scale") lastScale->setProperty("guid_var", lastVar); \
        else if (lastWidget == "combo") lastCombo->setProperty("guid_var", lastVar); \
        else if (lastWidget == "list") lastList->setProperty("guid_var", lastVar); \
        else if (lastWidget == "text-info") lastTextInfo->setProperty("guid_var", lastVar); \
    } \
    lastWidget = NEW_WIDGET;

#define SET_FORMS_MENU_ITEM_DATA \
    if (menuItemData.count() > 0) menuItemName = menuItemData.at(0); \
    if (menuItemData.count() > 1) menuItemExitCode = menuItemData.at(1).toInt(&ok); \
    if (menuItemData.count() > 2) menuItemCommand = menuItemData.at(2); \
    if (menuItemData.count() > 3) menuItemCommandPrintOutput = menuItemData.at(3) == "1" ? true : false; \
    if (menuItemData.count() > 4) menuItemIcon = menuItemData.at(4);

// End of "define"

/******************************************************************************
 * class InputGuard
 ******************************************************************************/

class InputGuard : public QObject
{
public:
    InputGuard() : QObject(), m_checkTimer(0), m_guardedWidget(NULL) {}
    static void watch(QWidget *w) {
        #if QT_VERSION >= 0x050000
            if (qApp->platformName() == "wayland")
                return;
        #endif
        if (!s_instance)
            s_instance = new InputGuard;
        w->installEventFilter(s_instance);
    }
protected:
    bool eventFilter(QObject *o, QEvent *e) {
        QWidget *w = static_cast<QWidget*>(o);
        switch (e->type()) {
            case QEvent::FocusIn:
            case QEvent::WindowActivate:
                if (hasActiveFocus(w))
                    guard(w);
                break;
            case QEvent::FocusOut:
            case QEvent::WindowDeactivate:
                if (w == m_guardedWidget && !hasActiveFocus(w))
                    unguard(w);
                break;
            default:
                break;
        }
        return false;
    }
    void timerEvent(QTimerEvent *te) {
        if (te->timerId() == m_checkTimer && m_guardedWidget)
            check(m_guardedWidget);
        else
            QObject::timerEvent(te);
    }
private:
    bool check(QWidget *w) {
        #if QT_VERSION >= 0x050000
            // try to re-grab
            if (!w->window()->windowHandle()->setKeyboardGrabEnabled(true))
                w->releaseKeyboard();
        #endif
        if (w == QWidget::keyboardGrabber()) {
            w->setPalette(QPalette());
            return true;
        }
        w->setPalette(QPalette(Qt::white, Qt::red, Qt::white, Qt::black, Qt::gray,
                               Qt::white, Qt::white, Qt::red, Qt::red));
        return false;
    }
    void guard(QWidget *w) {
        w->grabKeyboard();
        if (check(w))
            m_guardedWidget = w;
        if (!m_checkTimer)
            m_checkTimer = startTimer(500);
    }
    bool hasActiveFocus(QWidget *w) {
        return w == QApplication::focusWidget() && w->isActiveWindow();
    }
    void unguard(QWidget *w) {
        Q_ASSERT(m_guardedWidget == w);
        killTimer(m_checkTimer);
        m_checkTimer = 0;
        m_guardedWidget = NULL;
        w->releaseKeyboard();
    }
private:
    int m_checkTimer;
    QWidget *m_guardedWidget;
    static InputGuard *s_instance;
};

InputGuard *InputGuard::s_instance = NULL;

#ifdef WS_X11
    #include <QX11Info>
    #include <X11/Xlib.h>
#endif

// End of "class InputGuard"

/******************************************************************************
 * typedef
 ******************************************************************************/

typedef QPair<bool, QString> ValuePair;
typedef QPair<QString, QString> Help;
typedef QList<Help> HelpList;
typedef QPair<QString, HelpList> CategoryHelp;
typedef QMap<QString, CategoryHelp> HelpDict;

// End of "typedef"

/******************************************************************************
 * static variables
 ******************************************************************************/

static QFile *gs_stdin = 0;

// End of "static variables"

/******************************************************************************
 * static functions
 ******************************************************************************/

static QStringList addColumnToListValues(QStringList values, QString addValue, int nbColumns)
{
    QStringList result;
    
    if (!addValue.isEmpty()) {
        int moduloComp = nbColumns - 1;
        if (moduloComp <= 0)
            moduloComp = 1;
        int i = 0;
        foreach(QString v, values) {
            if (i % moduloComp == 0) {
                result << addValue;
            }
            result << v;
            i++;
        }
    } else {
        result = values;
    }
    
    return result;
}

static void addItems(QTreeWidget *tw, QStringList &values, bool editable, bool checkable, bool icons)
{
    QString selectionType = tw->property("guid_list_selection_type").toString();
    
    for (int i = 0; i < values.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < tw->columnCount(); ++j) {
            itemValues << values.at(i++);
            if (i == values.count())
                break;
        }
        
        QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
        tw->addTopLevelItem(item);
        
        Qt::ItemFlags flags = item->flags();
        if (editable)
            flags |= Qt::ItemIsEditable;
        item->setFlags(flags);
        
        if (selectionType == "checklist") {
            QCheckBox *cb = new QCheckBox();
            cb->setContentsMargins(0, 0, 0, 0);
            if (itemValues.at(0).toLower() == "true")
                cb->setCheckState(Qt::Checked);
            else
                cb->setCheckState(Qt::Unchecked);
            cb->setStyleSheet("QCheckBox::indicator {subcontrol-position: center center;}");
            tw->setItemWidget(item, 0, cb);
        } else if (selectionType == "radiolist") {
            QRadioButton *rb = new QRadioButton();
            rb->setContentsMargins(0, 0, 0, 0);
            if (itemValues.at(0).toLower() == "true")
                rb->setChecked(true);
            else
                rb->setChecked(false);
            rb->setStyleSheet("QRadioButton::indicator {subcontrol-position: center center;}");
            tw->setItemWidget(item, 0, rb);
        }
        if (icons)
            item->setIcon(0, QPixmap(item->text(0)));
        if (checkable || icons) {
            item->setData(0, Qt::EditRole, item->text(0));
            item->setText(0, QString());
        }
    }
}

static QSize getQTreeWidgetSize(QTreeWidget **qtw)
{
    QTreeWidget *tw = *qtw;
    int rows = 0;
    int height = 2 * tw->frameWidth();
    if (!tw->isHeaderHidden())
        height += tw->header()->sizeHint().height();
    
    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
        rows += 1;
        QTreeWidgetItem *twi = tw->topLevelItem(i);
        QRect rec = twi->treeWidget()->visualItemRect(twi);
        height += rec.height();
    }
    
    return QSize(tw->header()->length() + 2 * tw->frameWidth(), height);
}

static void buildFormsList(QTreeWidget **tree, GList &list, QStringList &columns, bool &showHeader,
                           Qt::ItemFlags &flags, int &height)
{
    QTreeWidget *tw = *tree;

    if (!tw)
        return;
    
    tw->setRootIsDecorated(false);
    int columnCount = columns.count();
    tw->setHeaderHidden(!showHeader);
    if (columns.count()) {
        tw->setColumnCount(columns.count());
        tw->setHeaderLabels(columns);
    } else {
        columnCount = 1;
    }
    
    list.val = addColumnToListValues(list.val, list.addValue, columnCount);
    QString selectionType = tw->property("guid_list_selection_type").toString();
    
    for (int i = 0; i < list.val.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < columnCount; ++j) {
            itemValues << list.val.at(i++);
            if (i == list.val.count())
                break;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
        tw->addTopLevelItem(item);
        
        flags |= item->flags();
        item->setFlags(flags);
        item->setTextAlignment(0, Qt::AlignLeft);
        
        if (selectionType == "checklist") {
            QCheckBox *cb = new QCheckBox();
            cb->setContentsMargins(0, 0, 0, 0);
            if (itemValues.at(0).toLower() == "true")
                cb->setCheckState(Qt::Checked);
            else
                cb->setCheckState(Qt::Unchecked);
            cb->setStyleSheet("QCheckBox::indicator {subcontrol-position: center center;}");
            tw->setItemWidget(item, 0, cb);
        } else if (selectionType == "radiolist") {
            QRadioButton *rb = new QRadioButton();
            rb->setContentsMargins(0, 0, 0, 0);
            if (itemValues.at(0).toLower() == "true")
                rb->setChecked(true);
            else
                rb->setChecked(false);
            rb->setStyleSheet("QRadioButton::indicator {subcontrol-position: center center;}");
            tw->setItemWidget(item, 0, rb);
        }
        
        if (!selectionType.isEmpty())
            item->setText(0, QString());
    }

    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    if (!selectionType.isEmpty())
        tw->header()->setSectionResizeMode(0, QHeaderView::Fixed);

    tw->setStyleSheet(QTREEWIDGET_STYLE);

    if (height >= 0 && height < getQTreeWidgetSize(&tw).height())
        tw->setMaximumHeight(height);

    list = GList();
    columns.clear();
    showHeader = false;
    flags = Qt::NoItemFlags;
    height = -1;
    *tree = NULL;
}

static ValuePair getFormsWidgetValue(const QWidget *w, const QString &dateFormat,
                                     const QString &separator, const QString &listRowSeparator)
{
    if (!w)
        return ValuePair(false, QString());
    
    QString var = w->property("guid_var").toString().simplified().replace(" ", "");
    if (!var.isEmpty())
        var += "=";
    
    IF_IS(QLineEdit) {
        return ValuePair(true, var + t->text());
    } else IF_IS(QTreeWidget) {
        QString results;
        QString rowValue;
        QString printColumn = t->property("guid_list_print_column").toString();
        QString printMode = t->property("guid_list_print_values_mode").toString();
        QString selectionType = t->property("guid_list_selection_type").toString();
        QList<QTreeWidgetItem*> itemsToCheck;
        
        if (selectionType == "checklist" || selectionType == "radiolist" || printMode == "all") {
            for (int i = 0; i < t->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = t->topLevelItem(i);
                itemsToCheck << item;
            }
        } else {
            itemsToCheck = t->selectedItems();
        }
        
        if (selectionType == "checklist" || selectionType == "radiolist") {
            bool isChecked = false;
            int itemNo = 0;
            foreach (QTreeWidgetItem *item, itemsToCheck) {
                if (selectionType == "checklist") {
                    QCheckBox *cb = qobject_cast<QCheckBox*>(t->itemWidget(item, 0));
                    isChecked = cb->isChecked();
                } else if (selectionType == "radiolist") {
                    QRadioButton *rb = qobject_cast<QRadioButton*>(t->itemWidget(item, 0));
                    isChecked = rb->isChecked();
                }
                
                if (isChecked || printMode == "all") {
                    rowValue = "";
                    for (int i = 0; i < t->columnCount(); ++i) {
                        if (printColumn == "all" || printColumn == QString::number(i + 1)) {
                            if (i > 0)
                                rowValue += ',';
                            
                            if (i == 0) {
                                if (isChecked)
                                    rowValue += "true";
                                else
                                    rowValue += "false";
                            } else {
                                rowValue += item->text(i);
                            }
                        }
                    }
                    if (itemNo > 0)
                        results += listRowSeparator;
                    results += rowValue;
                    itemNo++;
                }
            }
        } else {
            int itemNo = 0;
            foreach (QTreeWidgetItem *item, itemsToCheck) {
                rowValue = "";
                for (int i = 0; i < t->columnCount(); ++i) {
                    if (printColumn == "all" || printColumn == QString::number(i + 1)) {
                        if (i > 0)
                            rowValue += ',';
                        rowValue += item->text(i);
                    }
                }
                if (itemNo > 0)
                    results += listRowSeparator;
                results += rowValue;
                itemNo++;
            }
        }
        return ValuePair(true, var + results);
    } else IF_IS(QComboBox) {
        return ValuePair(true, var + t->currentText());
    } else IF_IS(QCalendarWidget) {
        if (dateFormat.isNull())
            return ValuePair(true, var + QLocale::system().toString(t->selectedDate(), QLocale::ShortFormat));
        return ValuePair(true, var + t->selectedDate().toString(dateFormat));
    } else IF_IS(QCheckBox) {
        return ValuePair(true, t->isChecked() ? var + "true" : var + "false");
    } else IF_IS(QSlider) {
        return ValuePair(true, var + QString::number(t->value()));
    } else IF_IS(QSpinBox) {
        return ValuePair(true, var + QString::number(t->value()));
    } else IF_IS(QDoubleSpinBox) {
        return ValuePair(true, var + QString::number(t->value()));
    } else IF_IS(QTabWidget) {
        QString tabsValue = NULL;
        ValuePair resultPair;
        ValuePair tabPair;
        int nbResults = 0;
        for (int i = 0; i < t->count(); ++i) {
            QWidget *tab = t->widget(i);
            QList<QWidget*> tabChildren = tab->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            foreach(QWidget *tabChild, tabChildren) {
                if (qstrcmp(tabChild->metaObject()->className(), "QLabel") == 0)
                    continue;
                tabPair = getFormsWidgetValue(tabChild, dateFormat, separator, listRowSeparator);
                if (tabPair.first) {
                    if (nbResults > 0) {
                        tabsValue += separator;
                    }
                    tabsValue += tabPair.second;
                    nbResults++;
                }
            }
        }
        resultPair = ValuePair(true, tabsValue);
        if (tabsValue.isNull())
            resultPair.first = false;
        else
            resultPair.second = var + resultPair.second;
        return resultPair;
    } else IF_IS(QTextEdit) {
        if (!t->isReadOnly()) {
            ValuePair resultPair = ValuePair(true, var + t->toPlainText());
            QString nsep = t->property("guid_text_info_nsep").toString();
            if (!nsep.isEmpty())
                resultPair.second.replace("\n", nsep);
            return resultPair;
        } else {
            return ValuePair(false, QString());
        }
    } else IF_IS(QWidget) {
        QString widgetsValue = NULL;
        ValuePair resultPair;
        ValuePair widgetPair;
        if (t->property("guid_list_container").toBool() ||
            t->property("guid_cols_container").toBool() ||
            t->property("guid_scale_container").toBool()) {
            int nbResults = 0;
            QList<QWidget*> wChildren = t->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            foreach(QWidget *widget, wChildren) {
                if (qstrcmp(widget->metaObject()->className(), "QLabel") == 0)
                    continue;
                widgetPair = getFormsWidgetValue(widget, dateFormat, separator, listRowSeparator);
                if (widgetPair.first) {
                    if (nbResults > 0) {
                        widgetsValue += separator;
                    }
                    widgetsValue += widgetPair.second;
                    nbResults++;
                }
            }
        }
        resultPair = ValuePair(true, widgetsValue);
        if (widgetsValue.isNull())
            resultPair.first = false;
        else
            resultPair.second = var + resultPair.second;
        return resultPair;
    }
    return ValuePair(false, QString());
}

static GList listValuesFromFile(QString data)
{
    GList list = GList();
    
    list.addValue = "";
    if (data.contains(QRegExp("^addValue=.+//"))) {
        list.addValue = data.section("//", 0, 0).replace("addValue=", "");
        data = data.section("//", 1, -1);
    }
    
    list.monitorFile = false;
    if (data.contains(QRegExp("^monitor=.//"))) {
        if (data.mid(8, 1) == "1")
            list.monitorFile = true;
        data.remove(0, 11);
    }
    
    list.fileSep = "|";
    if (data.contains(QRegExp("^sep=.//"))) {
        list.fileSep = data.mid(4, 1);
        data.remove(0, 7);
    }
    
    list.filePath = data;
    QFile file(list.filePath);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    
    if (file.open(QIODevice::ReadOnly)) {
        list.val = QString::fromLocal8Bit(file.readAll()).trimmed().replace(QRegExp("[\r\n]+"), list.fileSep).split(list.fileSep);
        file.close();
    }
    
    return list;
}

static void setCol2Label(QHBoxLayout* &colsHBoxLayout, QLabel* label)
{
    label->setContentsMargins(0, 3, 0, 0);
    colsHBoxLayout->addWidget(label);
    colsHBoxLayout->setAlignment(label, Qt::AlignTop);
}

static void setFormsColumns(QWidget* &colsContainer, QHBoxLayout* &colsHBoxLayout, QString prop_separator, bool &columnsAreSet)
{
    colsContainer = new QWidget();
    colsContainer->setProperty("guid_cols_container", true);
    colsContainer->setProperty("guid_separator", prop_separator);
    colsHBoxLayout->setContentsMargins(0, 0, 0, 0);
    colsContainer->setLayout(colsHBoxLayout);
    columnsAreSet = true;
}

static void setTabs(QTabWidget* &tabBar, QFormLayout* &layout, QString &tabName, int &tabIndex)
{
    layout->addRow(tabBar);
    tabBar = new QTabWidget();
    tabName = "";
    tabIndex = -1;
}

static void setTextInfo(QTextEdit* textInfo, QString filename, bool isReadOnly, bool isUrl,
                        QString format, QString curlPath, int heightToSet)
{
    textInfo->setReadOnly(isReadOnly);
    if (textInfo->isReadOnly()) {
        QPalette pal = textInfo->viewport()->palette();
        for (int i = 0; i < 3; ++i) { // Disabled, Active, Inactive, Normal
            QPalette::ColorGroup cg = (QPalette::ColorGroup)i;
            pal.setColor(cg, QPalette::Base, pal.color(cg, QPalette::Window));
            pal.setColor(cg, QPalette::Text, pal.color(cg, QPalette::WindowText));
        }
        textInfo->viewport()->setPalette(pal);
        textInfo->viewport()->setAutoFillBackground(false);
        textInfo->setFrameStyle(QFrame::NoFrame);
    }
    
    if (isUrl) {
        if (curlPath.isEmpty())
            curlPath = "curl";
        QProcess *curl = new QProcess;
        QObject::connect(curl, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() {
            QByteArray content = curl->readAllStandardOutput();
            while (content.right(1) == "\n")
                content.chop(1);
            if (format == "html")
                textInfo->setHtml(QString::fromLocal8Bit(content));
            else if (format == "plain")
                textInfo->setPlainText(QString::fromLocal8Bit(content));
            else
                textInfo->setText(QString::fromLocal8Bit(content));
            delete curl;
        });
        curl->start(curlPath, QStringList() << "-L" << "-s" << filename);
    } else {
        QFile file(filename);
        QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            while (content.right(1) == "\n")
                content.chop(1);
            if (format == "html")
                textInfo->setHtml(QString::fromLocal8Bit(content));
            else if (format == "plain")
                textInfo->setPlainText(QString::fromLocal8Bit(content));
            else
                textInfo->setText(QString::fromLocal8Bit(content));
            file.close();
        }
    }
    
    QFont defaultFont = textInfo->document()->defaultFont();
    QFontMetrics fontMetrics = QFontMetrics(defaultFont);
    QSize size = fontMetrics.size(0, textInfo->toPlainText());
    qreal documentMargin = textInfo->document()->documentMargin();
    QMargins contentsMargins = textInfo->contentsMargins();
    int currentHeight = size.height() + contentsMargins.top() + contentsMargins.bottom() + documentMargin * 2;
    
    if (!isUrl && format != "html")
        textInfo->setMaximumHeight(currentHeight);
    
    if (heightToSet >= 0 && (heightToSet < currentHeight || format == "html" || isUrl))
        textInfo->setMaximumHeight(heightToSet);
}

// End of "static functions"

/******************************************************************************
 * public
 ******************************************************************************/

Guid::Guid(int &argc, char **argv) : QApplication(argc, argv),
    m_closeToSysTray(false),
    m_dialog(NULL),
    m_modal(false),
    m_notificationId(0),
    m_parentWindow(0),
    m_prefixErr(""),
    m_prefixOk(""),
    m_selectableLabel(false),
    m_sysTray(NULL),
    m_sysTrayMsg(false),
    m_timeout(0),
    m_type(Invalid)
{
    QStringList argList = QCoreApplication::arguments(); // arguments() is slow
    m_zenity = argList.at(0).endsWith("zenity");
    // make canonical list
    QStringList args;
    if (argList.at(0).endsWith("-askpass")) {
        argList.removeFirst();
        args << "--title" << tr("Enter Password") << "--password" << "--prompt" << argList.join(' ');
    } else {
        for (int i = 1; i < argList.count(); ++i) {
            if (argList.at(i).startsWith("--")) {
                int split = argList.at(i).indexOf('=');
                if (split > -1) {
                    args << argList.at(i).left(split) << argList.at(i).mid(split+1);
                } else {
                    args << argList.at(i);
                }
            } else {
                args << argList.at(i);
            }
        }
    }
    argList.clear();

    if (!readGeneral(args))
        return;

    char error = 1;
    foreach (const QString &arg, args) {
        if (arg == "--calendar") {
            m_type = Calendar;
            error = showCalendar(args);
        } else if (arg == "--entry") {
            m_type = Entry;
            error = showEntry(args);
        } else if (arg == "--error") {
            m_type = Error;
            error = showMessage(args, 'e');
        } else if (arg == "--info") {
            m_type = Info;
            error = showMessage(args, 'i');
        } else if (arg == "--file-selection") {
            m_type = FileSelection;
            error = showFileSelection(args);
        } else if (arg == "--list") {
            m_type = List;
            error = showList(args);
        } else if (arg == "--notification") {
            m_type = Notification;
            error = showNotification(args);
        } else if (arg == "--progress") {
            m_type = Progress;
            error = showProgress(args);
        } else if (arg == "--question") {
            m_type = Question;
            error = showMessage(args, 'q');
        } else if (arg == "--warning") {
            m_type = Warning;
            error = showMessage(args, 'w');
        } else if (arg == "--scale") {
            m_type = Scale;
            error = showScale(args);
        } else if (arg == "--text-info") {
            m_type = TextInfo;
            error = showText(args);
        } else if (arg == "--color-selection") {
            m_type = ColorSelection;
            error = showColorSelection(args);
        } else if (arg == "--font-selection") {
            m_type = FontSelection;
            error = showFontSelection(args);
        } else if (arg == "--password") {
            m_type = Password;
            error = showPassword(args);
        } else if (arg == "--forms") {
            m_type = Forms;
            error = showForms(args);
        }
        if (error != 1) {
            break;
        }
    }

    if (error) {
        QMetaObject::invokeMethod(this, "exitGuid", Qt::QueuedConnection, Q_ARG(int, 2));
        return;
    }

    if (m_dialog) {
        #if QT_VERSION >= 0x050000
            // this hacks access to the --title parameter in Qt5
            // for some reason it's not set on the dialog.
            // since it's set on showing the first QWindow, we just create one here and copy the title
            // TODO: remove once this is fixed in Qt5
            QWindow *w = new QWindow;
            w->setVisible(true);
            m_dialog->setWindowTitle(w->title());
            delete w;
        #endif
        // close on ctrl+return in addition to ctrl+enter
        QAction *shortAccept = new QAction(m_dialog);
        m_dialog->addAction(shortAccept);
        shortAccept->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return));
        connect (shortAccept, SIGNAL(triggered()), m_dialog, SLOT(accept()));

        // workaround for #21 - since QWidget is now merely bitrot, QDialog closes,
        // but does not reject on the escape key (unlike announced in the specific section of the API)
        QAction *shortReject = new QAction(m_dialog);
        m_dialog->addAction(shortReject);
        shortReject->setShortcut(QKeySequence(Qt::Key_Escape));
        connect (shortReject, SIGNAL(triggered()), m_dialog, SLOT(reject()));

        if (!m_size.isNull()) {
            m_dialog->adjustSize();
            QSize sz = m_dialog->size();
            if (m_size.width() > 0)
                sz.setWidth(m_size.width());
            if (m_size.height() > 0)
                sz.setHeight(m_size.height());
            m_dialog->resize(sz);
        }
        m_dialog->setWindowModality(m_modal ? Qt::ApplicationModal : Qt::NonModal);
        if (!m_caption.isNull())
            m_dialog->setWindowTitle(m_caption);
        if (!m_icon.isNull())
            m_dialog->setWindowIcon(QIcon(m_icon));
        QDialogButtonBox *box = m_dialog->findChild<QDialogButtonBox*>();
        if (box && !m_ok.isNull()) {
            if (QPushButton *btn = box->button(QDialogButtonBox::Ok))
                btn->setText(m_ok);
        }
        if (box && !m_cancel.isNull()) {
            if (QPushButton *btn = box->button(QDialogButtonBox::Cancel))
                btn->setText(m_cancel);
        }
        if (m_parentWindow) {
            #ifdef WS_X11
                m_dialog->setAttribute(Qt::WA_X11BypassTransientForHint);
                XSetTransientForHint(QX11Info::display(), m_dialog->winId(), m_parentWindow);
            #endif
        }
    }
}

void Guid::printHelp(const QString &category)
{
    static HelpDict helpDict;
    QString main_separator      = "#####################################################################################";
    QString secondary_separator = "-------------------------------------------------------------------------------------";
    
    const char *addMenuHelpDescription = "Add a new Menu in forms dialog.\n"
        "Note that this widget is not a user input field, so it doesn't have any impact\n"
        "on the field count when parsing output printed on the console.\n"
        "First level menu items must be separated with the symbol \"|\". Example:\n"
        "Main Item A|Main Item B|Main Item C\n"
        "Second level menu items must be separated with the symbol \"#\". Example:\n"
        "Main Item A#Subitem A1#Subitem A2|Main Item B|Main Item C#Subitem C1\n"
        "Each menu item that doesn't contain subitems can have up to 5 settings separated\n"
        "with \";\": Menu item name;Exit code;Command to run;Print command output;Icon\n"
        "The menu item name is mandatory. Other settings have these default values:\n"
        "Menu item name;-1;\"\";0;\"\"\n"
        "Possible values are as follows:\n"
        "- Menu item name: string not containing the character \";\".\n"
        "- Exit code: integer. If the value is < 0, the dialog will not be closed after\n"
        "             a click on the menu item. If the value is >=0 and <= 255, the dialog\n"
        "             will be closed with the exit code sent by the command \"exit()\".\n"
        "- Command to run: the executable name/path and all arguments must be separated\n"
        "                  with \"@@\". For example, if the command to run is the following\n:"
        "                  explorer.exe /separate, \"C:\\Users\\My Name\\Desktop\"\n"
        "                  it must be set like this:\n"
        "                  explorer.exe@@/separate,@@\"C:\\Users\\My Name\\Desktop\"\n"
        "- Print command output: 0 or 1. If the value is 1, the command output will be\n"
        "                        printed on the console.\n"
        "- Icon: for now, the only valid value is 0. If the value is 0, the default icon\n"
        "        won't be added to the left of the menu item. TODO: set arbitrary icon.\n"
        "No matter if the dialog is closed or not after a click on the menu item, all\n"
        "settings (menu name, exit code, etc.) are printed on the console after a click,\n"
        "so it can be parsed by a script. Here's an example:\n"
        "guid --forms --add-menu=\"File#Profile;-1;explorer.exe@@%UserProfile%;0|Exit;10\"\n"
        "To add separators between first level menu items, add \"sep=SEP//\" at the\n"
        "beginning of the menu settings. Example:\n"
        "guid --forms --add-menu=\"sep=|//File#Profile;-1;explorer.exe@@%UserProfile%;0|Exit;10\"\n"
        "If a menu is the first widget added to the form and the form doesn't have a main\n"
        "label added with \"--text=TEXT\", special settings are applied to the menu to have it\n"
        "look like a main application menu. If we want a menu on top of the dialog but still\n"
        "have a form label, we must not add the form label with \"--text=TEXT\", but with\n"
        "\"--add-text=TEXT\" after the menu. Example:\n"
        "guid --forms --add-menu=\"Item A;10|Item B;20\" --add-text=\"Form label\" --bold";
    
    if (helpDict.isEmpty()) {
        /******************************
         * help
         ******************************/
        
        helpDict["help"] = CategoryHelp(tr("Help options"), HelpList() <<
        Help("-h, --help", tr("Show help options")) <<
        Help("--help-all", tr("Show all help options")) <<
        Help("--help-general", tr("Show general options")) <<
        Help("--help-misc", tr("Show miscellaneous options")) <<
        Help("--help-qt", tr("Show Qt Options")) <<
        Help("", "") <<
        
        Help("--help-calendar", tr("Show calendar options")) <<
        Help("--help-color-selection", tr("Show color selection options")) <<
        Help("--help-entry", tr("Show text entry options")) <<
        Help("--help-error", tr("Show error options")) <<
        Help("--help-file-selection", tr("Show file selection options")) <<
        Help("--help-forms", tr("Show forms dialog options")) <<
        Help("--help-info", tr("Show info options")) <<
        Help("--help-list", tr("Show list options")) <<
        Help("--help-notification", tr("Show notification icon options")) <<
        Help("--help-password", tr("Show password dialog options")) <<
        Help("--help-progress", tr("Show progress options")) <<
        Help("--help-question", tr("Show question options")) <<
        Help("--help-scale", tr("Show scale options")) <<
        Help("--help-text-info", tr("Show text information options")) <<
        Help("--help-warning", tr("Show warning options")));
            
        /******************************
         * misc
         ******************************/
        
        helpDict["misc"] = CategoryHelp(tr("Miscellaneous options"), HelpList() <<
        Help("--about", tr("About guid")) <<
        Help("--version", tr("Print version")));
            
        /******************************
         * qt
         ******************************/
        
        helpDict["qt"] = CategoryHelp(tr("Qt options"), HelpList() <<
        Help("--foo", tr("Foo")) <<
        Help("--bar", tr("Bar")));
            
        /******************************
         * general
         ******************************/
        
        helpDict["general"] = CategoryHelp(tr("General options"), HelpList() <<
        Help("--title=TITLE", tr("Set the dialog title")) <<
        Help("--window-icon=ICONPATH", tr("Set the window icon")) <<
        Help("--width=WIDTH", tr("Set the width")) <<
        Help("--height=HEIGHT", tr("Set the height")) <<
        Help("", "") <<
        
        Help("--ok-label=TEXT", tr("Sets the label of the Ok button")) <<
        Help("--cancel-label=TEXT", tr("Sets the label of the Cancel button")) <<
        Help("", "") <<
        
        Help("--attach=WINDOW", tr("Set the parent window to attach to")) <<
        Help("--modal", tr("Set the modal hint")) <<
        Help("--output-prefix-ok=PREFIX", "GUID ONLY! " + tr("Set prefix for output sent to stdout")) <<
        Help("--output-prefix-err=PREFIX", "GUID ONLY! " + tr("Set prefix for output sent to stderr")) <<
        Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")));
            
        /******************************
         * application
         ******************************/
        
        helpDict["application"] = CategoryHelp(tr("Application Options"), HelpList() <<
        Help("--display=DISPLAY", tr("X display to use")) <<
        Help("", "") <<
        
        Help("--calendar", tr("Display calendar dialog")) <<
        Help("--color-selection", tr("Display color selection dialog")) <<
        Help("--entry", tr("Display text entry dialog")) <<
        Help("--error", tr("Display error dialog")) <<
        Help("--file-selection", tr("Display file selection dialog")) <<
        Help("--font-selection", "GUID ONLY! " + tr("Display font selection dialog")) <<
        Help("--forms", tr("Display forms dialog")) <<
        Help("--info", tr("Display info dialog")) <<
        Help("--list", tr("Display list dialog")) <<
        Help("--notification", tr("Display notification")) <<
        Help("--password", tr("Display password dialog")) <<
        Help("--progress", tr("Display progress indication dialog")) <<
        Help("--question", tr("Display question dialog")) <<
        Help("--scale", tr("Display scale dialog")) <<
        Help("--text-info", tr("Display text information dialog")) <<
        Help("--warning", tr("Display warning dialog")));
            
        /******************************
         * calendar
         ******************************/
        
        helpDict["calendar"] = CategoryHelp(tr("Calendar options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--day=DAY", tr("Set the calendar day")) <<
        Help("--month=MONTH", tr("Set the calendar month")) <<
        Help("--year=YEAR", tr("Set the calendar year")) <<
        Help("--date-format=PATTERN", tr("Set the format for the returned date")) <<
        Help("", "") <<
        
        Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")));
            
        /******************************
         * color-selection
         ******************************/
        
        helpDict["color-selection"] = CategoryHelp(tr("Color selection options"), HelpList() <<
        Help("--color=VALUE", tr("Set the color")) <<
        Help("--custom-palette=path/to/some.gpl",  "GUID ONLY! " + tr("Load a custom GPL for standard colors")) <<
        Help("--show-palette", tr("Show the palette")));
            
        /******************************
         * entry
         ******************************/
        
        helpDict["entry"] = CategoryHelp(tr("Text entry options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--entry-text=TEXT", tr("Set the entry text")) <<
        Help("--hide-text", tr("Hide the entry text")) <<
        Help("", "") <<
        
        Help("--float=floating_point", "GUID ONLY! " + tr("Floating point input only, preset given value")) <<
        Help("--int=integer", "GUID ONLY! " + tr("Integer input only, preset given value")) <<
        Help("--values=v1|v2|v3|...", "GUID ONLY! " + tr("Offer preset values to pick from")));
            
        /******************************
         * error
         ******************************/
        
        helpDict["error"] = CategoryHelp(tr("Error options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
        Help("--no-markup", tr("Do not enable html markup")) <<
        Help("--no-wrap", tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
            
        /******************************
         * file-selection
         ******************************/
        
        helpDict["file-selection"] = CategoryHelp(tr("File selection options"), HelpList() <<
        Help("--filename=FILENAME", tr("Set the filename")) <<
        Help("--file-filter=NAME | PATTERN1 PATTERN2 ...", tr("Sets a filename filter")) <<
        Help("--directory", tr("Activate directory-only selection")) <<
        Help("--multiple", tr("Allow multiple files to be selected")) <<
        Help("", "") <<
        
        Help("--confirm-overwrite", tr("Confirm file selection if filename already exists")) <<
        Help("--save", tr("Activate save mode")) <<
        Help("", "") <<
        
        Help("--separator=SEPARATOR", tr("Set output separator character")));
            
        /******************************
         * font-selection
         ******************************/
        
        helpDict["font-selection"] = CategoryHelp(tr("Font selection options"), HelpList() <<
        Help("--sample=TEXT", tr("Sample text, defaults to the foxdogthing")) <<
        Help("--pattern=%1-%2:%3:%4", tr("Output pattern, %1: Name, %2: Size, %3: weight, %4: slant")) <<
        Help("--type=[vector][,bitmap][,fixed][,variable]", tr("Filter fonts (default: all)")));
        
        /******************************
         * forms
         ******************************/
        
        helpDict["forms"] = CategoryHelp(tr("Forms dialog options"), HelpList() <<
        
        // --text
        Help("--text=TEXT", tr("Set the dialog text (always displayed on top")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("--no-bold", "GUID ONLY! " + tr("Remove bold for the dialog text")) <<
        Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
        Help("--underline", "GUID ONLY! " + tr("Set text format to underline")) <<
        Help("--small-caps", "GUID ONLY! " + tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY", "GUID ONLY! " + tr("Set font family")) <<
        Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
        Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example:\nguid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
        Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example:\nguid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
        Help("", "") <<
        
        // --tab
        Help("--tab=NAME", "GUID ONLY! " + tr("Create a tab bar.\nNext fields will be added to the tab NAME unless another one is specified with\n \"--tab=ANOTHER_NAME\". Stop adding fields in the last tab with --tab=\"stop\".\nExample:\nguid --forms --text=\"Form with a tab bar\" --tab=\"Tab 1\" --add-entry=\"Text field\"\n     --add-hrule=\"#e0e0e0\" --add-entry=\"Another text field\" --tab=\"Tab 2\"\n     --add-spin-box=\"Spin box field\" --min-value=10 --value=50 --tab=\"stop\"\n     --add-scale=\"Field outside tabs\" --step=5")) <<
        Help("--tab-selected", "GUID ONLY! " + tr("Mark the tab as selected by default")) <<
        Help("", "") <<
        
        // --col1 || --col2
        Help("--col1", "GUID ONLY! " + tr("Start a two-field row.\nThe next form field specified will be added in the first column.\nSee --col2 for details.")) <<
        Help("--col2", "GUID ONLY! " + tr("Finish a two-field row.\nThe next form field specified will be added in the second column. Example:\nguid --forms --width=500 --text=\"The next row has two columns:\"\n     --col1 --add-entry=\"Left label\" --int=5 --col2 --add-entry=\"Right label\"\n     --field-width=100 --add-entry=\"Full-width row\"")) <<
        Help("", "") <<
        
        // --add-calendar
        Help("--add-calendar=Calendar field name", tr("Add a new Calendar in forms dialog")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-checkbox
        Help("--add-checkbox=Checkbox label", "GUID ONLY! " + tr("Add a new Checkbox forms dialog")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-combo
        Help("--add-combo=Combo box field name", tr("Add a new combo box in forms dialog")) <<
        Help("--combo-values=List of values separated by |", tr("List of values for combo box")) <<
        Help("--combo-values-from-file=[monitor=VALUE//]FILENAME", "GUID ONLY! " + tr("Open file and use content as combo values.\nTo monitor file changes, add \"monitor=1\" followed by \"//\". Example:\nguid --forms --add-combo=\"Combo description\"\n     --combo-values-from-file=\"monitor=1//C:\\Users\\name\\Desktop\\file.txt\"")) <<
        Help("--editable", "GUID ONLY! " + tr("Allow changes to text")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-entry
        Help("--add-entry=Field name", tr("Add a new Entry in forms dialog")) <<
        Help("--float=floating_point", "GUID ONLY! " + tr("Floating point input only, preset given value")) <<
        Help("--int=integer", "GUID ONLY! " + tr("Integer input only, preset given value")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-list
        Help("--add-list=[addNewRowButton=1//]List label", tr("Add a new List in forms dialog.\nTo add a button allowing to add new rows when clicked, start the list label\nwith \"addNewRowButton=1\" followed by \"//\". Example:\nguid --forms --add-list=\"addNewRowButton=1//My list\" --column-values=\"C1|C2\"\n     --show-header --list-values=\"v1|v2|v3|v4\"")) <<
        Help("--column-values=List of values separated by |", tr("List of values for columns")) <<
        Help("--list-values=List of values separated by |", tr("List of values for List")) <<
        Help("--list-values-from-file=[addValue=VALUE//][monitor=1//][sep=SEP//]FILENAME", "GUID ONLY! " + tr("Open file and use content as list values. Example:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"/path/to/file\"\nTo add automatically a value at the beginning of each row (for example\nthe value \"false\" in combination with \"--checklist\"), add \"addValue=VALUE\"\nfollowed by \"//\". Example:\nguid --forms --add-list=\"List description\" --column-values=\"Delete|Column 1|Column 2\"\n     --show-header --list-values-from-file=\"addValue=false///path/to/file\" --checklist\nTo monitor file changes, add \"monitor=1\" followed by \"//\". Example:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"monitor=1///path/to/file\"\nBy default, the symbol \"|\" is used as separator between values.\nTo use another separator, specify it with \"sep=SEP\" followed by \"//\". Example\nwith \",\" as separator:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n--show-header --list-values-from-file=\"sep=,///path/to/file\"\nExample with file monitord and custom separator:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n--show-header --list-values-from-file=\"monitor=1//sep=,///path/to/file\"")) <<
        Help("--checklist", tr("Use check boxes for first column.\nMultiple rows will be selectable.")) <<
        Help("--radiolist", tr("Use radio buttons for first column.\nOnly one row will be selectable.")) <<
        Help("--editable", "GUID ONLY! " + tr("Allow changes to text")) <<
        Help("--multiple", "GUID ONLY! " + tr("Allow multiple rows to be selected")) <<
        Help("--no-selection", "GUID ONLY! " + tr("Prevent row selection (useful to display information in a structured way\nwithout user interaction)")) <<
        Help("--print-column=NUMBER|all", tr("Print a specific column.\nDefault is 1. The value \"all\" can be used to print all columns.")) <<
        Help("--print-values=selected|all", "GUID ONLY! " + tr("Print selected values (default)\nor all values (useful in combination with --editable to get updated values).")) <<
        Help("--list-row-separator=SEPARATOR", "GUID ONLY! " + tr("Set output separator character for list rows (default is ~)")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--field-height=HEIGHT", "GUID ONLY! " + tr("Set the field height")) <<
        Help("--show-header", tr("Show the columns header")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-menu
        Help("--add-menu=Menu settings", "GUID ONLY! " + tr(addMenuHelpDescription)) <<
        Help("", "") <<
        
        // --add-password
        Help("--add-password=Field name", tr("Add a new Password Entry in forms dialog")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-scale
        Help("--add-scale=Field name", tr("Add a new Scale/Slider in forms dialog")) <<
        Help("--value=VALUE", tr("Set initial value")) <<
        Help("--min-value=VALUE", tr("Set minimum value")) <<
        Help("--max-value=VALUE", tr("Set maximum value")) <<
        Help("--step=VALUE", tr("Set step size")) <<
        Help("--hide-value", tr("Hide value")) <<
        Help("--print-partial", tr("Print partial values")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-spin-box
        Help("--add-spin-box=Spin box name", "GUID ONLY! " + tr("Add a new spin box in forms dialog")) <<
        Help("--value=VALUE", tr("Set initial value")) <<
        Help("--min-value=VALUE", "GUID ONLY! " + tr("Set minimum value")) <<
        Help("--max-value=VALUE", "GUID ONLY! " + tr("Set maximum value")) <<
        Help("--prefix=PREFIX", "GUID ONLY! " + tr("Set prefix")) <<
        Help("--suffix=SUFFIX", "GUID ONLY! " + tr("Set suffix")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-double-spin-box
        Help("--add-double-spin-box=Double spin box name", "GUID ONLY! " + tr("Add a new double spin box in forms dialog")) <<
        Help("--value=VALUE", tr("Set initial value")) <<
        Help("--decimals=VALUE", "GUID ONLY! " + tr("Set the number of decimals")) <<
        Help("--min-value=VALUE", "GUID ONLY! " + tr("Set minimum value")) <<
        Help("--max-value=VALUE", "GUID ONLY! " + tr("Set maximum value")) <<
        Help("--prefix=PREFIX", "GUID ONLY! " + tr("Set prefix")) <<
        Help("--suffix=SUFFIX", "GUID ONLY! " + tr("Set suffix")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output.\nSpaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-text
        Help("--add-text=TEXT", "GUID ONLY! " + tr("Add text without field.\nNote that this widget is not a user input field, so it doesn't have any impact\non the field count when parsing output printed on the console.")) <<
        Help("--tooltip=Tooltip text", "GUID ONLY! " + tr("Set tooltip displayed when the cursor is over")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("--bold", "GUID ONLY! " + tr("Set text in bold")) <<
        Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
        Help("--underline", "GUID ONLY! " + tr("Set text format to underline")) <<
        Help("--small-caps", "GUID ONLY! " + tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY", "GUID ONLY! " + tr("Set font family")) <<
        Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
        Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example:\nguid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
        Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example:\nguid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("", "") <<
        
        // --add-image-text
        Help("--add-image-text=PATH_TO_IMAGE//TEXT", "GUID ONLY! " + tr("Add an image and text without field.\nSet the path to the image followed by \"//\", then the text that will be\ndisplayed to the right of the image. Example:\nguid --forms --add-image-text=\"/path/to/image.png//Text displayed\"\nDefault resources shipped with guid can be used. To do so, start the image path\nwith \":/\" followed by the name of the resource. Example to use an icon\ndisplaying the number 5:\nguid --forms --add-image-text=\":/5//Text displayed\"\nNote that this widget is not a user input field, so it doesn't have any impact\non the field count when parsing output printed on the console.")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("--bold", "GUID ONLY! " + tr("Set text in bold")) <<
        Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
        Help("--underline", "GUID ONLY! " + tr("Set text format to underline")) <<
        Help("--small-caps", "GUID ONLY! " + tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY", "GUID ONLY! " + tr("Set font family")) <<
        Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
        Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example:\nguid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
        Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example:\nguid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("", "") <<
        
        // --add-hrule
        Help("--add-hrule=COLOR", "GUID ONLY! " + tr("Add horizontal rule and set the color specified. Example:\nguid --forms --text=\"Text\" --add-hrule=\"#B1B1B1\" --add-entry=\"Text field\"\nNote that this widget is not a user input field, so it doesn't have any impact\non the field count when parsing output printed on the console.")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("", "") <<
        
        // --add-spacer
        Help("--add-spacer=HEIGHT", "GUID ONLY! " + tr("Add vertical spacer.\nNote that this widget is not a user input field, so it doesn't have any impact\non the field count when parsing output printed on the console.")) <<
        Help("", "") <<
        
        // --add-text-info
        Help("--add-text-info=Field name", "GUID ONLY! " + tr("Add text information\nNote that this widget is a user input field only when the argument \"--editable\"\nis used.")) <<
        Help("--filename=FILENAME", tr("Get content from the specified file")) <<
        Help("--url=URL", "REQUIRES CURL BINARY! " + tr("Set an URL instead of a file.")) <<
        Help("--curl-path=PATH", "GUID ONLY! " + tr("Set the path to the curl binary. Default is \"curl\".")) <<
        Help("--editable", tr("Allow changes to text")) <<
        Help("--plain", "GUID ONLY! " + tr("Force plain text (zenity default limitation), otherwise guid will\ntry to guess the format")) <<
        Help("--html", tr("Force HTML format, otherwise guid will try to guess the format")) <<
        Help("--newline-separator=SEPARATOR", "GUID ONLY! " + tr("Replace newlines with SEPARATOR in the text output, so everything's\noutput on one line")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--field-height=HEIGHT", "GUID ONLY! " + tr("Set the field height")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("--bold", "GUID ONLY! " + tr("Set text in bold")) <<
        Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
        Help("--underline", "GUID ONLY! " + tr("Set text format to underline")) <<
        Help("--small-caps", "GUID ONLY! " + tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY", "GUID ONLY! " + tr("Set font family")) <<
        Help("--font=TEXT", tr("Set the text font")) <<
        Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
        Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example:\nguid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
        Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example:\nguid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
        Help("--var=NAME", "GUID ONLY! " + tr("Variable name added before the field output (when the field is\neditable). Spaces are removed and the character \"=\" is added after the\nvariable name. Example:\nguid --forms --add-calendar=\"Choose a date\" --var=\"cal\"\n     --add-entry=\"Type your pseudo\" --var=\"pseudo\"\nHere's what the output looks like: cal=2020-12-12|pseudo=Abcde\nWithout \"--var\", the output would be: 2020-12-12|Abcde")) <<
        Help("", "") <<
        
        // --add-text-browser
        Help("--add-text-browser=Field name", "GUID ONLY! " + tr("Add read-only HTML information with click on links enabled.\nNote that this widget is not a user input field, so it doesn't have any impact\non the field count when parsing output printed on the console.")) <<
        Help("--filename=FILENAME", tr("Get content from the specified file")) <<
        Help("--url=URL", "REQUIRES CURL BINARY! " + tr("Set an URL instead of a file.")) <<
        Help("--curl-path=PATH", "GUID ONLY! " + tr("Set the path to the curl binary. Default is \"curl\".")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
        Help("--field-height=HEIGHT", "GUID ONLY! " + tr("Set the field height")) <<
        Help("", "") <<
        
        // misc.
        Help("--win-min-button", tr("Add a \"Minimize\" button to the dialog window")) <<
        Help("--win-max-button", tr("Add a \"Maximize\" button to the dialog window")) <<
        Help("--close-to-systray", tr("Hide the dialog when clicking on the window \"Close\" button instead of exiting.\nThe system tray must be enabled with \"--systray-icon=ICON\".")) <<
        Help("--systray-icon=ICON", tr("Add the icon specified in the system tray.\nClicking on the \"Close\" window button will minimize the dialog in the systray,\nand a menu will be displayed with a right-click on the systray icon.")) <<
        Help("--forms-date-format=PATTERN", tr("Set the format for the returned date")) <<
        Help("--forms-align=left|center|right", "GUID ONLY! " + tr("Set label alignment for the entire form")) <<
        Help("--separator=SEPARATOR", tr("Set output separator character")));
        
        /******************************
         * info
         ******************************/
        
        helpDict["info"] = CategoryHelp(tr("Info options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
        Help("--no-markup", tr("Do not enable html markup")) <<
        Help("--no-wrap", tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
        
        /******************************
         * list
         ******************************/
        
        helpDict["list"] = CategoryHelp(tr("List options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--checklist", tr("Use check boxes for first column.\nMultiple rows will be selectable.")) <<
        Help("--imagelist", tr("Use an image for first column")) <<
        Help("--radiolist", tr("Use radio buttons for first column.\nOnly one row will be selectable.")) <<
        Help("", "") <<
        
        Help("--column=COLUMN", tr("Set the column header")) <<
        Help("--hide-column=NUMBER", tr("Hide a specific column")) <<
        Help("--print-column=NUMBER|all", tr("Print a specific column.\nDefault is 1. The value \"all\" can be used to print all columns.")) <<
        Help("--hide-header", tr("Hides the column headers")) <<
        Help("", "") <<
        
        Help("--list-values-from-file=[monitor=1//][sep=SEP//]FILENAME", "GUID ONLY! " + tr("Open file and use content as list values. Example:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"/path/to/file\"\nTo monitor file changes, add \"monitor=1\" followed by \"//\". Example:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"monitor=1///path/to/file\"\nBy default, the symbol \"|\" is used as separator between values.\nTo use another separator, specify it with \"sep=SEP\" followed by \"//\".\nExample with \",\" as separator:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"sep=,///path/to/file\"\nExample with file monitord and custom separator:\nguid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\"\n     --show-header --list-values-from-file=\"monitor=1//sep=,///path/to/file\"")) <<
        Help("", "") <<
        
        Help("--editable", tr("Allow changes to text")) <<
        Help("--multiple", tr("Allow multiple rows to be selected")) <<
        Help("--no-selection", "GUID ONLY! " + tr("Prevent row selection (useful to display information in a structured way\nwithout user interaction)")) <<
        Help("--print-values=selected|all", "GUID ONLY! " + tr("Print selected values (default)\nor all values (useful in combination with --editable to get updated values).")) <<
        Help("", "") <<
        
        Help("--mid-search", tr("Change list default search function searching for text in the middle,\nnot on the beginning")) <<
        Help("--field-height=HEIGHT", "GUID ONLY! " + tr("Set the field height")) <<
        Help("--separator=SEPARATOR", tr("Set output separator character")));
        
        /******************************
         * notification
         ******************************/
        
        helpDict["notification"] = CategoryHelp(tr("Notification icon options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--hint=TEXT", tr("Set the notification hints")) <<
        Help("", "") <<
        
        Help("--listen", tr("Listen for commands on stdin")) <<
        Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
        
        /******************************
         * password
         ******************************/
        
        helpDict["password"] = CategoryHelp(tr("Password dialog options"), HelpList() <<
        Help("--prompt=TEXT", "GUID ONLY! " + tr("The prompt for the user")) <<
        Help("--username", tr("Display the username option")) <<
        Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")));
        
        /******************************
         * progress
         ******************************/
        
        helpDict["progress"] = CategoryHelp(tr("Progress options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--percentage=PERCENTAGE", tr("Set initial percentage")) <<
        Help("--pulsate", tr("Pulsate progress bar")) <<
        Help("", "") <<
        
        Help("--auto-close", tr("Dismiss the dialog when 100% has been reached")) <<
        Help("--auto-kill", tr("Kill parent process if Cancel button is pressed")) <<
        Help("--no-cancel", tr("Hide Cancel button")));
        
        /******************************
         * question
         ******************************/
        
        helpDict["question"] = CategoryHelp(tr("Question options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
        Help("--no-markup", tr("Do not enable html markup")) <<
        Help("--no-wrap", tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--default-cancel", tr("Give cancel button focus by default")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
        
        /******************************
         * scale
         ******************************/
        
        helpDict["scale"] = CategoryHelp(tr("Scale options"), HelpList() <<
        Help("--text=TEXT", tr("Set the dialog text")) <<
        Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--value=VALUE", tr("Set initial value")) <<
        Help("--min-value=VALUE", tr("Set minimum value")) <<
        Help("--max-value=VALUE", tr("Set maximum value")) <<
        Help("--step=VALUE", tr("Set step size")) <<
        Help("", "") <<
        
        Help("--hide-value", tr("Hide value")) <<
        Help("--print-partial", tr("Print partial values")));
        
        /******************************
         * text-info
         ******************************/
        
        helpDict["text-info"] = CategoryHelp(tr("Text information options"), HelpList() <<
        Help("--filename=FILENAME", tr("Open file")) <<
        Help("", "") <<
        
        Help("--url=URL", "REQUIRES CURL BINARY! " + tr("Set an URL instead of a file.")) <<
        Help("--curl-path=PATH", "GUID ONLY! " + tr("Set the path to the curl binary. Default is \"curl\".")) <<
        Help("", "") <<
        
        Help("--checkbox=TEXT", tr("Enable an I read and agree checkbox")) <<
        Help("", "") <<
        
        Help("--editable", tr("Allow changes to text")) <<
        Help("--font=TEXT", tr("Set the text font")) <<
        Help("--plain", "GUID ONLY! " + tr("Force plain text (zenity default limitation), otherwise guid will\ntry to guess the format")) <<
        Help("--html", tr("Force HTML format, otherwise guid will try to guess the format")) <<
        Help("", "") <<
        
        Help("--auto-scroll", tr("Auto scroll the text to the end. Only when text is captured from stdin")) <<
        Help("--no-interaction", tr("Do not enable user interaction with the WebView.\nOnly works if you use --html option.")));
        
        /******************************
         * warning
         ******************************/
        
        helpDict["warning"] = CategoryHelp(tr("Warning options"), HelpList() <<
        Help("--text=TEXT", tr("Set\nthe dialog text")) <<
        Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
        Help("--no-markup", tr("Do not enable html markup")) <<
        Help("--no-wrap", tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
    }
    
    QStringList helpGeneralCats = {"help", "misc", "qt", "general", "application"}; 
    QStringList helpWidgetCats  = {"calendar", "color-selection", "entry", "error", "file-selection",
                                   "font-selection", "forms", "info", "list", "notification", "password",
                                   "progress", "scale", "text-info", "warning"};
    QStringList helpCatsToDisplay = helpGeneralCats;
    
    if (category == "all") {
        helpCatsToDisplay << helpWidgetCats;
    } else if (helpDict.contains(category)) {
        helpCatsToDisplay << category;
    }
    
    printf("\nUsage: guid [OPTION ...]\n");
    
    for (int i = 0; i < helpCatsToDisplay.count(); ++i) {
        QString catName = helpCatsToDisplay.at(i);
        
        printf("\n%s\n# %s\n%s\n\n", qPrintable(main_separator),
                             qPrintable(helpDict[catName].first),
                             qPrintable(main_separator));
        
        foreach (const Help &help, helpDict[catName].second) {
            QString helpArgument = help.first;
            QString helpDescription = help.second;
            
            if (helpArgument.isEmpty() && helpDescription.isEmpty()) {
                printf("%s\n", qPrintable(secondary_separator));
            } else {
                helpDescription = "      " + helpDescription;
                helpDescription.replace("\n", "\n      ");
                printf("%s\n%s\n", qPrintable(help.first), qPrintable(helpDescription));
            }
        }
    }
    
    return;
}

// End of "public"

/******************************************************************************
 * private slots
 ******************************************************************************/

void Guid::addListRow()
{
    QTreeWidget *list = sender()->parent()->findChild<QTreeWidget*>();
    if (list && list->topLevelItemCount() > 0) {
        QTreeWidgetItem *firstItem = list->topLevelItem(0);
        QTreeWidgetItem *newItem = firstItem->clone();
        list->addTopLevelItem(newItem);
        list->setCurrentItem(newItem);
        list->scrollToItem(newItem);
        for (int i = 0; i < newItem->columnCount(); ++i) {
            newItem->setText(i, QString());
        }
        
        QWidget *firstItemWidget = list->itemWidget(firstItem, 0);
        if (firstItemWidget) {
            if (qstrcmp(firstItemWidget->metaObject()->className(), "QCheckBox") == 0) {
                QCheckBox *cb = new QCheckBox();
                cb->setContentsMargins(0, 0, 0, 0);
                cb->setCheckState(Qt::Unchecked);
                cb->setStyleSheet("QCheckBox::indicator {subcontrol-position: center center;}");
                list->setItemWidget(newItem, 0, cb);
            } else if (qstrcmp(firstItemWidget->metaObject()->className(), "QRadioButton") == 0) {
                QRadioButton *rb = new QRadioButton();
                rb->setContentsMargins(0, 0, 0, 0);
                rb->setChecked(false);
                rb->setStyleSheet("QRadioButton::indicator {subcontrol-position: center center;}");
                list->setItemWidget(newItem, 0, rb);
            }
        }
    }
}

void Guid::afterCloseButtonClick()
{
    exitGuid(1);
}

void Guid::afterMenuClick()
{
    QOUT
    
    QString menuItemName = sender()->property("guid_menu_item_name").toString();
    int menuItemExitCode = sender()->property("guid_menu_item_exit_code").toInt();
    QString menuItemCommand = sender()->property("guid_menu_item_command").toString();
    bool menuItemCommandPrintOutput = sender()->property("guid_menu_item_command_print_output").toBool();
    
    QString output = QString("%1MENU_CLICKED_DATA_START|name=%2|exitCode=%3|command=%4|commandPrintOutput=%5|commandOutput=")
        .arg(m_prefixOk)
        .arg(menuItemName)
        .arg(QString::number(menuItemExitCode))
        .arg(menuItemCommand)
        .arg(QString::number(menuItemCommandPrintOutput));
    
    qOut << output;
    
    if (menuItemCommand.isEmpty() || !menuItemCommandPrintOutput) {
        qOut << "|MENU_CLICKED_DATA_END" << Qt::endl;
    }
    
    if (!menuItemCommand.isEmpty()) {
        QStringList commandArgs = menuItemCommand.split("@@");
        QString commandExec = commandArgs[0];
        commandArgs.removeFirst();
        QProcess *process = new QProcess;
        
        if (menuItemCommandPrintOutput) {
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() {
                QString commandOutput = QString::fromLocal8Bit(process->readAllStandardOutput());
                QOUT
                qOut << commandOutput << "|MENU_CLICKED_DATA_END";
                delete process;
            });
            process->start(commandExec, commandArgs);
            qOut << Qt::endl;
        } else {
            process->startDetached(commandExec, commandArgs);
        }
    }
    
    if (menuItemExitCode >=0 && menuItemExitCode <= 255) {
        QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
        if (sysTrayIcon)
            sysTrayIcon->hide();
        exit(menuItemExitCode);
    }
}

void Guid::dialogFinished(int status)
{
    QOUT
    QOUT_ERR
    
    if (m_type == FileSelection) {
        QFileDialog *dlg = static_cast<QFileDialog*>(sender());
        QVariantList l;
        for (int i = 0; i < dlg->sidebarUrls().count(); ++i)
            l << dlg->sidebarUrls().at(i);
        QSettings settings("guid");
        settings.setValue("Bookmarks", l);
        settings.setValue("FileDetails", dlg->viewMode() == QFileDialog::Detail);
    }

    if (!(status == QDialog::Accepted || status == QMessageBox::Ok || status == QMessageBox::Yes)) {
        #ifdef Q_OS_UNIX
            if (sender()->property("guid_autokill_parent").toBool()) {
                ::kill(getppid(), 15);
            }
        #endif
        bool minimize = false;
        if (m_closeToSysTray && m_sysTray && m_type == Forms) {
            minimize = true;
        }
        exitGuid(4, minimize);
        return;
    }

    switch (m_type) {
        case Question:
        case Warning:
        case Info:
        case Error:
        case Progress:
        case Notification:
            break;
        case Calendar: {
            QString dateFormat = sender()->property("guid_date_format").toString();
            QDate date = sender()->findChild<QCalendarWidget*>()->selectedDate();
            if (dateFormat.isEmpty())
                qOut << m_prefixOk + QLocale::system().toString(date, QLocale::ShortFormat);
            else
                qOut << m_prefixOk + date.toString(dateFormat);
            break;
        }
        case Entry: {
            QInputDialog *dlg = static_cast<QInputDialog*>(sender());
            if (dlg->inputMode() == QInputDialog::DoubleInput) {
                qOut << m_prefixOk + QLocale::c().toString(dlg->doubleValue(), 'f', 2);
            } else if (dlg->inputMode() == QInputDialog::IntInput) {
                qOut << m_prefixOk + dlg->intValue();
            } else {
                qOut << m_prefixOk + dlg->textValue();
            }
            break;
        }
        case Password: {
            QLineEdit   *username = sender()->findChild<QLineEdit*>("guid_username"),
                        *password = sender()->findChild<QLineEdit*>("guid_password");
            QString result;
            if (username)
                result = username->text() + '|';
            if (password)
                result += password->text();
            qOut << m_prefixOk + result;
            break;
        }
        case FileSelection: {
            QStringList files = static_cast<QFileDialog*>(sender())->selectedFiles();
            qOut << m_prefixOk + files.join(sender()->property("guid_separator").toString());
            break;
        }
        case ColorSelection: {
            QColorDialog *dlg = static_cast<QColorDialog*>(sender());
            qOut << m_prefixOk + dlg->selectedColor().name();
            QVariantList l;
            for (int i = 0; i < dlg->customCount(); ++i)
                l << dlg->customColor(i).rgba();
            QSettings("guid").setValue("CustomPalette", l);
            break;
        }
        case FontSelection: {
            QFontDialog *dlg = static_cast<QFontDialog*>(sender());
            QFont fnt = dlg->selectedFont();
            int size = fnt.pointSize();
            if (size < 0)
                size = fnt.pixelSize();

            // crude mapping of Qt's random enum to xft's random category
            QString weight = "medium";
            if (fnt.weight() < 35) weight = "light";
            else if (fnt.weight() > 85) weight = "black";
            else if (fnt.weight() > 70) weight = "bold";
            else if (fnt.weight() > 60) weight = "demibold";

            QString slant = "roman";
            if (fnt.style() == QFont::StyleItalic) slant = "italic";
            else if (fnt.style() == QFont::StyleOblique) slant = "oblique";

            QString font = sender()->property("guid_fontpattern").toString();
            font = font.arg(fnt.family()).arg(size).arg(weight).arg(slant);
            qOut << m_prefixOk + font;
            break;
        }
        case TextInfo: {
            QTextEdit *te = sender()->findChild<QTextEdit*>();
            if (te && !te->isReadOnly()) {
                qOut << m_prefixOk + te->toPlainText();
            }
            break;
        }
        case Scale: {
            QSlider *sld = sender()->findChild<QSlider*>();
            if (sld) {
                qOut << m_prefixOk + QString::number(sld->value());
            }
            break;
        }
        case List: {
            QTreeWidget *tw = sender()->findChild<QTreeWidget*>();
            QStringList result;
            if (tw) {
                QString rowValue;
                QString printColumn = tw->property("guid_list_print_column").toString();
                QString printMode = tw->property("guid_list_print_values_mode").toString();
                QString selectionType = tw->property("guid_list_selection_type").toString();
                QList<QTreeWidgetItem*> itemsToCheck;
                
                if (selectionType == "checklist" || selectionType == "radiolist" || printMode == "all") {
                    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                        QTreeWidgetItem *item = tw->topLevelItem(i);
                        itemsToCheck << item;
                    }
                } else {
                    itemsToCheck = tw->selectedItems();
                }
                
                if (selectionType == "checklist" || selectionType == "radiolist") {
                    bool isChecked = false;
                    foreach (QTreeWidgetItem *twi, itemsToCheck) {
                        if (selectionType == "checklist") {
                            QCheckBox *cb = qobject_cast<QCheckBox*>(tw->itemWidget(twi, 0));
                            isChecked = cb->isChecked();
                        } else if (selectionType == "radiolist") {
                            QRadioButton *rb = qobject_cast<QRadioButton*>(tw->itemWidget(twi, 0));
                            isChecked = rb->isChecked();
                        }
                        
                        if (isChecked || printMode == "all") {
                            rowValue = "";
                            for (int i = 0; i < tw->columnCount(); ++i) {
                                if (printColumn == "all" || printColumn == QString::number(i + 1)) {
                                    if (i > 0)
                                        rowValue += ',';
                                    
                                    if (i == 0) {
                                        if (isChecked)
                                            rowValue += "true";
                                        else
                                            rowValue += "false";
                                    } else {
                                        rowValue += twi->text(i);
                                    }
                                }
                            }
                            result << rowValue;
                        }
                    }
                } else {
                    foreach (QTreeWidgetItem *twi, itemsToCheck) {
                        rowValue = "";
                        for (int i = 0; i < tw->columnCount(); ++i) {
                            if (printColumn == "all" || printColumn == QString::number(i + 1)) {
                                if (i > 0)
                                    rowValue += ',';
                                rowValue += twi->text(i);
                            }
                        }
                        result << rowValue;
                    }
                }
            }
            qOut << m_prefixOk + result.join(sender()->property("guid_separator").toString());
            break;
        }
        case Forms: {
            QList<QFormLayout*> layouts = sender()->findChildren<QFormLayout*>();
            // We skip the first layout used for the top menu
            QFormLayout *fl = layouts.at(1);
            QStringList result;
            ValuePair resultPair;
            QString dateFormat = sender()->property("guid_date_format").toString();
            QString separator = sender()->property("guid_separator").toString();
            QString listRowSeparator = sender()->property("guid_list_row_separator").toString();
            for (int i = 0; i < fl->count(); ++i) {
                if (QLayoutItem *li = fl->itemAt(i, QFormLayout::FieldRole)) {
                    resultPair = getFormsWidgetValue(li->widget(), dateFormat, separator, listRowSeparator);
                    if (resultPair.first)
                        result << resultPair.second;
                }
            }
            qOut << m_prefixOk + result.join(separator);
            break;
        }
        default:
            qOutErr << m_prefixErr + "unhandled output" << m_type;
            break;
    }
    exitGuid(0);
}

void Guid::exitGuid(int exitCode, bool minimize)
{
    if (minimize) {
        // Cancel the exit process
        showDialog();
        // Minimize window
        QTimer *qtimer = new QTimer(this);
        qtimer->singleShot(10, this, SLOT(minimizeDialog()));
        qtimer->deleteLater();
    } else {
        QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
        if (sysTrayIcon)
            sysTrayIcon->hide();
        exit(exitCode);
    }
}

void Guid::finishProgress()
{
    Q_ASSERT(m_type == Progress);
    QProgressDialog *dlg = static_cast<QProgressDialog*>(m_dialog);
    if (dlg->property("guid_autoclose").toBool())
        QTimer::singleShot(250, this, SLOT(quitDialog()));
    else {
        dlg->setRange(0, 101);
        dlg->setValue(100);
        disconnect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
        connect (dlg, SIGNAL(canceled()), dlg, SLOT(accept()));
        dlg->setCancelButtonText(m_ok.isNull() ? tr("Ok") : m_ok);
        if (QPushButton *btn = dlg->findChild<QPushButton*>())
            btn->show();
    }
}

void Guid::minimizeDialog()
{
    QDialog *dlg = static_cast<QDialog*>(m_dialog);
    QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
    if (dlg && sysTrayIcon) {
        if (!m_sysTrayMsg) {
            QMessageBox::information(
                dlg,
                "Dialog in the system tray",
                "The dialog will keep running in the system tray. "
                "You can click on its icon to open the dialog again or to close it."
            );
            m_sysTrayMsg = true;
        }
        dlg->hide();
        setSysTrayAction("Minimize", false);
        setSysTrayAction("Show", true);
    }
}

void Guid::printInteger(int v)
{
    QOUT
    qOut << m_prefixOk + QString::number(v);
}

void Guid::quitDialog()
{
    exitGuid(0);
}

void Guid::readStdIn()
{
    QOUT_ERR
    if (!gs_stdin->isOpen())
        return;
    QSocketNotifier *notifier = qobject_cast<QSocketNotifier*>(sender());
    if (notifier)
        notifier->setEnabled(false);

    QByteArray ba = m_type == TextInfo ? gs_stdin->readAll() : gs_stdin->readLine();
    if (ba.isEmpty() && notifier) {
        gs_stdin->close();
        //gs_stdin->deleteLater(); // hello segfault...
        //gs_stdin = NULL;
        notifier->deleteLater();
        return;
    }

    static QString cachedText;
    QString newText = QString::fromLocal8Bit(ba);
    if (newText.isEmpty() && cachedText.isEmpty()) {
        if (notifier)
            notifier->setEnabled(true);
        return;
    }

    QStringList input;
    if (m_type != TextInfo) {
        if (newText.endsWith('\n'))
            newText.resize(newText.length()-1);
        input = newText.split('\n');
    }
    if (m_type == Progress) {
        QProgressDialog *dlg = static_cast<QProgressDialog*>(m_dialog);

        const int oldValue = dlg->value();
        bool ok;
        foreach (QString line, input) {
            if (line.startsWith('#')) {
                dlg->setLabelText(labelText(line.mid(1)));
            } else {
                static const QRegularExpression nondigit("[^0-9]");
                int u = line.section(nondigit,0,0).toInt(&ok);
                if (ok)
                    dlg->setValue(qMin(100,u));
            }
        }

        if (dlg->maximum() == 0)
            return; // we just need the label support

        if (dlg->value() == 100) {
            finishProgress();
        } else if (oldValue == 100) {
            disconnect (dlg, SIGNAL(canceled()), dlg, SLOT(accept()));
            connect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
            dlg->setCancelButtonText(m_cancel.isNull() ? tr("Cancel") : m_cancel);
        } else if (dlg->property("guid_eta").toBool()) {
            static QDateTime starttime;
            if (starttime.isNull()) {
                starttime = QDateTime::currentDateTime();
            } else if (dlg->value() > 0) {
                const qint64 secs = starttime.secsTo(QDateTime::currentDateTime());
                QString eta = QTime(0,0,0).addSecs(100 * secs / dlg->value() - secs).toString();
                foreach (QWidget *w, dlg->findChildren<QWidget*>())
                    w->setToolTip(eta);
            }
        }
    } else if (m_type == TextInfo) {
        if (QTextEdit *te = m_dialog->findChild<QTextEdit*>()) {
            cachedText += newText;
            static QPropertyAnimation *animator = NULL;
            if (!animator || animator->state() != QPropertyAnimation::Running) {
                const int oldValue = te->verticalScrollBar() ? te->verticalScrollBar()->value() : 0;
                if (te->property("guid_html").toBool())
                    te->setHtml(te->toHtml() + cachedText);
                else
                    te->setPlainText(te->toPlainText() + cachedText);
                cachedText.clear();
                if (te->verticalScrollBar() && te->property("guid_autoscroll").toBool()) {
                    te->verticalScrollBar()->setValue(oldValue);
                    if (!animator) {
                        animator = new QPropertyAnimation(te->verticalScrollBar(), "value", this);
                        animator->setEasingCurve(QEasingCurve::InOutCubic);
                        connect(animator, SIGNAL(finished()), SLOT(readStdIn()));
                    }
                    const int diff = te->verticalScrollBar()->maximum() - oldValue;
                    if (diff > 0) {
                        animator->setDuration(qMin(qMax(200, diff), 2500));
                        animator->setEndValue(te->verticalScrollBar()->maximum());
                        animator->start();
                    }
                }
            }
        }
    } else if (m_type == Notification) {
        bool userNeedsHelp = true;
        foreach (QString line, input) {
            int split = line.indexOf(':');
            if (split < 0)
                continue;
            if (line.left(split) == "icon") {
                userNeedsHelp = false;
                // TODO: some icon filename, seems gnome specific and i've no idea how to handle this atm.
                qOutErr << m_prefixErr + "'icon' command not yet supported - if you know what this is supposed to do, please file a bug";
            } else if (line.left(split) == "message" || line.left(split) == "tooltip") {
                userNeedsHelp = false;
                notify(line.mid(split+1));
            } else if (line.left(split) == "visible") {
                userNeedsHelp = false;
                if (m_dialog)
                    m_dialog->setVisible(line.mid(split+1).trimmed().compare("false", Qt::CaseInsensitive) &&
                                         line.mid(split+1).trimmed().compare("0", Qt::CaseInsensitive));
                else
                    qOutErr << m_prefixErr + "'visible' command only supported for failsafe dialog notification";
            } else if (line.left(split) == "hints") {
                m_notificationHints = line.mid(split+1);
            }
        }
        if (userNeedsHelp)
            qOutErr << m_prefixErr + "icon: <filename>\nmessage: <UTF-8 encoded text>\ntooltip: <UTF-8 encoded text>\nvisible: <true|false>";
    } else if (m_type == List) {
        if (QTreeWidget *tw = m_dialog->findChild<QTreeWidget*>()) {
            const int twflags = tw->property("guid_list_flags").toInt();
            addItems(tw, input, twflags & 1, twflags & 1<<1, twflags & 1<<2);
        }
    }
    if (notifier)
        notifier->setEnabled(true);
}

void Guid::showDialog()
{
    QDialog *dlg = static_cast<QDialog*>(m_dialog);
    if (dlg) {
        dlg->showNormal();
        setSysTrayAction("Show", false);
        setSysTrayAction("Minimize", true);
    }
}

void Guid::showSysTrayMenu(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
        if (sysTrayIcon)
            sysTrayIcon->contextMenu()->popup(QCursor::pos());
    }
}

void Guid::toggleItems(QTreeWidgetItem *item, int column)
{
    if (column)
        return; // not the checkmark

    static bool recursion = false;
    if (recursion)
        return;

    recursion = true;
    QTreeWidget *tw = item->treeWidget();
    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
        QTreeWidgetItem *twi = tw->topLevelItem(i);
        if (twi != item)
            twi->setCheckState(0, Qt::Unchecked);
    }
    recursion = false;
}

void Guid::updateCombo(QString filePath)
{
    if (!QFile::exists(filePath))
        return;
    
    foreach (QComboBox *combo, sender()->parent()->findChildren<QComboBox*>()) {
        if (combo->property("guid_monitor_file").toBool() && combo->property("guid_file_path").toString() == filePath) {
            combo->clear();
            GList list = listValuesFromFile(filePath);
            combo->addItems(list.val);
        }
    }
}

void Guid::updateList(QString filePath)
{
    if (!QFile::exists(filePath))
        return;
    
    foreach (QTreeWidget *tw, sender()->parent()->findChildren<QTreeWidget*>()) {
        bool propMonitorFile = tw->property("guid_monitor_file").toBool();
        QString propFilePath = tw->property("guid_file_path").toString();
        QString propSelectionType = tw->property("guid_list_selection_type").toString();
        QString propAddValue = tw->property("guid_list_add_value").toString();
        QString propFileSep = tw->property("guid_file_sep").toString();
        
        if (propMonitorFile && propFilePath == filePath) {
            int columnCount = tw->columnCount();
            Qt::ItemFlags flags = tw->topLevelItem(0)->flags();
            tw->clear();
            
            QString fileArg = "";
            if (!propAddValue.isEmpty())
                fileArg += "addValue=" + propAddValue + "//";
            if (!propFileSep.isEmpty())
                fileArg += "sep=" + propFileSep + "//";
            fileArg += filePath;
            GList list = listValuesFromFile(fileArg);
            list.val = addColumnToListValues(list.val, list.addValue, columnCount);
            for (int i = 0; i < list.val.count(); ) {
                QStringList itemValues;
                for (int j = 0; j < columnCount; ++j) {
                    itemValues << list.val.at(i++);
                    if (i == list.val.count())
                        break;
                }
                QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
                tw->addTopLevelItem(item);
                
                flags |= item->flags();
                item->setFlags(flags);
                item->setTextAlignment(0, Qt::AlignLeft);
                
                if (propSelectionType == "checklist") {
                    QCheckBox *cb = new QCheckBox();
                    cb->setContentsMargins(0, 0, 0, 0);
                    if (itemValues.at(0).toLower() == "true")
                        cb->setCheckState(Qt::Checked);
                    else
                        cb->setCheckState(Qt::Unchecked);
                    cb->setStyleSheet("QCheckBox::indicator {subcontrol-position: center center;}");
                    tw->setItemWidget(item, 0, cb);
                } else if (propSelectionType == "radiolist") {
                    QRadioButton *rb = new QRadioButton();
                    rb->setContentsMargins(0, 0, 0, 0);
                    if (itemValues.at(0).toLower() == "true")
                        rb->setChecked(true);
                    else
                        rb->setChecked(false);
                    rb->setStyleSheet("QRadioButton::indicator {subcontrol-position: center center;}");
                    tw->setItemWidget(item, 0, rb);
                }
                
                if (!propSelectionType.isEmpty())
                    item->setText(0, QString());
            }
            
            for (int i = 0; i < columnCount; ++i) {
                tw->resizeColumnToContents(i);
            }
        }
    }
}

// End of "private slots"

/******************************************************************************
 * private (1 of 2): misc.
 ******************************************************************************/

void Guid::centerWidget(QWidget *widget, QWidget *host) {
    if (!host)
        host = widget->parentWidget();
    
    if (host) {
        auto hostRect = host->geometry();
        widget->move(hostRect.center() - widget->rect().center());
    } else {
        QRect screenGeometry = QGuiApplication::screens()[0]->geometry();
        int x = (screenGeometry.width() - widget->width()) / 2;
        int y = (screenGeometry.height() - widget->height()) / 2;
        widget->move(x, y);
    }
}

bool Guid::error(const QString message)
{
    QOUT_ERR
    qOutErr << m_prefixErr + message;
    QMetaObject::invokeMethod(this, "exitGuid", Qt::QueuedConnection, Q_ARG(int, 3));
    return true;
}

QString Guid::labelText(const QString &s) const
{
    // zenity uses pango markup, https://developer.gnome.org/pygtk/stable/pango-markup-language.html
    // This near-html-subset isn't really compatible w/ Qt's html subset and we end up
    // w/ a weird mix of ASCII escape codes and html tags
    // the below is NOT a perfect translation

    // known "caveats"
    // pango termiantes the string for "\0" (we do not - atm)
    // pango inserts some control char for "\f", but that's not reasonably handled by gtk label (so it's ignored here)
    if (m_zenity) {
        QString r = s;
        // First replace backslashes with alarms to avoid false positives below.
        // The alarm is treated as invalid and not supported by zenity/pango either
        r.replace("\\\\", "\a") \
         .replace("\\n", "<br>").replace("\\t", "&nbsp;&nbsp;&nbsp;") \
         .replace("\\r", "<br>");
        int idx = 0;
        while (true) {
            static const QRegularExpression octet("\\\\([0-9]{1,3})");
            idx = r.indexOf(octet, idx);
            if (idx < 0)
                break;
            int sz = 0;
            while (sz < 3 && r.at(idx+sz+1).isDigit())
                ++sz;
            r.replace(idx, sz+1, QChar(r.midRef(idx+1, sz).toUInt(nullptr, 8)));
        }
        r.remove("\\").replace(("\a"), "\\");
        return r;
    }
    return s;
}

void Guid::listenToStdIn()
{
    if (gs_stdin)
        return;
    gs_stdin = new QFile;
    if (gs_stdin->open(stdin, QIODevice::ReadOnly)) {
        QSocketNotifier *snr = new QSocketNotifier(gs_stdin->handle(), QSocketNotifier::Read, gs_stdin);
        connect (snr, SIGNAL(activated(int)), SLOT(readStdIn()));
    } else {
        delete gs_stdin;
        gs_stdin = NULL;
    }
}

void Guid::notify(const QString message, bool noClose)
{
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered("org.freedesktop.Notifications")) {
        QDBusInterface notifications("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications");
        const QString summary = (message.length() < 32) ? message : message.left(25) + "...";
        QVariantMap hintMap;
        QStringList hintList = m_notificationHints.split(':');
        for (int i = 0; i < hintList.count() - 1; i+=2)
            hintMap.insert(hintList.at(i), hintList.at(i+1));
        QDBusMessage msg = notifications.call("Notify", "Guid", m_notificationId, "dialog-information", summary, message,
                                              QStringList() /*actions*/, hintMap, m_timeout);
        if (msg.arguments().count())
            m_notificationId = msg.arguments().at(0).toUInt();
        return;
    }

    QMessageBox *dlg = static_cast<QMessageBox*>(m_dialog);
    if (!dlg) {
        dlg = new QMessageBox;
        dlg->setIcon(QMessageBox::Information);
        dlg->setStandardButtons(noClose ? QMessageBox::NoButton : QMessageBox::Ok);
        dlg->setWindowFlags(Qt::ToolTip);
        dlg->setWindowOpacity(0.8);
        if (QLabel *l = dlg->findChild<QLabel*>("qt_msgbox_label")) {
            l->setWordWrap(true);
            if (m_selectableLabel)
                l->setTextInteractionFlags(l->textInteractionFlags()|Qt::TextSelectableByMouse);
        }
    }
    dlg->setText(labelText(message));
    SHOW_DIALOG
    dlg->adjustSize();
    dlg->move(QGuiApplication::screens().at(0)->availableGeometry().topRight() - QPoint(dlg->width() + 20, -20));
}

bool Guid::readGeneral(QStringList &args) {
    QStringList remains;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--title") {
            m_caption = NEXT_ARG;
        } else if (args.at(i) == "--window-icon") {
            m_icon = NEXT_ARG;
        } else if (args.at(i) == "--width") {
            bool ok;
            const int w = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--width must be followed by a positive number");
            m_size.setWidth(w);
        } else if (args.at(i) == "--height") {
            bool ok;
            const int h = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--height must be followed by a positive number");
            m_size.setHeight(h);
        } else if (args.at(i) == "--timeout") {
            bool ok;
            const int t = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--timeout must be followed by a positive number");
            QTimer::singleShot(t*1000, this, SLOT(quitDialog()));
        } else if (args.at(i) == "--ok-label") {
            m_ok = NEXT_ARG;
        } else if (args.at(i) == "--cancel-label") {
            m_cancel = NEXT_ARG;
        } else if (args.at(i) == "--modal") {
            m_modal = true;
        } else if (args.at(i) == "--attach") {
            bool ok;
            const int w = NEXT_ARG.toUInt(&ok, 0);
            if (!ok)
                return !error("--attach must be followed by a positive number");
            m_parentWindow = w;
        } else if (args.at(i) == "--output-prefix-ok") {
            m_prefixOk = NEXT_ARG;
        } else if (args.at(i) == "--output-prefix-err") {
            m_prefixErr = NEXT_ARG;
        } else {
            remains << args.at(i);
        }
    }
    args = remains;
    return true;
}

void Guid::setSysTrayAction(QString actionId, bool valueToSet)
{
    QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
    if (sysTrayIcon) {
        QList<QAction*> actions = sysTrayIcon->contextMenu()->actions();
        foreach(QAction *action, actions) {
            if (action->property("guid_systray_menu_action").toString() == actionId) {
                action->setEnabled(valueToSet);
                break;
            }
        }
    }
}

// End of "private (1 of 2): misc."

/******************************************************************************
 * private (2 of 2): show dialogs
 ******************************************************************************/

char Guid::showCalendar(const QStringList &args)
{
    QOUT_ERR
    NEW_DIALOG

    QDate date = QDate::currentDate();
    int d,m,y;
    date.getDate(&y, &m, &d);
    QLabel *label = new QLabel("");
    bool ok;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text") {
            label = new QLabel(NEXT_ARG, dlg);
            vl->addWidget(label);
        } else if (args.at(i) == "--align") {
            if (label) {
                QString alignment = NEXT_ARG;
                if (alignment == "left")
                    label->setAlignment(Qt::AlignLeft);
                else if (alignment == "center")
                    label->setAlignment(Qt::AlignCenter);
                else if (alignment == "right")
                    label->setAlignment(Qt::AlignRight);
                else
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i);
            } else
                WARN_UNKNOWN_ARG("--text");
        } else if (args.at(i) == "--day") {
            d = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--day must be followed by a positive number");
        } else if (args.at(i) == "--month") {
            m = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--month must be followed by a positive number");
        } else if (args.at(i) == "--year") {
            y = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--year must be followed by a positive number");
        } else if (args.at(i) == "--date-format") {
            dlg->setProperty("guid_date_format", NEXT_ARG);
        } else { WARN_UNKNOWN_ARG("--calendar") }
    }
    date.setDate(y, m, d);

    QCalendarWidget *cal = new QCalendarWidget(dlg);
    cal->setSelectedDate(date);
    vl->addWidget(cal);
    connect(cal, SIGNAL(activated(const QDate&)), dlg, SLOT(accept()));

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

char Guid::showColorSelection(const QStringList &args)
{
    QOUT_ERR
    QColorDialog *dlg = new QColorDialog;
    QVariantList l = QSettings("guid").value("CustomPalette").toList();
    for (int i = 0; i < l.count() && i < dlg->customCount(); ++i)
        dlg->setCustomColor(i, QColor(l.at(i).toUInt()));
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--color") {
            dlg->setCurrentColor(QColor(NEXT_ARG));
        } else if (args.at(i) == "--show-palette") {
            qOutErr << m_prefixErr + "The show-palette parameter is not supported by guid. Sorry.";
            void(0);
        } else if (args.at(i) == "--custom-palette") {
            if (i+1 < args.count()) {
                QString path = NEXT_ARG;
                QFile file(path);
                if (file.open(QIODevice::ReadOnly)) {
                    QStringList pal = QString::fromLocal8Bit(file.readAll()).split('\n');
                    int r, g, b; bool ok; int idx = 0;
                    for (const QString &line : pal) {
                        if (idx > 47) break; // sorry :(
                        QStringList color = line.split(QRegularExpression("\\s+"), SKIP_EMPTY);
                        if (color.count() < 3) continue;
                        r = color.at(0).toInt(&ok); if (!ok) continue;
                        g = color.at(1).toInt(&ok); if (!ok) continue;
                        b = color.at(2).toInt(&ok); if (!ok) continue;
                        dlg->setStandardColor(idx++, QColor(r,g,b));
                    }
                    while (idx < 48) {
                        dlg->setStandardColor(idx++, Qt::black);
                    }
                    file.close();
                } else {
                    qOutErr << m_prefixErr + "Cannot read" << path.toLocal8Bit().constData();
                }
            } else {
                qOutErr << m_prefixErr + "You have to provide a gimp palette (*.gpl)";
            }
        }{ WARN_UNKNOWN_ARG("--color-selection") }
    }
    SHOW_DIALOG
    return 0;
}

char Guid::showEntry(const QStringList &args)
{
    QInputDialog *dlg = new QInputDialog;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setLabelText(labelText(NEXT_ARG));
        else if (args.at(i) == "--entry-text")
            dlg->setTextValue(NEXT_ARG);
        else if (args.at(i) == "--hide-text")
            dlg->setTextEchoMode(QLineEdit::Password);
        else if (args.at(i) == "--values") {
            dlg->setComboBoxItems(NEXT_ARG.split('|'));
            dlg->setComboBoxEditable(true);
        } else if (args.at(i) == "--int") {
            dlg->setInputMode(QInputDialog::IntInput);
            dlg->setIntRange(INT_MIN, INT_MAX);
            dlg->setIntValue(NEXT_ARG.toInt());
        } else if (args.at(i) == "--float") {
            dlg->setInputMode(QInputDialog::DoubleInput);
            dlg->setDoubleRange(DBL_MIN, DBL_MAX);
            dlg->setDoubleValue(NEXT_ARG.toDouble());
        }
        else { WARN_UNKNOWN_ARG("--entry") }
    }
    SHOW_DIALOG

    return 0;
}

char Guid::showFileSelection(const QStringList &args)
{
    QFileDialog *dlg = new QFileDialog;
    QSettings settings("guid");
    dlg->setViewMode(settings.value("FileDetails", false).toBool() ? QFileDialog::Detail : QFileDialog::List);
    dlg->setFileMode(QFileDialog::ExistingFile);
    dlg->setOption(QFileDialog::DontConfirmOverwrite, false);
    dlg->setProperty("guid_separator", "|");
    QVariantList l = settings.value("Bookmarks").toList();
    QList<QUrl> bookmarks;
    for (int i = 0; i < l.count(); ++i)
        bookmarks << l.at(i).toUrl();
    if (!bookmarks.isEmpty())
        dlg->setSidebarUrls(bookmarks);
    QStringList mimeFilters;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--filename") {
            QString path = NEXT_ARG;
            if (path.endsWith("/."))
                dlg->setDirectory(path);
            else
                dlg->selectFile(path);
        }
        else if (args.at(i) == "--multiple")
            dlg->setFileMode(QFileDialog::ExistingFiles);
        else if (args.at(i) == "--directory") {
            dlg->setFileMode(QFileDialog::Directory);
            dlg->setOption(QFileDialog::ShowDirsOnly);
        } else if (args.at(i) == "--save") {
            dlg->setFileMode(QFileDialog::AnyFile);
            dlg->setAcceptMode(QFileDialog::AcceptSave);
        }
        else if (args.at(i) == "--separator")
            dlg->setProperty("guid_separator", NEXT_ARG);
        else if (args.at(i) == "--confirm-overwrite")
            dlg->setOption(QFileDialog::DontConfirmOverwrite);
        else if (args.at(i) == "--file-filter") {
            QString mimeFilter = NEXT_ARG;
            const int idx = mimeFilter.indexOf('|');
            if (idx > -1)
                mimeFilter = mimeFilter.left(idx).trimmed() + " (" + mimeFilter.mid(idx+1).trimmed() + ")";
            mimeFilters << mimeFilter;
        }
        else { WARN_UNKNOWN_ARG("--file-selection") }
    }
    dlg->setNameFilters(mimeFilters);
    SHOW_DIALOG
    return 0;
}

char Guid::showFontSelection(const QStringList &args)
{
    QOUT_ERR
    QFontDialog *dlg = new QFontDialog;
    QString pattern = "%1-%2:%3:%4";
    QString sample = "The quick brown fox jumps over the lazy dog";
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--type") {
            QStringList types = NEXT_ARG.split(',');
            QFontDialog::FontDialogOptions opts;
            for (const QString &type : types) {
                if (type == "vector")   opts |= QFontDialog::ScalableFonts;
                if (type == "bitmap")   opts |= QFontDialog::NonScalableFonts;
                if (type == "fixed")    opts |= QFontDialog::MonospacedFonts;
                if (type == "variable") opts |= QFontDialog::ProportionalFonts;
            }
            if (opts) // https://bugreports.qt.io/browse/QTBUG-93473
                dlg->setOptions(opts);
            dlg->setCurrentFont(QFont()); // also works around the bug :P
        } else if (args.at(i) == "--pattern") {
            pattern = NEXT_ARG;
            if (!pattern.contains("%1"))
                qOutErr << m_prefixErr + "The output pattern doesn't include a placeholder for the font name...";
        } else if (args.at(i) == "--sample") {
            sample = NEXT_ARG;
        } { WARN_UNKNOWN_ARG("--font-selection") }
    }
    if (QLineEdit *smpl = dlg->findChild<QLineEdit*>("qt_fontDialog_sampleEdit"))
        smpl->setText(sample);
    dlg->setProperty("guid_fontpattern", pattern);
    SHOW_DIALOG
    return 0;
}

char Guid::showForms(const QStringList &args)
{
    QOUT_ERR
    NEW_DIALOG
    dlg->setProperty("guid_separator", "|");
    dlg->setProperty("guid_list_row_separator", "~");
    
    QString sysTrayIconPath = "";
    
    Qt::WindowFlags dlgFlags = Qt::WindowCloseButtonHint;
    dlg->setWindowFlags(dlgFlags);
    
    QLabel *label;
    vl->addWidget(label = new QLabel(dlg));
    label->setVisible(false);
    bool labelInBold = true;
    
    QFormLayout *flTopMenu = new QFormLayout();
    vl->addLayout(flTopMenu);
    
    QFormLayout *fl = new QFormLayout();
    vl->addLayout(fl);
    
    const int formsWidgetSpacing = 9;
    vl->layout()->setSpacing(formsWidgetSpacing);
    vl->addStretch();
    
    QTabWidget *lastTabBar = new QTabWidget();
    QFormLayout *lastTabLayout = NULL;
    QWidget *lastTab = NULL;
    QString lastTabName = "";
    int lastTabIndex = -1;
    
    bool columnsAreSet = false;
    QLabel *labelCol1 = NULL;
    QHBoxLayout *colsHBoxLayout = NULL;
    QWidget *colsContainer = NULL;
    QString lastColumn = NULL;
    
    QCheckBox *lastCheckbox = NULL;
    
    QCalendarWidget *lastCalendar = NULL;
    
    QLineEdit *lastEntry = NULL;
    
    QLineEdit *lastPassword = NULL;
    
    QLabel *lastText = NULL;
    
    QLabel *lastImageText = NULL;
    
    QLabel *lastHRule = NULL;
    QString lastHRuleCss = NULL;
    
    QLabel *lastSpacer = NULL;
    int lastSpacerHeight = 0;
    
    QComboBox *lastCombo = NULL;
    GList lastComboGList = GList();
    
    QWidget *lastListContainer = NULL;
    QFormLayout *lastListLayout = NULL;
    QPushButton *lastListButton = NULL;
    
    QTreeWidget *lastList = NULL;
    GList lastListGList = GList();
    QStringList lastListColumns;
    bool lastListHeader = false;
    Qt::ItemFlags lastListFlags;
    int lastListHeight = -1;
    
    QTextEdit *lastTextInfo = NULL;
    QTextBrowser *lastTextBrowser = NULL;
    bool lastTextIsReadOnly = NULL;
    QString lastTextFormat = NULL;
    QString lastTextCurlPath = NULL;
    QString lastTextFilename = NULL;
    bool lastTextIsUrl = NULL;
    int lastTextHeight = -1;
    
    QHBoxLayout *scaleHBoxLayout = NULL;
    QWidget *scaleContainer = NULL;
    QLabel *lastScaleLabel = NULL;
    QSlider *lastScale = NULL;
    QLabel *lastScaleVal = NULL;
    
    QSpinBox *lastSpinBox = NULL;
    QDoubleSpinBox *lastDoubleSpinBox = NULL;
    
    QString lastWidget = NULL;
    QMenuBar *lastMenu = NULL;
    bool topMenu = false;
    
    QFileSystemWatcher * comboWatcher = new QFileSystemWatcher(dlg);
    QFileSystemWatcher * listWatcher = new QFileSystemWatcher(dlg);
    
    QString lastVar = "";
    
    bool ok;
    
    for (int i = 0; i < args.count(); ++i) {
        /********************************************************************************
         * WIDGET CONTAINERS
         ********************************************************************************/
        
        // --tab
        if (args.at(i) == "--tab") {
            QString next_arg = NEXT_ARG;
            if (next_arg == "stop") {
                setTabs(lastTabBar, fl, lastTabName, lastTabIndex);
            } else if (lastTabName.isEmpty() || lastTabName != next_arg) {
                lastTabName = next_arg;
                lastTabIndex++;
                lastTab = new QWidget();
                lastTabLayout = new QFormLayout();
                lastTab->setLayout(lastTabLayout);
                lastTabBar->addTab(lastTab, lastTabName);
            }
            lastVar = "";
        }
        
        // --col1
        else if (args.at(i) == "--col1") {
            lastColumn = "col1";
            colsHBoxLayout = new QHBoxLayout();
            lastVar = "";
        }
        
        // --col2
        else if (args.at(i) == "--col2") {
            lastColumn = "col2";
            lastVar = "";
        }
        
        /********************************************************************************
         * WIDGETS
         ********************************************************************************/
        
        // QCalendarWidget: --add-calendar
        else if (args.at(i) == "--add-calendar") {
            SWITCH_FORMS_WIDGET("calendar")
            QLabel *labelCalendar = new QLabel(NEXT_ARG);
            lastCalendar = new QCalendarWidget(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelCalendar, lastCalendar)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelCalendar, lastCalendar)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelCalendar, lastCalendar);
            } else {
                fl->addRow(labelCalendar, lastCalendar);
            }
        }
        
        // QCheckBox: --add-checkbox
        else if (args.at(i) == "--add-checkbox") {
            SWITCH_FORMS_WIDGET("checkbox")
            lastCheckbox = new QCheckBox(NEXT_ARG, dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastCheckbox)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastCheckbox)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(new QLabel(), lastCheckbox);
            } else {
                fl->addRow(lastCheckbox);
            }
        }
        
        // QLineEdit: --add-entry
        else if (args.at(i) == "--add-entry") {
            SWITCH_FORMS_WIDGET("entry")
            QLabel *labelEntry = new QLabel(NEXT_ARG);
            lastEntry = new QLineEdit(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelEntry, lastEntry)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelEntry, lastEntry)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelEntry, lastEntry);
            } else {
                fl->addRow(labelEntry, lastEntry);
            }
        }
        
        // QMenuBar: --add-menu
        else if (args.at(i) == "--add-menu") {
            if (lastWidget.isEmpty() && lastColumn.isEmpty()) {
                topMenu = true;
            }
            SWITCH_FORMS_WIDGET("menu")
            lastMenu = new QMenuBar();
            
            QString next_arg = NEXT_ARG;
            
            QIcon menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_TitleBarMaxButton);
            
            // Global menu settings
            
            QString menuSeparator = "";
            if (next_arg.contains(QRegExp("^sep=.//"))) {
                menuSeparator = next_arg.mid(4, 1);
                next_arg.remove(0, 7);
            }
            
            // Menu items
            
            QStringList menuItemsMainLevel = next_arg.split('|');
            QStringList menuItemChildren;
            
            QStringList menuItemData;
            QString menuItemName;
            int menuItemExitCode;
            QString menuItemCommand;
            bool menuItemCommandPrintOutput;
            QString menuItemIcon;
            
            for (int i = 0; i < menuItemsMainLevel.size(); ++i) {
                menuItemName = "";
                menuItemExitCode = -1;
                menuItemCommand = "";
                menuItemCommandPrintOutput = false;
                menuItemIcon = "";
                
                if (menuItemsMainLevel.at(i).contains('#')) {
                    menuItemData = menuItemsMainLevel.at(i).section('#', 0, 0).split(';');
                    menuItemChildren << menuItemsMainLevel.at(i).section('#', 1, -1).split('#');
                } else {
                    menuItemData = menuItemsMainLevel.at(i).split(';');
                }
                
                SET_FORMS_MENU_ITEM_DATA
                
                if (menuItemName.isEmpty())
                    continue;
                
                if (!ok)
                    menuItemExitCode = -1;
                
                if (!menuSeparator.isEmpty() && i > 0) {
                    auto* menuSep = new QAction(menuSeparator, this);
                    menuSep->setDisabled(true);
                    lastMenu->addAction(menuSep);
                }
                
                // Menu with submenu items (first level elements are added as QMenu)
                if (!menuItemChildren.isEmpty()) {
                    QMenu *menuItem = new QMenu(menuItemName);
                    lastMenu->addMenu(menuItem);
                    
                    for (int j = 0; j < menuItemChildren.size(); ++j) {
                        menuItemName = "";
                        menuItemExitCode = -1;
                        menuItemCommand = "";
                        menuItemCommandPrintOutput = false;
                        menuItemIcon = "";
                        
                        menuItemData = menuItemChildren.at(j).split(';');
                        SET_FORMS_MENU_ITEM_DATA
                        
                        if (menuItemName.isEmpty())
                            continue;
                        
                        if (!ok)
                            menuItemExitCode = -1;
                        
                        QAction *menuAction = new QAction(menuItemName, this);
                        
                        if (menuItemIcon != "false")
                            menuAction->setIcon(menuActionIcon);
                        
                        menuAction->setProperty("guid_menu_item_name", menuItemName);
                        menuAction->setProperty("guid_menu_item_exit_code", menuItemExitCode);
                        menuAction->setProperty("guid_menu_item_command", menuItemCommand);
                        menuAction->setProperty("guid_menu_item_command_print_output", menuItemCommandPrintOutput);
                        
                        menuItem->addAction(menuAction);
                        
                        connect(menuAction, SIGNAL(triggered()), this, SLOT(afterMenuClick()), Qt::UniqueConnection);
                    }
                    
                    menuItemChildren.clear();
                }
                
                // Menu without submenu items (first level elements are added as QAction)
                else {
                    QAction *menuAction = new QAction(menuItemName, this);
                    menuAction->setProperty("guid_menu_item_name", menuItemName);
                    menuAction->setProperty("guid_menu_item_exit_code", menuItemExitCode);
                    menuAction->setProperty("guid_menu_item_command", menuItemCommand);
                    menuAction->setProperty("guid_menu_item_command_print_output", menuItemCommandPrintOutput);
                    
                    lastMenu->addAction(menuAction);
                    
                    connect(menuAction, SIGNAL(triggered()), this, SLOT(afterMenuClick()), Qt::UniqueConnection);
                }
            }
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastMenu)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastMenu)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastMenu);
            } else if (topMenu) {
                lastMenu->setStyleSheet("background: white; border-top: 1px solid #F0F0F0;");
                flTopMenu->addRow(lastMenu);
            } else {
                fl->addRow(lastMenu);
            }
        }
        
        // QLineEdit: --add-password
        else if (args.at(i) == "--add-password") {
            SWITCH_FORMS_WIDGET("password")
            QLabel *labelPassword = new QLabel(NEXT_ARG);
            lastPassword = new QLineEdit(dlg);
            lastPassword->setEchoMode(QLineEdit::Password);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelPassword, lastPassword)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelPassword, lastPassword)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelPassword, lastPassword);
            } else {
                fl->addRow(labelPassword, lastPassword);
            }
        }
        
        // QSpinBox: --add-spin-box
        else if (args.at(i) == "--add-spin-box") {
            SWITCH_FORMS_WIDGET("spin-box")
            QLabel *labelSpinBox = new QLabel(NEXT_ARG);
            lastSpinBox = new QSpinBox();
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelSpinBox, lastSpinBox)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelSpinBox, lastSpinBox)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelSpinBox, lastSpinBox);
            } else {
                fl->addRow(labelSpinBox, lastSpinBox);
            }
        }
        
        // QDoubleSpinBox: --add-double-spin-box
        else if (args.at(i) == "--add-double-spin-box") {
            SWITCH_FORMS_WIDGET("double-spin-box")
            QLabel *labelDoubleSpinBox = new QLabel(NEXT_ARG);
            lastDoubleSpinBox = new QDoubleSpinBox();
            
            QLocale dv_locale(QLocale::C);
            dv_locale.setNumberOptions(QLocale::RejectGroupSeparator);
            lastDoubleSpinBox->setLocale(dv_locale);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelDoubleSpinBox, lastDoubleSpinBox)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelDoubleSpinBox, lastDoubleSpinBox)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelDoubleSpinBox, lastDoubleSpinBox);
            } else {
                fl->addRow(labelDoubleSpinBox, lastDoubleSpinBox);
            }
        }
        
        // QLabel: --add-text
        else if (args.at(i) == "--add-text") {
            SWITCH_FORMS_WIDGET("text")
            lastText = new QLabel(NEXT_ARG);
            lastText->setContentsMargins(0, 3, 0, 0);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastText)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastText)
                colsHBoxLayout->setSpacing(0);
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastText);
            } else {
                fl->addRow(lastText);
            }
        }
        
        // QLabel: --add-image-text
        else if (args.at(i) == "--add-image-text") {
            SWITCH_FORMS_WIDGET("image-text")
            QString next_arg = NEXT_ARG;
            QStringList imageTextData = next_arg.split("//");
            QString labelImageText;
            if (imageTextData.count() == 2 && (imageTextData[0].startsWith(":/") || QFile::exists(imageTextData[0]))) {
                labelImageText += "<table><tr>";
                labelImageText += "<td><img src=\"" + imageTextData[0] + "\" /></td>";
                labelImageText += "<td style=\"padding-left: 5px; vertical-align: middle;\">" + imageTextData[1] + "</td>";
                labelImageText += "</tr></table>";
            } else {
                labelImageText = next_arg;
            }
            lastImageText = new QLabel(labelImageText);
            lastImageText->setContentsMargins(0, 3, 0, 0);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastImageText)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastImageText)
                colsHBoxLayout->setSpacing(0);
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastImageText);
            } else {
                fl->addRow(lastImageText);
            }
        }
        
        // QLabel: --add-hrule
        else if (args.at(i) == "--add-hrule") {
            SWITCH_FORMS_WIDGET("hrule")
            lastHRule = new QLabel();
            lastHRuleCss = QString("color: " + NEXT_ARG + ";");
            lastHRule->setContentsMargins(0, 0, 0, 0);
            lastHRule->setFrameShape(QFrame::HLine);
            lastHRule->setStyleSheet(lastHRuleCss);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastHRule)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastHRule)
                colsHBoxLayout->setSpacing(0);
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastHRule);
            } else {
                fl->addRow(lastHRule);
            }
        }
        
        // QLabel: --add-spacer
        else if (args.at(i) == "--add-spacer") {
            SWITCH_FORMS_WIDGET("spacer")
            lastSpacer = new QLabel();
            lastSpacerHeight = NEXT_ARG.toInt();
            lastSpacerHeight -= formsWidgetSpacing;
            if (lastSpacerHeight < 0)
                lastSpacerHeight = 0;
            lastSpacer->setFixedHeight(lastSpacerHeight);
            lastSpacer->setContentsMargins(0, 0, 0, 0);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastSpacer)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastSpacer)
                colsHBoxLayout->setSpacing(0);
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastSpacer);
            } else {
                fl->addRow(lastSpacer);
            }
        }
        
        // QTextEdit: --add-text-info
        else if (args.at(i) == "--add-text-info") {
            SWITCH_FORMS_WIDGET("text-info")
            
            QLabel *labelTextInfo = new QLabel(NEXT_ARG);
            
            lastTextInfo = new QTextEdit();
            lastTextInfo->setTextInteractionFlags(Qt::TextEditorInteraction);
            lastTextInfo->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            lastTextInfo->setProperty("guid_text_info_nsep", "");
            lastTextIsReadOnly = true;
            lastTextFormat = "guess";
            lastTextCurlPath = "";
            lastTextFilename = "";
            lastTextIsUrl = false;
            lastTextHeight = -1;
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelTextInfo, lastTextInfo)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelTextInfo, lastTextInfo)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelTextInfo, lastTextInfo);
            } else {
                fl->addRow(labelTextInfo, lastTextInfo);
            }
        }
        
        // QTextBrowser: --add-text-browser
        else if (args.at(i) == "--add-text-browser") {
            SWITCH_FORMS_WIDGET("text-browser")
            
            QLabel *labelTextBrowser = new QLabel(NEXT_ARG);
            
            lastTextBrowser = new QTextBrowser();
            lastTextBrowser->setOpenLinks(true);
            lastTextBrowser->setOpenExternalLinks(true);
            lastTextBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction);
            lastTextBrowser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            lastTextIsReadOnly = true;
            lastTextFormat = "html";
            lastTextCurlPath = "";
            lastTextFilename = "";
            lastTextIsUrl = false;
            lastTextHeight = -1;
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelTextBrowser, lastTextBrowser)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelTextBrowser, lastTextBrowser)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelTextBrowser, lastTextBrowser);
            } else {
                fl->addRow(labelTextBrowser, lastTextBrowser);
            }
        }
        
        // QComboBox: --add-combo
        else if (args.at(i) == "--add-combo") {
            SWITCH_FORMS_WIDGET("combo")
            QLabel *labelCombo = new QLabel(NEXT_ARG);
            lastCombo = new QComboBox(dlg);
            lastCombo->setProperty("guid_file_sep", "");
            lastCombo->setProperty("guid_file_path", "");
            lastCombo->setProperty("guid_monitor_file", false);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelCombo, lastCombo)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelCombo, lastCombo)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelCombo, lastCombo);
            } else {
                fl->addRow(labelCombo, lastCombo);
            }
            
            lastCombo->addItems(lastComboGList.val);
        }
        
        // QTreeWidget: --add-list
        else if (args.at(i) == "--add-list") {
            SWITCH_FORMS_WIDGET("list")
            
            int lastListAddNewRowButton = 0;
            QString textLabelList = NEXT_ARG;
            if (textLabelList.contains(QRegExp("^addNewRowButton=.//"))) {
                lastListAddNewRowButton = textLabelList.section("//", 0, 0).replace("addNewRowButton=", "").toInt();
                textLabelList = textLabelList.section("//", 1, -1);
            }
            QLabel *labelList = new QLabel(textLabelList);
            
            buildFormsList(&lastList, lastListGList, lastListColumns, lastListHeader, lastListFlags, lastListHeight);
            
            lastListContainer = new QWidget();
            lastListLayout = new QFormLayout();
            lastList = new QTreeWidget(dlg);
            lastListButton = new QPushButton();
            
            lastList->setProperty("guid_file_sep", "");
            lastList->setProperty("guid_file_path", "");
            lastList->setProperty("guid_monitor_file", false);
            lastList->setProperty("guid_list_add_value", "");
            lastList->setProperty("guid_list_print_values_mode", "selected");
            lastList->setProperty("guid_list_selection_type", "");
            lastList->setProperty("guid_list_print_column", "1");
            lastList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
            lastList->header()->setStretchLastSection(true);
            
            lastListLayout->addRow(lastList);
            lastListLayout->setSpacing(0);
            lastListLayout->setContentsMargins(0, 0, 0, 0);
            
            if (lastListAddNewRowButton == 1) {
                lastListButton = new QPushButton(tr("Add row"), lastList);
                connect(lastListButton, SIGNAL(clicked()), this, SLOT(addListRow()), Qt::UniqueConnection);
                
                lastListLayout->addRow(lastListButton);
                lastListLayout->setAlignment(lastListButton, Qt::AlignLeft);
            }
            
            lastListContainer->setLayout(lastListLayout);
            lastListContainer->setProperty("guid_list_container", true);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelList, lastListContainer)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelList, lastListContainer)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(labelList, lastListContainer);
            } else {
                fl->addRow(labelList, lastListContainer);
            }
        }
        
        // QSlider: --add-scale
        else if (args.at(i) == "--add-scale") {
            SWITCH_FORMS_WIDGET("scale")
            
            lastScaleLabel = new QLabel(NEXT_ARG);
            lastScale = new QSlider(Qt::Horizontal, dlg);
            lastScale->setRange(0, 100);
            lastScaleVal = new QLabel(dlg);
            lastScaleVal->setNum(0);
            connect(lastScale, SIGNAL(valueChanged(int)), lastScaleVal, SLOT(setNum(int)));
            
            scaleHBoxLayout = new QHBoxLayout();
            scaleHBoxLayout->setContentsMargins(0, 0, 0, 0);
            scaleHBoxLayout->addWidget(lastScale);
            scaleHBoxLayout->addWidget(lastScaleVal);
            
            scaleContainer = new QWidget();
            scaleContainer->setProperty("guid_scale_container", true);
            scaleContainer->setLayout(scaleHBoxLayout);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(lastScaleLabel, scaleContainer)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(lastScaleLabel, scaleContainer)
            } else if (!lastTabName.isEmpty()) {
                lastTabLayout->addRow(lastScaleLabel, scaleContainer);
            } else {
                fl->addRow(lastScaleLabel, scaleContainer);
            }
        }
        
        // labelText: --text
        else if (args.at(i) == "--text") {
            SWITCH_FORMS_WIDGET("label")
            topMenu = false;
            
            label->setText(labelText(NEXT_ARG));
            label->setVisible(true);
        }
        
        /********************************************************************************
         * CONTAINER SETTINGS
         ********************************************************************************/
        
        /******************************
         * tab
         ******************************/
        
        // --tab-visible
        else if (args.at(i) == "--tab-visible") {
            if (!lastTabName.isEmpty())
                lastTabBar->setCurrentIndex(lastTabIndex);
            else
                WARN_UNKNOWN_ARG("--tab");
        }
        
        /********************************************************************************
         * WIDGET SETTINGS
         ********************************************************************************/
        
        /******************************
         * calendar || checkbox || entry || password || spin-box || double-spin-box || scale || combo || list || text-info
         ******************************/
        
        // --var
        else if (args.at(i) == "--var") {
            if (lastWidget == "calendar" || lastWidget == "checkbox" || lastWidget == "entry" ||
                lastWidget == "password" || lastWidget == "spin-box" || lastWidget == "double-spin-box" ||
                lastWidget == "scale" || lastWidget == "combo" || lastWidget == "list" || lastWidget == "text-info") {
                lastVar = NEXT_ARG;
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * entry
         ******************************/
        
        // --int
        else if (args.at(i) == "--int") {
            if (lastWidget == "entry") {
                lastEntry->setValidator(new QIntValidator(INT_MIN, INT_MAX, this));
                lastEntry->setText(QString::number(NEXT_ARG.toInt()));
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        // --float
        else if (args.at(i) == "--float") {
            if (lastWidget == "entry") {
                QLocale dv_locale(QLocale::C);
                dv_locale.setNumberOptions(QLocale::RejectGroupSeparator);
                
                QDoubleValidator *dv = new QDoubleValidator(DBL_MIN, DBL_MAX, 2, this);
                dv->setNotation(QDoubleValidator::StandardNotation);
                dv->setLocale(dv_locale);
                
                lastEntry->setValidator(dv);
                lastEntry->setText(QString::number(NEXT_ARG.toDouble(), 'f', 2));
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * entry || text || image-text || hrule || password || spin-box || double-spin-box || combo || text-browser || text-info
         ******************************/
        
        // --field-width
        else if (args.at(i) == "--field-width") {
            if (lastWidget == "entry") {
                lastEntry->setMaximumWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "text") {
                lastText->setMaximumWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "image-text") {
                lastImageText->setMaximumWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "hrule") {
                lastHRule->setMaximumWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "password") {
                lastPassword->setMaximumWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "spin-box") {
                lastSpinBox->setFixedWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "double-spin-box") {
                lastDoubleSpinBox->setFixedWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "combo") {
                lastCombo->setFixedWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "list") {
                lastList->setFixedWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "text-browser") {
                lastTextBrowser->setFixedWidth(NEXT_ARG.toInt());
            } else if (lastWidget == "text-info") {
                lastTextInfo->setFixedWidth(NEXT_ARG.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * spin-box || double-spin-box
         ******************************/
        
        // --prefix
        else if (args.at(i) == "--prefix") {
            if (lastWidget == "spin-box")
                lastSpinBox->setPrefix(NEXT_ARG);
            else if (lastWidget == "double-spin-box")
                lastDoubleSpinBox->setPrefix(NEXT_ARG);
            else
                WARN_UNKNOWN_ARG("--add-spin-box");
        }
        
        // --suffix
        else if (args.at(i) == "--suffix") {
            if (lastWidget == "spin-box")
                lastSpinBox->setSuffix(NEXT_ARG);
            else if (lastWidget == "double-spin-box")
                lastDoubleSpinBox->setSuffix(NEXT_ARG);
            else
                WARN_UNKNOWN_ARG("--add-spin-box");
        }
        
        /******************************
         * double-spin-box
         ******************************/
        
        // --decimals
        else if (args.at(i) == "--decimals") {
            if (lastWidget == "double-spin-box")
                lastDoubleSpinBox->setDecimals(NEXT_ARG.toInt());
            else
                WARN_UNKNOWN_ARG("--add-double-spin-box");
        }
        
        /******************************
         * spin-box || double-spin-box || scale
         ******************************/
        
        // --value
        else if (args.at(i) == "--value") {
            if (lastWidget == "spin-box") {
                lastSpinBox->setValue(NEXT_ARG.toInt());
            } else if (lastWidget == "double-spin-box") {
                lastDoubleSpinBox->setValue(NEXT_ARG.toDouble());
            } else if (lastWidget == "scale") {
                lastScale->setValue(NEXT_ARG.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        // --min-value
        else if (args.at(i) == "--min-value") {
            if (lastWidget == "spin-box") {
                lastSpinBox->setMinimum(NEXT_ARG.toInt());
            } else if (lastWidget == "double-spin-box") {
                lastDoubleSpinBox->setMinimum(NEXT_ARG.toDouble());
            } else if (lastWidget == "scale") {
                lastScale->setMinimum(NEXT_ARG.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        // --max-value
        else if (args.at(i) == "--max-value") {
            if (lastWidget == "spin-box") {
                lastSpinBox->setMaximum(NEXT_ARG.toInt());
            } else if (lastWidget == "double-spin-box") {
                lastDoubleSpinBox->setMaximum(NEXT_ARG.toDouble());
            } else if (lastWidget == "scale") {
                lastScale->setMaximum(NEXT_ARG.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        /******************************
         * scale
         ******************************/
        
        // --step
        else if (args.at(i) == "--step") {
            if (lastWidget == "scale")
                lastScale->setSingleStep(NEXT_ARG.toInt());
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        // --print-partial
        else if (args.at(i) == "--print-partial") {
            if (lastWidget == "scale")
                connect(lastScale, SIGNAL(valueChanged(int)), SLOT(printInteger(int)));
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        // --hide-value
        else if (args.at(i) == "--hide-value") {
            if (lastWidget == "scale")
                lastScaleVal->hide();
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        /******************************
         * combo
         ******************************/
        
        // --combo-values
        else if (args.at(i) == "--combo-values") {
            if (lastWidget == "combo") {
                lastComboGList.val = NEXT_ARG.split('|');
                lastCombo->addItems(lastComboGList.val);
            } else {
                WARN_UNKNOWN_ARG("--add-combo");
            }
        }
        
        // --combo-values-from-file
        else if (args.at(i) == "--combo-values-from-file") {
            if (lastWidget == "combo") {
                lastComboGList = listValuesFromFile(NEXT_ARG);
                
                lastCombo->setProperty("guid_file_sep", lastComboGList.fileSep);
                lastCombo->setProperty("guid_file_path", lastComboGList.filePath);
                lastCombo->setProperty("guid_monitor_file", lastComboGList.monitorFile);
                
                if (QFile::exists(lastComboGList.filePath)) {
                    comboWatcher->addPath(lastComboGList.filePath);
                    connect(comboWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateCombo(QString)), Qt::UniqueConnection);
                }
                
                lastCombo->addItems(lastComboGList.val);
            } else {
                WARN_UNKNOWN_ARG("--add-combo");
            }
        }
        
        /******************************
         * combo || list || text-info
         ******************************/
        
        // --editable
        else if (args.at(i) == "--editable") {
            if (lastWidget == "list")
                lastListFlags |= Qt::ItemIsEditable;
            else if (lastWidget == "combo")
                lastCombo->setEditable(true);
            else if (lastWidget == "text-info")
                lastTextIsReadOnly = false;
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        /******************************
         * list || text-browser || text-info
         ******************************/
        
        // --field-height
        else if (args.at(i) == "--field-height") {
            if (lastWidget == "list") {
                lastListHeight = NEXT_ARG.toInt(&ok);
                if (!ok || lastListHeight < 0)
                    lastListHeight = -1;
            } else if (lastWidget == "text-browser" || lastWidget == "text-info") {
                lastTextHeight = NEXT_ARG.toInt(&ok);
                if (!ok || lastTextHeight < 0)
                    lastTextHeight = -1;
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        /******************************
         * list
         ******************************/
        
        // --column-values
        else if (args.at(i) == "--column-values") {
            if (lastWidget == "list")
                lastListColumns = NEXT_ARG.split('|');
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --print-column
        else if (args.at(i) == "--print-column") {
            if (lastWidget == "list")
                lastList->setProperty("guid_list_print_column", NEXT_ARG.toLower());
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --list-values
        else if (args.at(i) == "--list-values") {
            if (lastWidget == "list")
                lastListGList.val = NEXT_ARG.split('|');
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --list-values-from-file
        else if (args.at(i) == "--list-values-from-file") {
            if (lastWidget == "list") {
                lastListGList = listValuesFromFile(NEXT_ARG);
                
                lastList->setProperty("guid_list_add_value", lastListGList.addValue);
                lastList->setProperty("guid_file_sep", lastListGList.fileSep);
                lastList->setProperty("guid_file_path", lastListGList.filePath);
                lastList->setProperty("guid_monitor_file", lastListGList.monitorFile);
                
                if (QFile::exists(lastListGList.filePath)) {
                    listWatcher->addPath(lastListGList.filePath);
                    connect(listWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateList(QString)), Qt::UniqueConnection);
                }
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        // --print-values
        else if (args.at(i) == "--print-values") {
            if (lastWidget == "list")
                lastList->setProperty("guid_list_print_values_mode", NEXT_ARG.toLower());
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --checklist
        else if (args.at(i) == "--checklist") {
            if (lastWidget == "list")
                lastList->setProperty("guid_list_selection_type", "checklist");
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --radiolist
        else if (args.at(i) == "--radiolist") {
            if (lastWidget == "list")
                lastList->setProperty("guid_list_selection_type", "radiolist");
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --multiple
        else if (args.at(i) == "--multiple") {
            if (lastWidget == "list")
                lastList->setSelectionMode(QAbstractItemView::ExtendedSelection);
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --no-selection
        else if (args.at(i) == "--no-selection") {
            if (lastWidget == "list") {
                lastList->setSelectionMode(QAbstractItemView::NoSelection);
                lastList->setFocusPolicy(Qt::NoFocus);
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        // --show-header
        else if (args.at(i) == "--show-header") {
            if (lastWidget == "list")
                lastListHeader = true;
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        /******************************
         * label
         ******************************/
        
        // --no-bold
        else if (args.at(i) == "--no-bold") {
            labelInBold = false;
        }
        
        /******************************
         * text
         ******************************/
        
        // --tooltip
        else if (args.at(i) == "--tooltip") {
            lastText->setToolTip(NEXT_ARG);
        }
        
        /******************************
         * label || text || image-text || text-info
         ******************************/
        
        // --align || --bold || --italics || --underline || --small-caps || --font-family || --font-size ||
        // --foreground-color || --background-color
        else if (args.at(i) == "--align" || args.at(i) == "--bold" || args.at(i) == "--italics" ||
                 args.at(i) == "--underline" || args.at(i) == "--small-caps" || args.at(i) == "--font-family" ||
                 args.at(i) == "--font-size" || args.at(i) == "--foreground-color" || args.at(i) == "--background-color") {
            QLabel *labelToSet = NULL;
            if (lastWidget == "label") {
                labelToSet = label;
            } else if (lastWidget == "text") {
                labelToSet = lastText;
            } else if (lastWidget == "image-text") {
                labelToSet = lastImageText;
            }
            
            // labelToSet
            if (labelToSet) {
                QFont fontToSet = labelToSet->font();
                
                if (args.at(i) == "--align") {
                    QString alignment = NEXT_ARG;
                    if (alignment == "left") {
                        labelToSet->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        labelToSet->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        labelToSet->setAlignment(Qt::AlignRight);
                    } else {
                        qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i);
                    }
                } else if (args.at(i) == "--bold") {
                    fontToSet.setBold(true);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--italics") {
                    fontToSet.setItalic(true);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--underline") {
                    fontToSet.setUnderline(true);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--small-caps") {
                    fontToSet.setCapitalization(QFont::SmallCaps);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--font-family") {
                    fontToSet.setFamily(NEXT_ARG);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--font-size") {
                    int fontSize = NEXT_ARG.toInt(&ok);
                    if (ok) {
                        fontToSet.setPointSize(fontSize);
                        labelToSet->setFont(fontToSet);
                    }
                } else if (args.at(i) == "--foreground-color") {
                    QString foregroundColor = NEXT_ARG;
                    if (foregroundColor.left(1) != "#") {
                        foregroundColor = "#" + foregroundColor;
                    }
                    
                    QColor *color = new QColor(0, 0, 0);
                    color->setNamedColor(foregroundColor);
                    
                    QPalette labelPalette = labelToSet->palette();
                    labelPalette.setColor(QPalette::WindowText, *color);
                    
                    labelToSet->setPalette(labelPalette);
                } else if (args.at(i) == "--background-color") {
                    QString backgroundColor = NEXT_ARG;
                    if (backgroundColor.left(1) != "#") {
                        backgroundColor = "#" + backgroundColor;
                    }
                    
                    QColor *color = new QColor(0, 0, 0);
                    color->setNamedColor(backgroundColor);
                    
                    QPalette labelPalette = labelToSet->palette();
                    labelPalette.setColor(QPalette::Window, *color);
                    
                    labelToSet->setAutoFillBackground(true);
                    labelToSet->setPalette(labelPalette);
                }
            }
            
            // text-info
            else if (lastWidget == "text-info") {
                if (args.at(i) == "--align") {
                    QString alignment = NEXT_ARG;
                    if (alignment == "left") {
                        lastTextInfo->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        lastTextInfo->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        lastTextInfo->setAlignment(Qt::AlignRight);
                    } else {
                        qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i);
                    }
                } else if (args.at(i) == "--bold") {
                    lastTextInfo->setFontWeight(QFont::Bold);
                } else if (args.at(i) == "--italics") {
                    lastTextInfo->setFontItalic(true);
                } else if (args.at(i) == "--underline") {
                    lastTextInfo->setFontUnderline(true);
                } else if (args.at(i) == "--font-family") {
                    lastTextInfo->setFontFamily(NEXT_ARG);
                } else if (args.at(i) == "--font-size") {
                    int fontSize = NEXT_ARG.toDouble(&ok);
                    if (ok) {
                        lastTextInfo->setFontPointSize(fontSize);
                    }
                } else if (args.at(i) == "--foreground-color") {
                    QString foregroundColor = NEXT_ARG;
                    if (foregroundColor.left(1) != "#") {
                        foregroundColor = "#" + foregroundColor;
                    }
                    
                    QColor color = QColor(0, 0, 0);
                    color.setNamedColor(foregroundColor);
                    lastTextInfo->setTextColor(color);
                } else if (args.at(i) == "--background-color") {
                    QString backgroundColor = NEXT_ARG;
                    if (backgroundColor.left(1) != "#") {
                        backgroundColor = "#" + backgroundColor;
                    }
                    QColor color = QColor(0, 0, 0);
                    color.setNamedColor(backgroundColor);
                    lastTextInfo->setTextBackgroundColor(color);
                }
            }
            
            // else
            else {
                WARN_UNKNOWN_ARG("--add-text");
            }
        }
        
        /******************************
         * text-info
         ******************************/
        
        // --font
        else if (args.at(i) == "--font") {
            if (lastWidget == "text-info") {
                lastTextInfo->setFont(QFont(NEXT_ARG));
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --html
        else if (args.at(i) == "--html") {
            if (lastWidget == "text-info") {
                lastTextFormat = "html";
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --plain
        else if (args.at(i) == "--plain") {
            if (lastWidget == "text-info") {
                lastTextFormat = "plain";
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --newline-separator
        else if (args.at(i) == "--newline-separator") {
            if (lastWidget == "text-info") {
                lastTextInfo->setProperty("guid_text_info_nsep", NEXT_ARG);
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        /******************************
         * text-browser || text-info
         ******************************/
        
        // --filename
        else if (args.at(i) == "--filename") {
            if (lastWidget == "text-browser" || lastWidget == "text-info")
                lastTextFilename = NEXT_ARG;
            else
                WARN_UNKNOWN_ARG("--text-info");
        }
        
        // --url
        else if (args.at(i) == "--url") {
            if (lastWidget == "text-browser" || lastWidget == "text-info") {
                lastTextFilename = NEXT_ARG;
                lastTextIsUrl = true;
            } else {
                WARN_UNKNOWN_ARG("--text-info");
            }
        }
        
        // --curl-path
        else if (args.at(i) == "--curl-path") {
            if (lastWidget == "text-browser" || lastWidget == "text-info")
                lastTextCurlPath = NEXT_ARG;
            else
                WARN_UNKNOWN_ARG("--text-info");
        }
        
        /********************************************************************************
         * DIALOG SETTINGS
         ********************************************************************************/
        
        // --win-min-button
        else if (args.at(i) == "--win-min-button") {
            dlgFlags = dlg->windowFlags();
            dlgFlags |= Qt::WindowMinimizeButtonHint;
            dlg->setWindowFlags(dlgFlags);
        }
        
        // --win-max-button
        else if (args.at(i) == "--win-max-button") {
            dlgFlags = dlg->windowFlags();
            dlgFlags |= Qt::WindowMaximizeButtonHint;
            dlg->setWindowFlags(dlgFlags);
        }
        
        // --close-to-systray
        else if (args.at(i) == "--close-to-systray") {
            m_closeToSysTray = true;
        }
        
        // --systray-icon
        else if (args.at(i) == "--systray-icon") {
            sysTrayIconPath = NEXT_ARG;
        }
        
        // --forms-date-format
        else if (args.at(i) == "--forms-date-format") {
            dlg->setProperty("guid_date_format", NEXT_ARG);
        }
        
        // --forms-align
        else if (args.at(i) == "--forms-align") {
            QString alignment = NEXT_ARG;
            if (alignment == "left") {
                fl->setLabelAlignment(Qt::AlignLeft);
            } else if (alignment == "center") {
                fl->setLabelAlignment(Qt::AlignCenter);
            } else if (alignment == "right") {
                fl->setLabelAlignment(Qt::AlignRight);
            } else {
                qOutErr << m_prefixErr + "argument --forms-align: unknown value" << args.at(i);
            }
        }
        
        // --separator
        else if (args.at(i) == "--separator") {
            dlg->setProperty("guid_separator", NEXT_ARG);
        }
        
        // --list-row-separator
        else if (args.at(i) == "--list-row-separator") {
            dlg->setProperty("guid_list_row_separator", NEXT_ARG);
        }
        
        // else
        else {
            WARN_UNKNOWN_ARG("--forms");
        }
        
        lastComboGList = GList();
        if (columnsAreSet) {
            labelCol1 = new QLabel();
            lastColumn = "";
            columnsAreSet = false;
        }
    }
    
    SWITCH_FORMS_WIDGET(lastWidget)
    if (!lastTabName.isEmpty())
        setTabs(lastTabBar, fl, lastTabName, lastTabIndex);
    buildFormsList(&lastList, lastListGList, lastListColumns, lastListHeader, lastListFlags, lastListHeight);
    
    if (labelInBold) {
        QFont mainLabelFont = label->font();
        mainLabelFont.setBold(true);
        label->setFont(mainLabelFont);
    }
    
    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    
    if (topMenu) {
        fl->setContentsMargins(10, 0, 10, 10);
        vl->setContentsMargins(0, 0, 0, 10);
        btns->setContentsMargins(10, 0, 10, 0);
    }
    
    if (!sysTrayIconPath.isEmpty() && QSystemTrayIcon::isSystemTrayAvailable()) {
        // System tray icon
        QSystemTrayIcon *sysTrayIcon = new QSystemTrayIcon(dlg);
        m_sysTray = sysTrayIcon;
        QIcon iconToSet(sysTrayIconPath);
        sysTrayIcon->setIcon(iconToSet);
        sysTrayIcon->setVisible(true);
        
        // Menu
        
        QMenu *sysTrayMenu = new QMenu();
        sysTrayIcon->setContextMenu(sysTrayMenu);
        QIcon menuActionIcon;
        
        QAction *actionSysTrayMinimize = new QAction(tr("Minimize"), sysTrayIcon);
        actionSysTrayMinimize->setProperty("guid_systray_menu_action", "Minimize");
        menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_TitleBarMinButton);
        actionSysTrayMinimize->setIcon(menuActionIcon);
        connect(actionSysTrayMinimize, SIGNAL(triggered()), this, SLOT(minimizeDialog()));
        sysTrayMenu->addAction(actionSysTrayMinimize);
        
        QAction *actionSysTrayShow = new QAction(tr("Show"), sysTrayIcon);
        actionSysTrayShow->setProperty("guid_systray_menu_action", "Show");
        menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_TitleBarMaxButton);
        actionSysTrayShow->setIcon(menuActionIcon);
        connect(actionSysTrayShow, SIGNAL(triggered()), this, SLOT(showDialog()));
        sysTrayMenu->addAction(actionSysTrayShow);
        
        sysTrayMenu->addSeparator();
        
        QAction *actionSysTrayQuit = new QAction(tr("Quit"), sysTrayIcon);
        actionSysTrayQuit->setProperty("guid_systray_menu_action", "Quit");
        menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton);
        actionSysTrayQuit->setIcon(menuActionIcon);
        //connect(actionSysTrayQuit, SIGNAL(triggered()), qApp, SLOT(quit()));
        connect(actionSysTrayQuit, SIGNAL(triggered()), this, SLOT(quitDialog()));
        sysTrayMenu->addAction(actionSysTrayQuit);
        
        connect(sysTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                this, SLOT(showSysTrayMenu(QSystemTrayIcon::ActivationReason)));
        
        sysTrayIcon->show();
    }
    
    SHOW_DIALOG
    
    return 0;
}

char Guid::showList(const QStringList &args)
{
    QOUT_ERR
    NEW_DIALOG

    QLabel *lbl;
    vl->addWidget(lbl = new QLabel(dlg));

    QTreeWidget *tw;
    vl->addWidget(tw = new QTreeWidget(dlg));
    tw->setSelectionBehavior(QAbstractItemView::SelectRows);
    tw->setSelectionMode(QAbstractItemView::SingleSelection);
    tw->setRootIsDecorated(false);
    tw->setAllColumnsShowFocus(true);
    tw->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    tw->header()->setStretchLastSection(true);
    tw->setProperty("guid_list_print_column", "1");
    tw->setProperty("guid_list_add_value", "");
    
    bool editable(false), exclusive(false), checkable(false), icons(false), ok, needFilter(true);
    QString selectionType;
    int heightToSet = -1;
    QStringList columns;
    GList list = GList();
    QList<int> hiddenCols;
    dlg->setProperty("guid_separator", "|");
    QFileSystemWatcher * listWatcher = new QFileSystemWatcher(dlg);
    
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            lbl->setText(labelText(NEXT_ARG));
        else if (args.at(i) == "--align") {
            if (lbl) {
                QString alignment = NEXT_ARG;
                if (alignment == "left")
                    lbl->setAlignment(Qt::AlignLeft);
                else if (alignment == "center")
                    lbl->setAlignment(Qt::AlignCenter);
                else if (alignment == "right")
                    lbl->setAlignment(Qt::AlignRight);
                else
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i);
            } else
                WARN_UNKNOWN_ARG("--text");
        } else if (args.at(i) == "--multiple") {
            tw->setSelectionMode(QAbstractItemView::ExtendedSelection);
        } else if (args.at(i) == "--no-selection") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setFocusPolicy(Qt::NoFocus);
    } else if (args.at(i) == "--column") {
            columns << NEXT_ARG;
        } else if (args.at(i) == "--editable")
            editable = true;
        else if (args.at(i) == "--hide-header")
            tw->setHeaderHidden(true);
        else if (args.at(i) == "--separator")
            dlg->setProperty("guid_separator", NEXT_ARG);
        else if (args.at(i) == "--hide-column") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                hiddenCols << v-1;
        } else if (args.at(i) == "--print-column") {
            tw->setProperty("guid_list_print_column", NEXT_ARG.toLower());
        } else if (args.at(i) == "--checklist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
            selectionType = "checklist";
            checkable = true;
        } else if (args.at(i) == "--radiolist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
            selectionType = "radiolist";
            checkable = true;
            exclusive = true;
        } else if (args.at(i) == "--imagelist") {
            icons = true;
        } else if (args.at(i) == "--mid-search") {
            if (needFilter) {
                needFilter = false;
                QLineEdit *filter;
                vl->addWidget(filter = new QLineEdit(dlg));
                filter->setPlaceholderText(tr("Filter"));
                connect (filter, &QLineEdit::textChanged, this, [=](const QString &match){
                    for (int i = 0; i < tw->topLevelItemCount(); ++i)
                        tw->topLevelItem(i)->setHidden(!tw->topLevelItem(i)->text(0).contains(match, Qt::CaseInsensitive));
                });
            }
        } else if (args.at(i) == "--field-height") {
            heightToSet = NEXT_ARG.toInt(&ok);
            if (!ok)
                heightToSet = -1;
        } else if (args.at(i) == "--list-values-from-file") {
            list = listValuesFromFile(NEXT_ARG);
            
            tw->setProperty("guid_list_add_value", list.addValue);
            tw->setProperty("guid_file_sep", list.fileSep);
            tw->setProperty("guid_file_path", list.filePath);
            tw->setProperty("guid_monitor_file", list.monitorFile);
            
            if (QFile::exists(list.filePath)) {
                listWatcher->addPath(list.filePath);
                connect(listWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateList(QString)), Qt::UniqueConnection);
            }
        } else if (args.at(i) == "--print-values") {
            tw->setProperty("guid_list_print_values_mode", NEXT_ARG.toLower());
        } else if (args.at(i) != "--list") {
            list.val << args.at(i);
        }
    }
    if (list.val.isEmpty())
        listenToStdIn();

    tw->setProperty("guid_list_selection_type", selectionType);
    tw->setProperty("guid_list_flags", int(editable | checkable << 1 | icons << 2));

    int columnCount = qMax(columns.count(), 1);
    tw->setColumnCount(columnCount);
    tw->setHeaderLabels(columns);
    tw->setStyleSheet(QTREEWIDGET_STYLE);
    foreach (const int &i, hiddenCols)
        tw->setColumnHidden(i, true);

    list.val = addColumnToListValues(list.val, list.addValue, columnCount);
    addItems(tw, list.val, editable, checkable, icons);

    if (exclusive) {
        connect (tw, SIGNAL(itemChanged(QTreeWidgetItem*, int)), SLOT(toggleItems(QTreeWidgetItem*, int)));
    }
    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    if (!selectionType.isEmpty())
        tw->header()->setSectionResizeMode(0, QHeaderView::Fixed);

    if (heightToSet >= 0 && heightToSet < getQTreeWidgetSize(&tw).height())
        tw->setMaximumHeight(heightToSet);

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

char Guid::showMessage(const QStringList &args, char type)
{
    QOUT_ERR
    
    QMessageBox *dlg = new QMessageBox;
    dlg->setStandardButtons((type == 'q') ? QMessageBox::Yes|QMessageBox::No : QMessageBox::Ok);
    dlg->setDefaultButton(QMessageBox::Ok);

    bool wrap = true, html = true;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setText(html ? labelText(NEXT_ARG) : NEXT_ARG);
        else if (args.at(i) == "--icon-name")
            dlg->setIconPixmap(QIcon(NEXT_ARG).pixmap(64));
        else if (args.at(i) == "--no-wrap")
            wrap = false;
        else if (args.at(i) == "--ellipsize")
            wrap = true;
        else if (args.at(i) == "--no-markup")
            html = false;
        else if (args.at(i) == "--default-cancel")
            dlg->setDefaultButton(QMessageBox::Cancel);
        else if (args.at(i) == "--selectable-labels")
            m_selectableLabel = true;
        else if (args.at(i).startsWith("--") && args.at(i) != "--info" && args.at(i) != "--question" &&
                                                args.at(i) != "--warning" && args.at(i) != "--error")
            qOutErr << m_prefixErr + "unspecific argument" << args.at(i);
    }
    if (QLabel *l = dlg->findChild<QLabel*>("qt_msgbox_label")) {
        l->setWordWrap(wrap);
        l->setTextFormat(html ? Qt::RichText : Qt::PlainText);
        if (m_selectableLabel)
            l->setTextInteractionFlags(l->textInteractionFlags()|Qt::TextSelectableByMouse);
    }
    if (dlg->iconPixmap().isNull())
        dlg->setIcon(type == 'w' ? QMessageBox::Warning :
                   (type == 'q' ? QMessageBox::Question :
                   (type == 'e' ? QMessageBox::Critical : QMessageBox::Information)));
    SHOW_DIALOG
    return 0;
}

char Guid::showNotification(const QStringList &args)
{
    QString message;
    bool listening(false);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text") {
            message = NEXT_ARG;
        } else if (args.at(i) == "--listen") {
            listening = true;
            listenToStdIn();
        } else if (args.at(i) == "--hint") {
            m_notificationHints = NEXT_ARG;
        } else if (args.at(i) == "--selectable-labels") {
            m_selectableLabel = true;
        } else { WARN_UNKNOWN_ARG("--notification") }
    }
    if (!message.isEmpty())
        notify(message, listening);
    if (!(listening || m_dialog))
        QMetaObject::invokeMethod(this, "exitGuid", Qt::QueuedConnection);
    return 0;
}

char Guid::showPassword(const QStringList &args)
{
    NEW_DIALOG

    QLineEdit *username(NULL), *password(NULL);
    QString prompt = tr("Enter password");
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--username") {
            vl->addWidget(new QLabel(tr("Enter username"), dlg));
            vl->addWidget(username = new QLineEdit(dlg));
            username->setObjectName("guid_username");
            break;
        } else if (args.at(i) == "--prompt") {
            prompt = NEXT_ARG;
        } { WARN_UNKNOWN_ARG("--password") }
    }

    vl->addWidget(new QLabel(prompt, dlg));
    vl->addWidget(password = new QLineEdit(dlg));
    password->setObjectName("guid_password");
    password->setEchoMode(QLineEdit::Password);

    InputGuard::watch(password);

    if (username)
        username->setFocus(Qt::OtherFocusReason);
    else
        password->setFocus(Qt::OtherFocusReason);

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

char Guid::showProgress(const QStringList &args)
{
    QProgressDialog *dlg = new QProgressDialog;
    dlg->setRange(0, 101);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setLabelText(labelText(NEXT_ARG));
        else if (args.at(i) == "--percentage")
            dlg->setValue(NEXT_ARG.toUInt());
        else if (args.at(i) == "--pulsate")
            dlg->setRange(0,0);
        else if (args.at(i) == "--auto-close")
            dlg->setProperty("guid_autoclose", true);
        else if (args.at(i) == "--auto-kill")
            dlg->setProperty("guid_autokill_parent", true);
        else if (args.at(i) == "--no-cancel") {
            if (QPushButton *btn = dlg->findChild<QPushButton*>())
                btn->hide();
        } else if (args.at(i) == "--time-remaining") {
            dlg->setProperty("guid_eta", true);
        }
        else { WARN_UNKNOWN_ARG("--progress") }
    }

    listenToStdIn();
    if (dlg->maximum() == 0) { // pulsate, quit as stdin closes
        connect (gs_stdin, SIGNAL(aboutToClose()), this, SLOT(finishProgress()));
    }

    if (!m_cancel.isNull())
        dlg->setCancelButtonText(m_cancel);
    connect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
    SHOW_DIALOG
    return 0;
}

char Guid::showScale(const QStringList &args)
{
    QOUT_ERR
    NEW_DIALOG

    QHBoxLayout *hl = new QHBoxLayout;
    QLabel *lbl, *val;
    QSlider *sld;

    vl->addWidget(lbl = new QLabel("Enter a value", dlg));
    vl->addLayout(hl);
    hl->addWidget(sld = new QSlider(Qt::Horizontal, dlg));
    hl->addWidget(val = new QLabel(dlg));
    connect (sld, SIGNAL(valueChanged(int)), val, SLOT(setNum(int)));

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);

    sld->setRange(0,100);
    val->setNum(0);

    bool ok;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            lbl->setText(labelText(NEXT_ARG));
        else if (args.at(i) == "--align") {
            if (lbl) {
                QString alignment = NEXT_ARG;
                if (alignment == "left")
                    lbl->setAlignment(Qt::AlignLeft);
                else if (alignment == "center")
                    lbl->setAlignment(Qt::AlignCenter);
                else if (alignment == "right")
                    lbl->setAlignment(Qt::AlignRight);
                else
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i);
            } else
                WARN_UNKNOWN_ARG("--text");
        } else if (args.at(i) == "--value")
            sld->setValue(NEXT_ARG.toInt());
        else if (args.at(i) == "--min-value") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setMinimum(v);
        } else if (args.at(i) == "--max-value") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setMaximum(v);
        } else if (args.at(i) == "--step") {
            int u = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setSingleStep(u);
        } else if (args.at(i) == "--print-partial") {
            connect (sld, SIGNAL(valueChanged(int)), SLOT(printInteger(int)));
        } else if (args.at(i) == "--hide-value") {
            val->hide();
        } else { WARN_UNKNOWN_ARG("--scale") }
    }
    SHOW_DIALOG
    return 0;
}

char Guid::showText(const QStringList &args)
{
    NEW_DIALOG

    QTextBrowser *te;
    vl->addWidget(te = new QTextBrowser(dlg));
    te->setReadOnly(true);
    te->setOpenExternalLinks(true);
    QCheckBox *cb(NULL);

    QString filename;
    QString curlPath;
    bool html(false), plain(false), onlyMarkup(false), url(false);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--filename") {
            filename = NEXT_ARG;
        } else if (args.at(i) == "--url") {
            filename = NEXT_ARG;
            url = true;
        } else if (args.at(i) == "--curl-path") {
            curlPath = NEXT_ARG;
        } else if (args.at(i) == "--editable") {
            te->setReadOnly(false);
        } else if (args.at(i) == "--font") {
            te->setFont(QFont(NEXT_ARG));
        } else if (args.at(i) == "--checkbox") {
            vl->addWidget(cb = new QCheckBox(NEXT_ARG, dlg));
        } else if (args.at(i) == "--auto-scroll") {
            te->setProperty("guid_autoscroll", true);
        } else if (args.at(i) == "--html") {
            html = true;
            te->setProperty("guid_html", true);
        } else if (args.at(i) == "--plain") {
            plain = true;
        } else if (args.at(i) == "--no-interaction") {
            onlyMarkup = true;
        } else {
            WARN_UNKNOWN_ARG("--text-info");
        }
    }
    
    if (curlPath.isEmpty())
        curlPath = "curl";
    
    if (html) {
        te->setReadOnly(true);
        te->setTextInteractionFlags(onlyMarkup ? Qt::TextSelectableByMouse : Qt::TextBrowserInteraction);
    }
    if (te->isReadOnly()) {
        QPalette pal = te->viewport()->palette();
        for (int i = 0; i < 3; ++i) { // Disabled, Active, Inactive, Normal
            QPalette::ColorGroup cg = (QPalette::ColorGroup)i;
            pal.setColor(cg, QPalette::Base, pal.color(cg, QPalette::Window));
            pal.setColor(cg, QPalette::Text, pal.color(cg, QPalette::WindowText));
        }
        te->viewport()->setPalette(pal);
        te->viewport()->setAutoFillBackground(false);
        te->setFrameStyle(QFrame::NoFrame);
    }

    if (filename.isNull()) {
        listenToStdIn();
    } else if (url) {
        QProcess *curl = new QProcess;
        connect(curl, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() {
            te->setText(QString::fromLocal8Bit(curl->readAllStandardOutput()));
            delete curl;
        });
        curl->start(curlPath, QStringList() << "-L" << "-s" << filename);
    } else {
        QFile file(filename);
        QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

        if (file.open(QIODevice::ReadOnly)) {
            if (html)
                te->setHtml(QString::fromLocal8Bit(file.readAll()));
            else if (plain)
                te->setPlainText(QString::fromLocal8Bit(file.readAll()));
            else
                te->setText(QString::fromLocal8Bit(file.readAll()));
            file.close();
        }
    }

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);

    if (cb) {
        QPushButton *btn = btns->button(QDialogButtonBox::Ok);
        btn->setEnabled(false);
        connect(cb, SIGNAL(toggled(bool)), btn, SLOT(setEnabled(bool)));
    }

    SHOW_DIALOG
    return 0;
}

// End of "private (2 of 2): show dialogs"

/******************************************************************************
 * main
 ******************************************************************************/

int main(int argc, char **argv)
{
    if (argc < 2) {
        Guid::printHelp();
        return 1;
    }
    
    bool helpMission = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg(argv[i]);
        if (arg == "-h" || arg.startsWith("--help")) {
            helpMission = true;
            Guid::printHelp(arg.mid(7)); // "--help-"
        }
    }
    
    if (helpMission) {
        return 0;
    }
    
    QFont appFont("Sans-serif", 9);
    QApplication::setFont(appFont);
    foreach (QWidget *widget, QApplication::allWidgets()) {
        widget->setFont(appFont);
        widget->update();
    }
    
    Guid d(argc, argv);
    
    return d.exec();
}

// End of "main"
