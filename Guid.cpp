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
#include <QClipboard>
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
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCodec>
#include <QThread>
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

#include "qrcodegen/qrcodegen.hpp"

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
    QVBoxLayout *tll = new QVBoxLayout(dlg);

#define SHOW_DIALOG \
    m_dialog = dlg; \
    connect(dlg, SIGNAL(finished(int)), SLOT(dialogFinished(int))); \
    if (!m_size.isNull()) { \
        dlg->adjustSize(); \
        QSize sz = dlg->size(); \
        if (m_size.width() > 0) \
            sz.setWidth(m_size.width()); \
        if (m_size.height() > 0) \
            sz.setHeight(m_size.height()); \
        dlg->resize(sz); \
    } \
    if (m_alwaysOnTop || m_noTaskbar) { \
        Qt::WindowFlags allDialogFlags = dlg->windowFlags(); \
        if (m_alwaysOnTop) allDialogFlags |= Qt::WindowStaysOnTopHint; \
        if (m_noTaskbar) allDialogFlags |= Qt::Tool; \
        dlg->setWindowFlags(allDialogFlags); \
    } \
    dlg->show(); \
    if (m_noTaskbar) dlg->activateWindow();

#define FINISH_DIALOG(_BTNS_) \
    QDialogButtonBox *btns = new QDialogButtonBox(_BTNS_, Qt::Horizontal, dlg); \
    tll->addWidget(btns); \
    btns->setStyleSheet("QPushButton {padding: 8px 12px;}"); \
    if (!m_okCommand.isEmpty() || m_okKeepOpen) \
        connect(btns, SIGNAL(accepted()), this, SLOT(printFormsAfterOKClick())); \
    else \
        connect(btns, SIGNAL(accepted()), dlg, SLOT(accept())); \
    connect(btns, SIGNAL(rejected()), this, SLOT(afterCloseButtonClick()));

// We can't just set the selected tab name as bold because the tab width won't adjust,
// hence the workaround to set bold by default. This bug was solved in Qt 6.2.2:
// https://bugreports.qt.io/browse/QTBUG-6905
#define QTABBAR_STYLE \
    "QTabBar {font-weight: bold;} QTabBar::tab:!selected {font-weight: normal;}"

#define QTREEWIDGET_STYLE \
    "QHeaderView::section {border: 1px solid #E0E0E0; background: #F7F7F7; padding-left: 3px; font-weight: bold;}"

/*******************
 * Forms
 *******************/

#define SET_WIDGET_SETTINGS(ARG) \
    ws = WidgetSettings(); \
    next_arg_split = next_arg.split('@'); \
    foreach (QString setting, next_arg_split) { \
        if      (setting.startsWith("addLabel=")) ws.addLabel = getWidgetSettingQString(setting); \
        else if (setting.startsWith("addNewRowButton=")) ws.addNewRowButton = getWidgetSettingBool(setting); \
        else if (setting.startsWith("backgroundColor=")) ws.backgroundColor = getWidgetSettingQString(setting); \
        else if (setting.startsWith("buttonText=")) ws.buttonText = getWidgetSettingQString(setting); \
        else if (setting.startsWith("command=")) ws.command = getWidgetSettingQString(setting); \
        else if (setting.startsWith("commandToFooter=")) ws.commandToFooter = getWidgetSettingBool(setting); \
        else if (setting.startsWith("defaultIndex=")) ws.defaultIndex = getWidgetSettingInt(setting); \
        else if (setting.startsWith("defMarkerVal1=")) ws.defMarkerVal1 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal2=")) ws.defMarkerVal2 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal3=")) ws.defMarkerVal3 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal4=")) ws.defMarkerVal4 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal5=")) ws.defMarkerVal5 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal6=")) ws.defMarkerVal6 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal7=")) ws.defMarkerVal7 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal8=")) ws.defMarkerVal8 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("defMarkerVal9=")) ws.defMarkerVal9 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("disableButtons=")) ws.disableButtons = getWidgetSettingBool(setting); \
        else if (setting.startsWith("excludeFromOutput=")) ws.excludeFromOutput = getWidgetSettingBool(setting); \
        else if (setting.startsWith("foregroundColor=")) ws.foregroundColor = getWidgetSettingQString(setting); \
        else if (setting.startsWith("hideLabel=")) ws.hideLabel = getWidgetSettingBool(setting); \
        else if (setting.startsWith("image=")) ws.image = getWidgetSettingQString(setting); \
        else if (setting.startsWith("keepOpen=")) ws.keepOpen = getWidgetSettingBool(setting); \
        else if (setting.startsWith("monitor=")) ws.monitorFile = getWidgetSettingBool(setting); \
        else if (setting.startsWith("monitorMarkerFile1=")) ws.monitorMarkerFile1 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile2=")) ws.monitorMarkerFile2 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile3=")) ws.monitorMarkerFile3 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile4=")) ws.monitorMarkerFile4 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile5=")) ws.monitorMarkerFile5 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile6=")) ws.monitorMarkerFile6 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile7=")) ws.monitorMarkerFile7 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile8=")) ws.monitorMarkerFile8 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorMarkerFile9=")) ws.monitorMarkerFile9 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName1=")) ws.monitorVarName1 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName2=")) ws.monitorVarName2 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName3=")) ws.monitorVarName3 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName4=")) ws.monitorVarName4 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName5=")) ws.monitorVarName5 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName6=")) ws.monitorVarName6 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName7=")) ws.monitorVarName7 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName8=")) ws.monitorVarName8 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("monitorVarName9=")) ws.monitorVarName9 = getWidgetSettingQString(setting); \
        else if (setting.startsWith("sep=")) ws.sep = getWidgetSettingQString(setting); \
        else if (setting.startsWith("stop=")) ws.stop = getWidgetSettingBool(setting); \
        else if (setting.startsWith("valuesToFooter=")) ws.valuesToFooter = getWidgetSettingBool(setting); \
        else if (setting.startsWith("verboseTabBar=")) ws.verboseTabBar = getWidgetSettingBool(setting); \
        else    next_arg_join << setting; \
    } \
    next_arg = next_arg_join.join('@'); \
    next_arg_split.clear(); \
    next_arg_join.clear();

#define SWITCH_FORM_WIDGET(NEW_WIDGET) \
    if (lastWidgetId == "text-browser") setTextInfo(lastTextBrowser); \
    else if (lastWidgetId == "text-info") setTextInfo(lastTextInfo); \
    if (!lastWidgetVar.isEmpty() && lastWidget) lastWidget->setProperty("guid_var", lastWidgetVar); \
    lastWidgetId = NEW_WIDGET;

#define SET_FORMS_MENU_ITEM_DATA \
    if (menuItemData.count() > 0) menuItemName = menuItemData.at(0); \
    if (menuItemData.count() > 1) menuItemExitCode = menuItemData.at(1).toInt(&ok); \
    if (menuItemData.count() > 2) menuItemCommand = menuItemData.at(2); \
    if (menuItemData.count() > 3) menuItemCommandPrintOutput = (menuItemData.at(3) == "true" || menuItemData.at(3) == "1") ? true : false; \
    if (menuItemData.count() > 4) menuItemIcon = menuItemData.at(4);

#define ADD_WIDGET_TO_FORM(LABEL, WIDGET) \
    if (lastColumn == "col1") { \
        labelCol1 = LABEL; \
        if (ws.hideLabel) hideCol1Label = true; \
        if (col1HSpacer == "before") columnsLayout->addStretch(); \
        columnsLayout->addWidget(WIDGET); \
        if (col1HSpacer == "after") columnsLayout->addStretch(); \
        col1HSpacer = ""; \
        columnsLayout->setAlignment(WIDGET, col1VAlignFlag); \
    } else if (lastColumn == "col2") { \
        if (!ws.hideLabel) { \
            LABEL->setContentsMargins(0, 3, 0, 0); \
            columnsLayout->addWidget(LABEL); \
            columnsLayout->setAlignment(LABEL, Qt::AlignTop); \
        } \
        if (col2HSpacer == "before") columnsLayout->addStretch(); \
        columnsLayout->addWidget(WIDGET); \
        if (col2HSpacer == "after") columnsLayout->addStretch(); \
        col2HSpacer = ""; \
        columnsLayout->setAlignment(WIDGET, col2VAlignFlag); \
        col1VAlignFlag = Qt::AlignTop; \
        col2VAlignFlag = Qt::AlignTop; \
        columnsContainer = new QWidget(); \
        columnsContainer->setProperty("guid_cols_container", true); \
        columnsContainer->setProperty("guid_separator", dlg->property("guid_separator").toString()); \
        columnsLayout->setContentsMargins(0, 1, 0, 0); \
        columnsContainer->setLayout(columnsLayout); \
        if (!lastGroupName.isEmpty()) { \
            if (!hideCol1Label) { \
                lastGroupLayout->addRow(labelCol1, columnsContainer); \
            } else { \
                lastGroupLayout->addRow(columnsContainer); \
            } \
            lastGroupLayout->setAlignment(columnsContainer, Qt::AlignTop); \
        } else if (!lastTabName.isEmpty()) { \
            if (!hideCol1Label) { \
                lastTabLayout->addRow(labelCol1, columnsContainer); \
            } else { \
                lastTabLayout->addRow(columnsContainer); \
            } \
            lastTabLayout->setAlignment(columnsContainer, Qt::AlignTop); \
        } else if (addingToHeader) { \
            if (!hideCol1Label) headerLayout->addRow(labelCol1, columnsContainer); \
            else headerLayout->addRow(columnsContainer); \
        } else if (!hideCol1Label) { \
            fl->addRow(labelCol1, columnsContainer); \
        } else { \
            fl->addRow(columnsContainer); \
        } \
        if (lastWidgetId == "vspacer") \
            columnsLayout->setSpacing(0); \
        lastColumn = ""; \
        labelCol1 = new QLabel(); \
        hideCol1Label = false; \
    } else if (!lastGroupName.isEmpty()) { \
        if (!ws.hideLabel) { \
            lastGroupLayout->addRow(LABEL, WIDGET); \
        } else { \
            lastGroupLayout->addRow(WIDGET); \
        } \
        lastGroupLayout->setAlignment(WIDGET, Qt::AlignTop); \
    } else if (!lastTabName.isEmpty()) { \
        if (!ws.hideLabel) { \
            lastTabLayout->addRow(LABEL, WIDGET); \
        } else { \
            lastTabLayout->addRow(WIDGET); \
        } \
        lastTabLayout->setAlignment(WIDGET, Qt::AlignTop); \
    } else if (lastWidgetId == "menu" && lastMenuIsTopMenu) { \
        tml->addRow(WIDGET); \
        WIDGET->setStyleSheet("QMenuBar {padding-top: 7px; padding-bottom: 7px;}"); \
    } else if (addingToHeader) { \
        headerLayout->addRow(WIDGET); \
    } else if (!ws.hideLabel) { \
        fl->addRow(LABEL, WIDGET); \
    } else { \
        fl->addRow(WIDGET); \
    }

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
 * class ReadOnlyColumn
 ******************************************************************************/

class ReadOnlyColumn : public QStyledItemDelegate {
public:
    ReadOnlyColumn(QObject* parent = 0): QStyledItemDelegate(parent) {}
    virtual QWidget* createEditor(QWidget *, const QStyleOptionViewItem &, const QModelIndex &) const {
        return 0;
    }
};

// End of "class ReadOnlyColumn"

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

    if (height >= 0) {
        QSizePolicy twSizePolicy = tw->sizePolicy();
        twSizePolicy.setVerticalPolicy(QSizePolicy::Fixed);
        tw->setSizePolicy(twSizePolicy);
        if (height < getQTreeWidgetSize(&tw).height())
            tw->setFixedHeight(height);
    }
    
    int roColumnNumber = tw->property("guid_list_read_only_column").toInt();
    roColumnNumber = roColumnNumber - 1;
    if (roColumnNumber >= 0 && roColumnNumber < columns.count())
        tw->setItemDelegateForColumn(roColumnNumber, new ReadOnlyColumn(tw));

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
    if (!w || w->property("guid_hide").toBool())
        return ValuePair(false, QString());
    
    QString var = w->property("guid_var").toString().simplified().replace(" ", "");
    if (!var.isEmpty())
        var += "=";
    
    IF_IS(QLineEdit) {
        return ValuePair(true, var + t->text());
    } else IF_IS(QTreeWidget) {
        if (t->selectionMode() == QAbstractItemView::NoSelection ||
            t->property("guid_list_exclude_from_output").toBool())
            return ValuePair(false, QString());
        
        QString results = "";
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
        
        bool verboseMode = t->property("guid_tab_bar_verbose").toBool();
        QString tabValue = "";
        QString tabValuePrefix = "";
        QString tabValueSuffix = "";
        QString tabSelectionMarker = "";
        bool addTabValue = false;
        
        for (int i = 0; i < t->count(); ++i) {
            tabValue = "";
            tabValuePrefix = "";
            tabValueSuffix = "";
            tabSelectionMarker = "";
            addTabValue = false;
            
            QWidget *tab = t->widget(i);
            
            if (tab && verboseMode) {
                if (i == t->currentIndex())
                    tabSelectionMarker = "*";
                tabValuePrefix = "<TAB_START" + tabSelectionMarker + ">" +
                                 t->tabText(i) + "</TAB_START" + tabSelectionMarker + ">";
                tabValueSuffix = "<TAB_END" + tabSelectionMarker + ">" +
                                 t->tabText(i) + "</TAB_END" + tabSelectionMarker + ">";
            }
            
            QList<QWidget*> tabChildren = tab->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            foreach(QWidget *tabChild, tabChildren) {
                if (qstrcmp(tabChild->metaObject()->className(), "QLabel") == 0)
                    continue;
                tabPair = getFormsWidgetValue(tabChild, dateFormat, separator, listRowSeparator);
                if (tabPair.first) {
                    addTabValue = true;
                    if (!tabValue.isEmpty())
                        tabValue += separator;
                    tabValue += tabPair.second;
                }
            }
            
            if (addTabValue) {
                if (!tabsValue.isEmpty())
                    tabsValue += separator;
                tabsValue += tabValuePrefix + tabValue + tabValueSuffix;
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
            t->property("guid_file_sel_container").toBool() ||
            t->property("guid_scale_container").toBool() ||
            qstrcmp(t->metaObject()->className(), "QGroupBox") == 0) {
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

static bool getWidgetSettingBool(QString setting)
{
    QString value = setting.toLower().section('=', 1, 1);
    return (value == "1" || value == "true") ? true : false;
}

static int getWidgetSettingInt(QString setting)
{
    bool ok;
    int value = setting.section('=', 1, 1).toInt(&ok);
    if (!ok)
        value = -100;
    return value;
}

static QString getWidgetSettingQString(QString setting)
{
    QStringList results = setting.split("=");
    results.removeAt(0);
    return results.join("=");
}

static GList listValuesFromFile(QString data)
{
    GList list = GList();
    QStringList data_join;
    QStringList data_split = data.split('@');
    foreach (QString setting, data_split) {
        if (setting.startsWith("addValue="))
            list.addValue = getWidgetSettingQString(setting);
        else if (setting.startsWith("monitor="))
            list.monitorFile = getWidgetSettingBool(setting);
        else if (setting.startsWith("sep=")) {
            list.fileSep = getWidgetSettingQString(setting);
        } else
            data_join << setting;
    }
    if (list.fileSep.isEmpty())
        list.fileSep = "\n";
    list.filePath = data_join.join('@');
    QFile file(list.filePath);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    if (file.open(QIODevice::ReadOnly)) {
        list.val = QString::fromLocal8Bit(file.readAll()).trimmed().replace(QRegExp("[\r\n]+"), list.fileSep).split(list.fileSep);
        file.close();
    }
    return list;
}

static bool pathTester(QString filePath)
{
    bool fileExists = false;
    int timer = 500;
    
    while (timer > 0) {
        if (QFile::exists(filePath)) {
            fileExists = true;
            
            break;
        }
        
        timer = timer - 20;
        QThread::msleep(20);
    }
    
    return fileExists;
}

static void setGroup(QGroupBox* &group, QFormLayout* &layout, QLabel* groupLabel, QString &lastGroupName)
{
    if (groupLabel)
        layout->addRow(groupLabel, group);
    else
        layout->addRow(group);
    
    group = nullptr;
    lastGroupName = "";
}

static void setTabBar(QTabWidget* &tabBar, QFormLayout* &layout, QLabel* tabBarLabel, QString &tabName, int &tabIndex)
{
    if (tabBarLabel)
        layout->addRow(tabBarLabel, tabBar);
    else
        layout->addRow(tabBar);
    
    tabBar = new QTabWidget();
    tabBar->setProperty("guid_tab_bar_verbose", false);
    tabBar->setStyleSheet(QTABBAR_STYLE);
    tabName = "";
    tabIndex = -1;
}

static void setText(QLabel *text)
{
    QString textTemplate = text->property("guid_text_content").toString();
    QString textContent = textTemplate;
    QString defaultTextContent = textTemplate;
    
    if (!text->property("guid_text_markers_set").toBool()) {
        for (int i = 1; i < 10; ++i) {
            QString propDefMarkerVal = "guid_text_def_marker_val_" + QString::number(i);
            QString defMarkerVal = text->property(propDefMarkerVal.toStdString().c_str()).toString();
            if (defMarkerVal.isEmpty())
                defMarkerVal = "(?)";
            defaultTextContent.replace("GUID_MARKER_" + QString::number(i), defMarkerVal);
        }
        
        text->setText(defaultTextContent);
        text->setProperty("guid_text_markers_set", true);
    }
    
    for (int i = 1; i < 10; ++i) {
        QString propDefMarkerVal = "guid_text_def_marker_val_" + QString::number(i);
        QString defMarkerVal = text->property(propDefMarkerVal.toStdString().c_str()).toString();
        if (defMarkerVal.isEmpty())
                defMarkerVal = "(?)";
        
        QString propMarkerFile = "guid_text_monitor_marker_file_" + QString::number(i);
        QString filePath = text->property(propMarkerFile.toStdString().c_str()).toString();
        
        if (!filePath.isEmpty()) {
            QFile file(filePath);
            QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
            
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray markerValue = file.readAll();
                while (markerValue.right(1) == "\n")
                    markerValue.chop(1);
                
                QString newValue = QString(markerValue);
                QString propMonitorVarName = "guid_text_monitor_var_name_" + QString::number(i);
                QString monitorVarName = text->property(propMonitorVarName.toStdString().c_str()).toString();
                bool varFound = false;
                if (!monitorVarName.isEmpty()) {
                    newValue.replace(QRegExp("[\r\n]+"), "\n");
                    QStringList newValueLines = newValue.split("\n");
                    foreach(QString line, newValueLines) {
                        QString varName = line.section('=', 0, 0);
                        if (varName == monitorVarName) {
                            varFound = true;
                            newValue = line.section('=', 1, 1);
                            break;
                        }
                    }
                    if (!varFound)
                        newValue = "";
                }
                
                if (newValue.isEmpty())
                    newValue = defMarkerVal;
                
                textContent.replace("GUID_MARKER_" + QString::number(i), newValue);
                
                file.close();
            }
        }
    }
    
    if (text->text() != textContent)
        text->setText(textContent);
}

static void setTextInfo(QTextEdit *textInfo)
{
    QString filename = textInfo->property("guid_text_filename").toString();
    bool isReadOnly = textInfo->property("guid_text_read_only").toBool();
    bool isUrl = textInfo->property("guid_text_is_url").toBool();
    QString format = textInfo->property("guid_text_format").toString();
    QString curlPath = textInfo->property("guid_text_curl_path").toString();
    int heightToSet = textInfo->property("guid_text_height").toInt();
    
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
    m_alwaysOnTop(false),
    m_closeToSysTray(false),
    m_dialog(NULL),
    m_modal(false),
    m_noTaskbar(false),
    m_notificationId(0),
    m_okCommand(""),
    m_okCommandToFooter(false),
    m_okKeepOpen(false),
    m_okValuesToFooter(false),
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
    QString main_separator      = "#########################################################################################";
    QString secondary_separator = "-----------------------------------------------------------------------------------------";
    
    if (helpDict.isEmpty()) {
        /******************************
         * help
         ******************************/
        
        helpDict["help"] = CategoryHelp(tr("Help options"), HelpList() <<
        Help("-h, --help",
             tr("Show help options")) <<
        Help("--help-all",
             tr("Show all help options")) <<
        Help("--help-general",
             tr("Show general options")) <<
        Help("--help-misc",
             tr("Show miscellaneous options")) <<
        Help("", "") <<
        
        Help("--help-calendar",
             tr("Show calendar options")) <<
        Help("--help-color-selection",
             tr("Show color selection options")) <<
        Help("--help-entry",
             tr("Show text entry options")) <<
        Help("--help-error",
             tr("Show error dialog options")) <<
        Help("--help-file-selection",
             tr("Show file selection options")) <<
        Help("--help-forms",
             tr("Show forms dialog options")) <<
        Help("--help-info",
             tr("Show info dialog options")) <<
        Help("--help-list",
             tr("Show list options")) <<
        Help("--help-notification",
             tr("Show notification icon options")) <<
        Help("--help-password",
             tr("Show password dialog options")) <<
        Help("--help-progress",
             tr("Show progress options")) <<
        Help("--help-question",
             tr("Show question options")) <<
        Help("--help-scale",
             tr("Show scale options")) <<
        Help("--help-text-info",
             tr("Show text information options")) <<
        Help("--help-warning",
             tr("Show warning dialog options")));
        
        /******************************
         * misc
         ******************************/
        
        helpDict["misc"] = CategoryHelp(tr("Miscellaneous options"), HelpList() <<
        Help("--about",
             tr("About guid")) <<
        Help("--version",
             tr("Print guid version")));
        
        /******************************
         * general
         ******************************/
        
        helpDict["general"] = CategoryHelp(tr("General options"), HelpList() <<
        Help("--title=TITLE",
             tr("Set the dialog title")) <<
        Help("--window-icon=/path/to/icon",
             tr("Set the window icon")) <<
        Help("--width=WIDTH",
             tr("Set the dialog width")) <<
        Help("--height=HEIGHT",
             tr("Set the dialog height")) <<
        Help("", "") <<
        
        Help("--ok-label=TEXT",
             tr("Set the label of the Ok button")) <<
        Help("--cancel-label=TEXT",
             tr("Set the label of the Cancel button")) <<
        Help("", "") <<
        
        Help("--timeout=TIMEOUT",
             tr("Set dialog timeout in seconds")) <<
        Help("--always-on-top",
             tr("Force the dialog to be always on top of other windows")) <<
        Help("--no-taskbar",
             tr("Don't display the dialog in the taskbar")) <<
        Help("--attach=WINDOW",
             tr("Set the parent window to attach to")) <<
        Help("--modal",
             tr("Set the modal hint")) <<
        Help("--output-prefix-ok=PREFIX",
             tr("Set prefix for output sent to stdout")) <<
        Help("--output-prefix-err=PREFIX",
             tr("Set prefix for output sent to stderr")));
        
        /******************************
         * application
         ******************************/
        
        helpDict["application"] = CategoryHelp(tr("Application options"), HelpList() <<
        Help("--display=DISPLAY",
             tr("X display to use")) <<
        Help("", "") <<
        
        Help("--calendar",
             tr("Display calendar dialog")) <<
        Help("--color-selection",
             tr("Display color selection dialog")) <<
        Help("--entry",
             tr("Display text entry dialog")) <<
        Help("--error",
             tr("Display error dialog")) <<
        Help("--file-selection",
             tr("Display file selection dialog")) <<
        Help("--font-selection",
             tr("Display font selection dialog")) <<
        Help("--forms",
             tr("Display forms dialog")) <<
        Help("--info",
             tr("Display info dialog")) <<
        Help("--list",
             tr("Display list dialog")) <<
        Help("--notification",
             tr("Display notification")) <<
        Help("--password",
             tr("Display password dialog")) <<
        Help("--progress",
             tr("Display progress indication dialog")) <<
        Help("--question",
             tr("Display question dialog")) <<
        Help("--scale",
             tr("Display scale dialog")) <<
        Help("--text-info",
             tr("Display text information dialog")) <<
        Help("--warning",
             tr("Display warning dialog")));
        
        /******************************
         * calendar
         ******************************/
        
        helpDict["calendar"] = CategoryHelp(tr("Calendar options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--align=left|center|right",
             tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--day=DAY",
             tr("Set the calendar day")) <<
        Help("--month=MONTH",
             tr("Set the calendar month")) <<
        Help("--year=YEAR",
             tr("Set the calendar year")) <<
        Help("--date-format=PATTERN",
             tr("Set the format for the returned date")) <<
        Help("", "") <<
        
        Help("--timeout=TIMEOUT",
             tr("Set dialog timeout in seconds")));
        
        /******************************
         * color-selection
         ******************************/
        
        helpDict["color-selection"] = CategoryHelp(tr("Color selection options"), HelpList() <<
        Help("--color=VALUE",
             tr("Set the color")) <<
        Help("--custom-palette=/path/to/file.gpl",
             tr("Load a custom GPL file for standard colors")) <<
        Help("--show-palette",
             tr("Show the palette")));
        
        /******************************
         * entry
         ******************************/
        
        helpDict["entry"] = CategoryHelp(tr("Text entry options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--entry-text=TEXT",
             tr("Set the entry text")) <<
        Help("--hide-text",
             tr("Hide the entry text")) <<
        Help("", "") <<
        
        Help("--float=FLOATING_POINT",
             tr("Floating point input only, preset given value")) <<
        Help("--int=INTEGER",
             tr("Integer input only, preset given value")) <<
        Help("--values=v1|v2|v3|...",
             tr("Offer preset values to pick from")));
        
        /******************************
         * error
         ******************************/
        
        helpDict["error"] = CategoryHelp(tr("Error options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--ellipsize",
             tr("Do wrap text (zenity has a rather special problem here)")) <<
        Help("--no-markup",
             tr("Do not enable HTML markup")) <<
        Help("--no-wrap",
             tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON_NAME",
             tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels",
             tr("Allow to select text for copy and paste")));
        
        /******************************
         * file-selection
         ******************************/
        
        helpDict["file-selection"] = CategoryHelp(tr("File selection options"), HelpList() <<
        Help("--filename=/path/to/file",
             tr("Set the file selected by default")) <<
        Help("--file-filter=NAME|PATTERN1 PATTERN2...",
             tr("Set a filename filter")) <<
        Help("--directory",
             tr("Activate directory-only selection")) <<
        Help("--multiple",
             tr("Allow multiple files to be selected")) <<
        Help("", "") <<
        
        Help("--confirm-overwrite",
             tr("Confirm file selection if the file selected already exists")) <<
        Help("--save",
             tr("Activate save mode")) <<
        Help("", "") <<
        
        Help("--separator=SEPARATOR",
             tr("Set output separator character")));
        
        /******************************
         * font-selection
         ******************************/
        
        helpDict["font-selection"] = CategoryHelp(tr("Font selection options"), HelpList() <<
        Help("--sample=TEXT",
             tr("Sample text (default is \"The quick brown fox jumps over the lazy dog.\"")) <<
        Help("--pattern=%1-%2:%3:%4",
             tr("Output pattern where %1 is name, %2 is size, %3 is weight, %4 is slant.")) <<
        Help("--type=[vector][,bitmap][,fixed][,variable]",
             tr("Filter fonts (default is all)")));
        
        /******************************
         * forms
         ******************************/
        
        const char *formsVarHelp = R"HEREDOC(Variable name added before the field value when printed to the console.
Spaces are removed and the character "=" is added after the variable name. Example
without "--var":
    guid --forms \
         --add-calendar="Choose a date" \
         --add-entry="Type your pseudo"
Here's what the output printed to the console looks like:
    2020-12-12|Little Mouse
Example with "--var":
    guid --forms \
         --add-calendar="Choose a date" --var="cal" \
         --add-entry="Type your pseudo" --var="pseudo"
Here's what the output printed to the console looks like:
    cal=2020-12-12|pseudo=Little Mouse)HEREDOC";
        
        helpDict["forms"] = CategoryHelp(tr("Forms dialog options"), HelpList() <<
        
        // Widget settings
        Help(tr(R"HEREDOC(Widgets are added to forms dialog using the following syntax:
    --add-WIDGET-TYPE="Label displayed"
Example:
    guid --forms \
         --add-entry="Your name" \
         --add-file-selection="Your document"
Several widgets can be configured with variables using a syntax like this:
    --add-WIDGET-TYPE="variableName=Value@anotherVariableName=Value@Label displayed"
Example:
    guid --forms \
         --add-text="image=:/info@Text with image" \
         --add-entry="Your name" \
         --add-file-selection="buttonText=Select file@Your document"
The list of variables supported is displayed at the beginning of each widget section.)HEREDOC"), "") <<
        Help("", "") <<
        
        // --text
        Help("--text=\"Form label (form description)\"",
             tr("Set the form label (always displayed on top, and bold by default")) <<
        Help("--align=left|center|right",
             tr("Set label alignment")) <<
        Help("--no-bold",
             tr("Remove bold for the form label")) <<
        Help("--italics",
             tr("Set form label in italics")) <<
        Help("--underline",
             tr("Set form label format to underline")) <<
        Help("--small-caps",
             tr("Set form label rendering to small-caps type")) <<
        Help("--font-family=FAMILY",
             tr("Set font family for the form label")) <<
        Help("--font-size=SIZE",
             tr("Set font size for the form label")) <<
        Help("--foreground-color=COLOR",
             tr(R"HEREDOC(Set form label color. Example:
    guid --forms \
         --text="Form label (form description)" \
             --foreground-color="#0000FF" \
         --add-entry="Entry field not colored")HEREDOC")) <<
        Help("--background-color=COLOR",
             tr(R"HEREDOC("Set form label background color. Example:
    guid --forms \
         --text="Form label (form description)" \
         --background-color="#0000FF" \
         --add-entry="Entry field not colored")HEREDOC")) <<
        Help("", "") <<
        
        // --header
        Help(R"HEREDOC(--header="[backgroundColor=COLOR@][foregroundColor=COLOR@][hideLabel=true@]
          [stop=true@]Header label")HEREDOC",
             tr(R"HEREDOC("Add a header to the form.
The header is a section without margins (so the background color fills the entire
dialog width) under the top menu but over form fields. It's useful to display
information (input fields added in the header are not parsed for user input). Next
fields will be added to the header unless a new "--header" is added containing the
variable "stop=true". Example:
    guid --forms \
         --header="backgroundColor=#DEDEDE@hideLabel=true" \
             --add-text="image=:/info@Text added in the header" \
             --add-text="More text in the header" --bold --align=center \
         --header="stop=true" \
         --add-entry="Field outside the header")HEREDOC")) <<
        Help("", "") <<
        
        // --group
        Help("--group=\"[addLabel=Group label@][stop=true@]Group name\"",
             tr(R"HEREDOC(Add a group box (fieldset).
Next fields will be added to the group unless a new "--group" is added containing
the variable "stop=true". Example:
    guid --forms \
         --text="Form with a group box (fieldset)" \
         --group="Group name" \
             --add-entry="Text field in the group" \
             --add-entry="Another text field in the group" \
         --group="stop=true" \
         --add-text="Text outside the group")HEREDOC")) <<
        Help("", "") <<
        
        // --tab
        Help(R"HEREDOC(--tab="[addLabel=Tab bar label@][disableButtons=true@][stop=true@]
       [verboseTabBar=true@]Tab name")HEREDOC",
             tr(R"HEREDOC(Create a tab bar.
After adding "--tab=NAME", next fields will be added to the tab NAME unless another
one is specified with "--tab=ANOTHER_NAME". Stop adding fields in the last tab with
the variable "stop=true". Example:
    guid --forms \
         --text="Form with a tab bar" \
         --tab="Tab 1" \
             --add-entry="Text field in the tab 1" \
             --add-entry="Another text field in the tab 1" \
         --tab="Tab 2" \
             --add-spin-box="Spin box in the tab 2" --min-value=10 --value=50 \
         --tab="stop=true" \
         --add-scale="Field outside the tab bar" --step=5
To disable dialog buttons when the tab is displayed, add the variable
"disableButtons=true". Example:
    guid --forms \
         --text="Form with a tab bar" \
         --tab="Tab 1" \
             --add-entry="Text field in the tab 1" \
             --add-entry="Another text field in the tab 1" \
         --tab="disableButtons=true@Tab 2" \
             --add-spin-box="Spin box in the tab 2" --min-value=10 --value=50 \
         --tab="stop=true" \
         --add-scale="Field outside the tab bar" --step=5
To include information about the tab bar in values printed to the console, add the
variable "verboseTabBar=true". Separators will be added in the output between tab
fields and the selected tab will be marked with "*".)HEREDOC")) <<
        Help("--tab-selected",
             tr("Mark the tab as selected by default when the dialog is shown")) <<
        Help("", "") <<
        
        // --col1 || --col2
        Help("--col1",
             tr(R"HEREDOC(Start a two-field row.
The next form field specified will be added in the first column. See "--col2" for
details.)HEREDOC")) <<
        Help("--col2",
             tr(R"HEREDOC(Finish a two-field row.
The next form field specified will be added in the second column. Example:
    guid --forms --width=500 \
         --text="The next row has two columns:" \
         --col1 \
             --add-entry="Left entry (column 1)" --int=5 \
         --col2 \
             --add-entry="Right entry (column 2)" --field-width=100 \
         --add-entry="Outside columns")HEREDOC")) <<
        Help("--add-hspacer=before|after",
             tr(R"HEREDOC(Add horizontal expanding spacer before or after the current
column content)HEREDOC")) <<
        Help("--valign=top|center|bottom|baseline",
             tr(R"HEREDOC(Set vertical alignment of the current column. Example:
    guid --forms --width=500 \
         --text="The next row has two columns:" \
         --col1 --valign=bottom \
             --add-text="Aligned to the bottom" \
         --col2 \
             --add-text="Lorem<br />ipsum<br />dolor<br />sit<br />amet" \
         --add-entry="Outside columns")HEREDOC")) <<
        Help("", "") <<
        
        // --add-calendar
        Help("--add-calendar=\"[hideLabel=true@]Calendar label\"",
             tr("Add a calendar in forms dialog")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-checkbox
        Help("--add-checkbox=\"[addLabel=Checkbox label@]Checkbox text\"",
             tr("Add a checkbox in forms dialog")) <<
        Help("--checked",
             tr("Make the checkbox checked by default")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-combo
        Help("--add-combo=\"[hideLabel=true@]Combo label\"",
             tr("Add a combo box in forms dialog")) <<
        Help("--combo-values=[defaultIndex=Index number@]List of values separated by |",
             tr(R"HEREDOC(List of values for the combo box.
To select a specific index by default, add the variable "defaultIndex=Index number".)HEREDOC")) <<
        Help("--combo-values-from-file=[defaultIndex=Index number@][monitor=VALUE@]Path to file",
             tr(R"HEREDOC(Use the file content as combo values (one value per line).
To select a specific index by default, add the variable "defaultIndex=Index number".
To monitor file changes, add "monitor=true". Example:
    guid --forms \
         --add-combo="Combo label" \
         --combo-values-from-file="defaultIndex=1@monitor=true@C:\\Path\\to\\file.txt")HEREDOC")) <<
        Help("--editable",
             tr("Allow the user to edit combo values")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-entry
        Help("--add-entry=\"[hideLabel=true@]Entry label\"",
             tr("Add a text entry in forms dialog")) <<
        Help("--float=FLOATING_POINT",
             tr("Floating point input only, preset given value")) <<
        Help("--int=INTEGER",
             tr("Integer input only, preset given value")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-file-selection
        Help("--add-file-selection=\"[buttonText=Text@][hideLabel=true@]File selection label\"",
             tr(R"HEREDOC(File selection options.
The field name is displayed in a button that opens the file selection dialog.
The selection is added and displayed in a text entry.)HEREDOC")) <<
        Help("--filename=/path/to/file",
             tr("Set the file selected by default")) <<
        Help("--file-filter=NAME|PATTERN1 PATTERN2...",
             tr("Set a filename filter")) <<
        Help("--directory",
             tr("Activate directory-only selection")) <<
        Help("--multiple",
             tr("Allow multiple files to be selected")) <<
        Help("--file-separator=SEPARATOR",
             tr(R"HEREDOC(Set output separator character if there are multiple files selected
(default is "~"))HEREDOC")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-list
        Help(R"HEREDOC(--add-list="[addNewRowButton=true@][excludeFromOutput=true@]
            [hideLabel=true@]List label")HEREDOC",
             tr(R"HEREDOC(Add a list in forms dialog.
To include a button allowing to add new rows when clicked, add the variable
"addNewRowButton=true". Example:
    guid --forms \
         --add-list="addNewRowButton=true@My list" \
            --column-values="C1|C2" --show-header \
            --list-values="v1|v2|v3|v4" --editable
To exclude the list from the output printed to the console, add the variable
"excludeFromOutput=true". It's useful when using lists for displaying data instead of
asking users to make a selection.
To hide the list label, add the variable "hideLabel=true". Example:
    guid --forms \
         --add-list="addNewRowButton=true@hideLabel=true@Label hidden" \
             --column-values="C1|C2" --show-header \
             --list-values="v1|v2|v3|v4" --editable)HEREDOC")) <<
        Help("--column-values=Column names separated by |",
             tr("List of column names")) <<
        Help("--list-values=List values separated by |",
             tr("List values")) <<
        Help("--list-values-from-file=\"[addValue=Value@][monitor=true@][sep=Separator@]Path to file\"",
             tr(R"HEREDOC(Use the file content as list values. Example:
    guid --forms \
         --add-list="List label" \
             --column-values="Column 1|Column 2" --show-header \
             --list-values-from-file="/path/to/file"
To add automatically a value at the beginning of each row (for example the
value "false" in combination with "--checklist"), add the variable "addValue=Value".
For example, to add the word "false", so checkboxes won't be checked by default:
    guid --forms \
         --add-list="List label" --checklist \
             --column-values="Delete|Column 1|Column 2" --show-header \
             --list-values-from-file="addValue=false@/path/to/file"
To monitor file changes, add the variable "monitor=true". Example:
    guid --forms \
         --add-list="List label" \
             --column-values="Column 1|Column 2" --show-header \
             --list-values-from-file="monitor=true@/path/to/file"
By default, the character "|" is used as separator between values. To use another
separator, specify it with the variable "sep=Separator". Example with "," as
separator:
    guid --forms \
         --add-list="List label" \
             --column-values="Column 1|Column 2" --show-header \
             --list-values-from-file="sep=,@/path/to/file"
Example with file changes monitored and with a custom separator:
    guid --forms \
         --add-list="List label" \
             --column-values="Column 1|Column 2" --show-header \
             --list-values-from-file="monitor=true@sep=,@/path/to/file")HEREDOC")) <<
        Help("--checklist",
             tr("Add checkboxes in the first column (multiple rows will be selectable)")) <<
        Help("--radiolist",
             tr("Add radio buttons in the first column (only one row will be selectable)")) <<
        Help("--editable",
             tr("Allow the user to edit list values")) <<
        Help("--read-only-column=Column number",
             tr("If editable, set a specific column as read only")) <<
        Help("--multiple",
             tr("Allow multiple rows to be selected")) <<
        Help("--no-selection",
             tr(R"HEREDOC(Prevent row selection (useful to display information in a structured way
without user interaction))HEREDOC")) <<
        Help("--print-column=NUMBER|all",
             tr(R"HEREDOC(Print a specific column to the console.
Default is "1" (print only the first column). The value "all" can be used to print
all columns.)HEREDOC")) <<
        Help("--print-values=selected|all",
             tr(R"HEREDOC(Print selected values (by default) or all values (useful in combination
with "--editable" to get updated values).)HEREDOC")) <<
        Help("--list-row-separator=SEPARATOR",
             tr("Set output separator character for list rows (default is \"~\")")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--show-header",
             tr("Show the columns header")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-menu
        Help("--add-menu=\"addLabel=Menu label@Menu settings\"",
             tr(R"HEREDOC(Add a menu in forms dialog.
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.
First level menu items must be separated with the character "|". Example:
    Main Item A|Main Item B|Main Item C
Second level menu items must be separated with the character "#". Example:
    Main Item A#Subitem A1#Subitem A2|Main Item B|Main Item C#Subitem C1
A click on a first level menu item will initiate an action if it doesn't contain
subitems. However, a click on a first level menu item containing subitems will only
display the second level menu.
A click on a second level menu item will always initiate an action since they can't
contain subitems.
Each menu item without subitems can have up to 5 settings separated with the
character ";", as follows:
    1              2         3              4                    5
    Menu item name;Exit code;Command to run;Print command output;Icon
The menu item name is mandatory. Other settings have these default values:
    1              2  3  4 5
    Menu item name;-1;"";0;""
Allowed values are as follows:
  1) Menu item name: Any string not containing the character ";".
  2) Exit code: Integer. If the value is < 0, the dialog will not be closed after
     a click on the menu item. If the value is >=0 and <= 255, the dialog will be
     closed with the exit code sent by the command "exit()".
  3) Command to run: The executable name or the executable path and its arguments.
     Arguments must be separated with "<>". For example, if the command to run is
     the following:
         explorer.exe /separate, "C:\\Users\\My Name\\Desktop"
     it must be set like this:
         explorer.exe<>/separate,<>"C:\\Users\\My Name\\Desktop"
     It's also possible to specify one of these internal commands:
         guidInfo<>Information to display
         guidWarning<>Warning to display
         guidError<>Error to display
  4) Print command output: true|false. If the value is "true", the command output
     will be printed to the console.
  5) Icon: For now, the only valid value is "false". If the value is "false", the
     default icon won't be added to the left of the menu item.
     TODO: Set arbitrary icon.
No matter if the dialog is closed or not after a click on the menu item, all
settings (menu name, exit code, etc.) are printed to the console after a click,
so it can be parsed by an external script. Here's an example:
    guid --forms \
         --add-menu="File#Profile;-1;explorer.exe<>%UserProfile%;0|Exit;10"
To add separators between first level menu items, add the variable "sep=SEP".
Example:
    guid --forms \
         --add-menu="sep=|@File#Profile;-1;explorer.exe<>%UserProfile%;0|Exit;10"
If a menu is the first widget added to the form and the form doesn't have a main
label added with --text="Form label (form description)", special settings are
applied to the menu to have it look like a main application menu. If we want a menu
on top of the dialog but still have a form label (form description), we can't add
the form label with --text="Form label", but with --add-text="Form label" after the
menu. Example:
    guid --forms \
         --add-menu="Item A;10|Item B;20" \
         --add-text="Form label" --bold)HEREDOC")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-password
        Help("--add-password=\"[hideLabel=true@]Password label\"",
             tr("Add a password entry in forms dialog")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-qr-code
        Help("--add-qr-code=\"[addLabel=QR code label@]QR Code text\"",
             tr(R"HEREDOC(Add a QR code in forms dialog.
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--align=left|center|right",
             tr("Set QR code alignment")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-scale
        Help("--add-scale=\"[hideLabel=true@]Scale label\"",
             tr("Add a scale (slider) in forms dialog")) <<
        Help("--value=VALUE",
             tr("Set initial value")) <<
        Help("--min-value=VALUE",
             tr("Set minimum value")) <<
        Help("--max-value=VALUE",
             tr("Set maximum value")) <<
        Help("--step=VALUE",
             tr("Set step size")) <<
        Help("--hide-value",
             tr("Hide value")) <<
        Help("--print-partial",
             tr("Print the value to the console each time it's changed by the user")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-spin-box
        Help("--add-spin-box=\"[hideLabel=true@]Spin box label\"",
             tr("Add a spin box in forms dialog")) <<
        Help("--value=VALUE",
             tr("Set initial value")) <<
        Help("--min-value=VALUE",
             tr("Set minimum value")) <<
        Help("--max-value=VALUE",
             tr("Set maximum value")) <<
        Help("--prefix=PREFIX",
             tr("Set prefix")) <<
        Help("--suffix=SUFFIX",
             tr("Set suffix")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-double-spin-box
        Help("--add-double-spin-box=\"[hideLabel=true@]Double spin box label\"",
             tr("Add a double spin box in forms dialog")) <<
        Help("--value=VALUE",
             tr("Set initial value")) <<
        Help("--decimals=VALUE",
             tr("Set the number of decimals")) <<
        Help("--min-value=VALUE",
             tr("Set minimum value")) <<
        Help("--max-value=VALUE",
             tr("Set maximum value")) <<
        Help("--prefix=PREFIX",
             tr("Set prefix")) <<
        Help("--suffix=SUFFIX",
             tr("Set suffix")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets. Note that a hidden widget is not a user input field,
so it doesn't appear in the console (no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--var=NAME",
             tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-text
        Help(R"HEREDOC(--add-text="[addLabel=Text label@][defMarkerVal1=Value@][image=Path to image@]
            [monitorMarkerFile1=Path to file@][monitorVarName1=Variable name@]Text")HEREDOC",
             tr(R"HEREDOC(Add text without field.
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.
To add an image to the left, add the variable "image=Path to the image". Example:
    guid --forms \
         --add-text="image=/path/to/image.png@Text displayed"
Default resources shipped with guid can be used. To do so, start the image path
with ":/" followed by the resource name. For example, to use a "warning" icon:
    guid --forms \
         --add-text="image=:/warning@Text displayed"
A marker named "GUID_MARKER_1" can be added in the text and it will be replaced by the file
content specified with the variable "monitorMarkerFile1=Path to file". Up to 9 markers can
be used (from GUID_MARKER_1 to GUID_MARKER_9). If the file content is empty, a default value
can be specified with the variable "defMarkerVal1=Value" (and "defMarkerVal2=Value",
"defMarkerVal3=Value", etc.). Example:
    guid --forms \
         --add-text="defMarkerVal1=?monitorMarkerFile1=/to/file.txt@Lorem GUID_MARKER_1 amet."
It's possible to monitor only part of the file. To do so, add the variable
"monitorVarName=Variable name", and only the value of the variable specified will be used
as content. For example, let's say we have a file "/to/settings.sh" with this content:
    name=Little Mouse
    age=2
    color=grey
We can replace the marker GUID_MARKER_1 with only the value of the variable "age" like this:
    guid --forms \
         --add-text="monitorMarkerFile1=/to/settings.sh@monitorVarName1=age@Age is GUID_MARKER_1.")HEREDOC")) <<
        Help("--tooltip=Tooltip text",
             tr("Set tooltip displayed when the cursor is over")) <<
        Help("--wrap",
             tr("Enable text wrapping")) <<
        Help("--align=left|center|right",
             tr("Set text alignment")) <<
        Help("--bold",
             tr("Set text in bold")) <<
        Help("--italics",
             tr("Set text in italics")) <<
        Help("--underline",
             tr("Set text format to underline")) <<
        Help("--small-caps",
             tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY",
             tr("Set font family")) <<
        Help("--font-size=SIZE",
             tr("Set font size")) <<
        Help("--foreground-color=COLOR",
             tr(R"HEREDOC(Set text color. Example:
    guid --forms \
         --add-text="Lorem ipsum" --foreground-color="#0000FF")HEREDOC")) <<
        Help("--background-color=COLOR",
             tr(R"HEREDOC(Set text background color. Example:
    guid --forms \
         --add-text="Lorem ipsum" --background-color="#0000FF")HEREDOC")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-hrule
        Help("--add-hrule=\"[addLabel=Rule label@]Rule color",
             tr(R"HEREDOC(Add a horizontal rule with the color specified. Example:
    guid --forms \
         --add-text="Lorem ipsum" \
         --add-hrule="#B1B1B1" \
         --add-entry="Text field"
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-vspacer
        Help("--add-vspacer=\"[addLabel=Vertical spacer label@]Height\"",
             tr(R"HEREDOC(Add a vertical spacer.
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("", "") <<
        
        // --add-text-info
        Help("--add-text-info=\"[hideLabel=true@]Text info label\"",
             tr(R"HEREDOC(Add text information.
Note that this widget is a user input field only when the argument "--editable"
is used. Otherwise, it's not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--filename=\"[monitor=true@]Path to file\"",
             tr("Get content from the specified file")) <<
        Help("--url=URL", tr("Get content from the specified URL (curl must be installed on the system)")) <<
        Help("--curl-path=\"Path to curl\"",
             tr("Set path to the curl binary (default is \"curl\")")) <<
        Help("--editable",
             tr("Allow the user to edit text")) <<
        Help("--plain",
             tr(R"HEREDOC(Force plain text (zenity default limitation), otherwise guid will try
to guess automatically the format)HEREDOC")) <<
        Help("--html",
             tr("Force HTML format, otherwise guid will try to guess automatically the format")) <<
        Help("--newline-separator=SEPARATOR",
             tr(R"HEREDOC(Replace newlines with SEPARATOR in the text printed to the console (so the
output is only on one line))HEREDOC")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--align=left|center|right",
             tr("Set text alignment")) <<
        Help("--bold",
             tr("Set text in bold")) <<
        Help("--italics",
             tr("Set text in italics")) <<
        Help("--underline",
             tr("Set text format to underline")) <<
        Help("--small-caps",
             tr("Set text rendering to small-caps type")) <<
        Help("--font-family=FAMILY",
             tr("Set font family")) <<
        Help("--font=TEXT",
             tr("Set the text font")) <<
        Help("--font-size=SIZE",
             tr("Set font size")) <<
        Help("--foreground-color=COLOR",
             tr(R"HEREDOC(Set text color. Example:
    guid --forms \
         --add-text-info="Text info label" --foreground-color="#0000FF" \
             --filename="/path/to/file.txt")HEREDOC")) <<
        Help("--background-color=COLOR",
             tr(R"HEREDOC(Set text background color. Example:
    guid --forms \
         --add-text-info="Text info label" --background-color="#0000FF" \
             --filaneme="/path/to/file.txt")HEREDOC")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("--var=NAME",
             tr("Only when the field is editable.") + "\n" + tr(formsVarHelp)) <<
        Help("", "") <<
        
        // --add-text-browser
        Help("--add-text-browser=\"[hideLabel=true@]Text browser label\"",
             tr(R"HEREDOC(Add read-only HTML information with click on links enabled.
Note that this widget is not a user input field, so it doesn't appear in the console
(no even as empty value) when user input is printed.)HEREDOC")) <<
        Help("--filename=/path/to/file",
             tr("Get content from the specified file")) <<
        Help("--url=URL", tr("Get content from the specified URL (curl must be installed on the system)")) <<
        Help("--curl-path=\"Path to curl\"",
             tr("Set path to the curl binary (default is \"curl\")")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--hide",
             tr(R"HEREDOC(Hide the widget but retain its size in the dialog.
Useful mainly for positioning other widgets.)HEREDOC")) <<
        Help("", "") <<
        
        // Window buttons
        Help("--win-min-button",
             tr("Add a \"Minimize\" button to the dialog window")) <<
        Help("--win-max-button",
             tr("Add a \"Maximize\" button to the dialog window")) <<
        Help("", "") <<
        
        // System tray
        Help("--close-to-systray",
             tr(R"HEREDOC(Hide the dialog (instead of exiting) when clicking the window "Close" button.
The system tray must be enabled with --systray-icon="Path to icon".)HEREDOC")) <<
        Help("--systray-icon=\"Path to icon\"",
             tr(R"HEREDOC(Add the icon specified in the system tray.
Clicking the "Close" window button will minimize the dialog in the systray, and a
menu will be displayed with a right-click on the systray icon.)HEREDOC")) <<
        Help("", "") <<
        
        // Dialog buttons
        Help(R"HEREDOC(--action-after-ok-click="[keepOpen=true@]
                         [valuesToFooter=true@][commandToFooter=true@]
                         [command=command name[<>command argument][<>another arg]")HEREDOC",
             tr(R"HEREDOC(Action after a click on the OK button.
No matter the options specified, values are always printed to the console. Default is
to exit. Variables that can be used are the following:
  - Set "keepOpen=true" to keep the forms dialog open (fields will be cleared).
  - Set "valuesToFooter=true" to add values to the dialog footer.
  - Set "commandToFooter=true" to add command output to the dialog footer.
  - Set "command" to run a command with values as input (values will also be printed
    to the console and fields will be cleared). Arguments must be separated with
    "<>". Actual values will be put where the variable/marker "GUID_VALUES" is added.
    If there's no variable/marker, values will be added at the end of the command.
    Example:
        guid --forms \
             --add-entry="Folder" \
             --action-after-ok-click="command=explorer.exe<>/separate,<>GUID_VALUES"
    To convert values to base64, use the variable/marker "GUID_VALUES_BASE64".
    Example:
        action="keepOpen=true"
        action+="@commandToFooter=true"
        action+="@command=myCommand"
        action+="<>myCommandArg1<>myCommandArg2<>GUID_VALUES_BASE64<>myCommandArg3"
        guid --forms \
             --add-entry="Folder" \
             --action-after-ok-click="$action"
    To convert values to base64 in a format suitable for URL, use the variable/marker
    "GUID_VALUES_BASE64_URL".)HEREDOC")) <<
        Help("--no-cancel",
             tr("Hide Cancel button")) <<
        Help("", "") <<
        
        // Footer
        Help("--footer-name=\"Footer name\"",
             tr("Name of the footer fieldset")) <<
        Help("--footer-entries=\"Number of entries\"",
             tr(R"HEREDOC(Number of entries to display in the footer (most recent entries are always displayed first).
Default is 3.)HEREDOC")) <<
        Help("--footer-from-file=\"[monitor=true@]Path to file\"",
             tr(R"HEREDOC(Use the file content as a source of footer entries.
To monitor file changes, add the variable "monitor=true".)HEREDOC")) <<
        Help("", "") <<
        
        // Misc.
        Help("--forms-date-format=PATTERN",
             tr("Set the format for the returned date")) <<
        Help("--forms-align=left|center|right",
             tr("Set label alignment for the entire form")) <<
        Help("--separator=SEPARATOR",
             tr("Set output separator character")) <<
        Help("--comment=COMMENT",
             tr(R"HEREDOC(Add comment for convenience in the command line arguments.
It'll be ignored when parsing arguments. Example:
    guid --forms \
         --text="Form with a tab bar" \
         --comment="--------------- TAB 1 ---------------" \
         --tab="Tab 1" \
             --add-entry="Text field in the tab 1" \
             --add-entry="Another text field in the tab 1" \
         --comment="--------------- TAB 2 ---------------" \
         --tab="Tab 2" \
             --add-spin-box="Spin box in the tab 2" --min-value=10 --value=50 \
         --tab="stop=true" \
         --comment="--------------- SCALE ---------------" \
         --add-scale="Field outside the tab bar" --step=5)HEREDOC")));
        
        /******************************
         * info
         ******************************/
        
        helpDict["info"] = CategoryHelp(tr("Info options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--ellipsize",
             tr("Do wrap text (zenity has a rather special problem here)")) <<
        Help("--no-markup",
             tr("Do not enable HTML markup")) <<
        Help("--no-wrap",
             tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON_NAME",
             tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels",
             tr("Allow to select text for copy and paste")));
        
        /******************************
         * list
         ******************************/
        
        helpDict["list"] = CategoryHelp(tr("List options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--align=left|center|right",
             tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--checklist",
             tr("Add checkboxes in the first column (multiple rows will be selectable)")) <<
        Help("--imagelist",
             tr("Use an image in the first column")) <<
        Help("--radiolist",
             tr("Add radio buttons in the first column (only one row will be selectable)")) <<
        Help("", "") <<
        
        Help("--column=\"Column name\"",
             tr("Add a column")) <<
        Help("--hide-column=NUMBER",
             tr("Hide a specific column")) <<
        Help("--print-column=NUMBER|all",
             tr(R"HEREDOC(Print a specific column to the console.
Default is "1" (print only the first column). The value "all" can be used to print
all columns.)HEREDOC")) <<
        Help("--hide-header",
             tr("Hide the column headers")) <<
        Help("", "") <<
        
        Help("--list-values-from-file=\"[monitor=true@][sep=Separator@]Path to file\"",
             tr(R"HEREDOC(Use the file content as list values.
To monitor file changes, add the variable "monitor=true". By default, the character
"|" is used as separator between values. To use another separator, specify it with
the variable "sep=Separator". Example:
    guid --list \
         --text="List label" \
         --column="Column 1" --column="Column 2" \
         --list-values-from-file="monitor=true@sep=,@/path/to/file")HEREDOC")) <<
        Help("", "") <<
        
        Help("--editable",
             tr("Allow the user to edit list values")) <<
        Help("--multiple",
             tr("Allow multiple rows to be selected")) <<
        Help("--no-selection",
             tr(R"HEREDOC(Prevent row selection (useful to display information in a structured way
without user interaction))HEREDOC")) <<
        Help("--print-values=selected|all",
             tr(R"HEREDOC(Print selected values (by default) or all values (useful in combination
with "--editable" to get updated values).)HEREDOC")) <<
        Help("", "") <<
        
        Help("--mid-search",
             tr(R"HEREDOC(Change list default search function searching for text in the middle, not at the
beginning)HEREDOC")) <<
        Help("--field-height=HEIGHT",
             tr("Set the field height")) <<
        Help("--separator=SEPARATOR",
             tr("Set output separator character")));
        
        /******************************
         * notification
         ******************************/
        
        helpDict["notification"] = CategoryHelp(tr("Notification icon options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--hint=TEXT",
             tr("Set the notification hints")) <<
        Help("", "") <<
        
        Help("--listen",
             tr("Listen for commands on stdin")) <<
        Help("--selectable-labels",
             tr("Allow to select text for copy and paste")));
        
        /******************************
         * password
         ******************************/
        
        helpDict["password"] = CategoryHelp(tr("Password dialog options"), HelpList() <<
        Help("--prompt=TEXT",
             tr("The prompt for the user (field label)")) <<
        Help("--username",
             tr("Add a username field")) <<
        Help("--field-width=WIDTH",
             tr("Set the field width")));
        
        /******************************
         * progress
         ******************************/
        
        helpDict["progress"] = CategoryHelp(tr("Progress options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("", "") <<
        
        Help("--percentage=PERCENTAGE",
             tr("Set initial percentage")) <<
        Help("--pulsate",
             tr("Pulsate progress bar")) <<
        Help("", "") <<
        
        Help("--auto-close",
             tr(R"HEREDOC(Dismiss the dialog when 100% has been reached)HEREDOC")) <<
        Help("--auto-kill",
             tr("Kill parent process if Cancel button is pressed")) <<
        Help("--no-cancel",
             tr("Hide Cancel button")));
        
        /******************************
         * question
         ******************************/
        
        helpDict["question"] = CategoryHelp(tr("Question options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--ellipsize",
             tr("Do wrap text (zenity has a rather special problem here)")) <<
        Help("--no-markup",
             tr("Do not enable HTML markup")) <<
        Help("--no-wrap",
             tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--default-cancel",
             tr("Give Cancel button focus by default")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON_NAME",
             tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels",
             tr("Allow to select text for copy and paste")));
        
        /******************************
         * scale
         ******************************/
        
        helpDict["scale"] = CategoryHelp(tr("Scale (slider) options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--align=left|center|right",
             tr("Set text alignment")) <<
        Help("", "") <<
        
        Help("--value=VALUE",
             tr("Set initial value")) <<
        Help("--min-value=VALUE",
             tr("Set minimum value")) <<
        Help("--max-value=VALUE",
             tr("Set maximum value")) <<
        Help("--step=VALUE",
             tr("Set step size")) <<
        Help("", "") <<
        
        Help("--hide-value",
             tr("Hide value")) <<
        Help("--print-partial",
             tr("Print the value to the console each time it's changed by the user")));
        
        /******************************
         * text-info
         ******************************/
        
        helpDict["text-info"] = CategoryHelp(tr("Text information options"), HelpList() <<
        Help("--filename=Path to file",
             tr("Get content from the specified file")) <<
        Help("", "") <<
        
        Help("--url=URL", tr("Get content from the specified URL (curl must be installed on the system)")) <<
        Help("--curl-path=\"Path to curl\"",
             tr("Set path to the curl binary (default is \"curl\")")) <<
        Help("", "") <<
        
        Help("--checkbox=TEXT",
             tr("Enable an I read and agree checkbox")) <<
        Help("", "") <<
        
        Help("--editable",
             tr("Allow the user to edit text")) <<
        Help("--font=TEXT",
             tr("Set the text font")) <<
        Help("--plain",
             tr(R"HEREDOC(Force plain text (zenity default limitation), otherwise guid will try
to guess automatically the format)HEREDOC")) <<
        Help("--html",
             tr("Force HTML format, otherwise guid will try to guess automatically the format")) <<
        Help("", "") <<
        
        Help("--auto-scroll",
             tr("Auto scroll the text to the end (only when text is captured from stdin)")) <<
        Help("--no-interaction",
             tr("Do not enable user interaction with the WebView (only when \"--html\" is used)")));
        
        /******************************
         * warning
         ******************************/
        
        helpDict["warning"] = CategoryHelp(tr("Warning options"), HelpList() <<
        Help("--text=TEXT",
             tr("Set the dialog text")) <<
        Help("--ellipsize",
             tr("Do wrap text (zenity has a rather special problem here)")) <<
        Help("--no-markup",
             tr("Do not enable HTML markup")) <<
        Help("--no-wrap",
             tr("Do not enable text wrapping")) <<
        Help("", "") <<
        
        Help("--icon-name=ICON_NAME",
             tr("Set the dialog icon")) <<
        Help("", "") <<
        
        Help("--selectable-labels",
             tr("Allow to select text for copy and paste")));
    }
    
    QStringList helpGeneralCats = {"help", "misc", "general", "application"}; 
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
                helpDescription = "    " + helpDescription;
                helpDescription.replace("\n", "\n    ");
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

void Guid::afterTabBarClick(int i)
{
    QDialog *dlg = static_cast<QDialog*>(m_dialog);
    if (dlg) {
        QPushButton *buttons = dlg->findChild<QPushButton*>();
        if (buttons) {
            bool disableButtons = false;
            QTabWidget *tabBar = static_cast<QTabWidget*>(sender());
            if (tabBar) {
                disableButtons = tabBar->widget(i)->property("guid_tab_disable_buttons").toBool();
            }
            buttons->setEnabled(!disableButtons);
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
        QStringList commandArgs = menuItemCommand.split("<>");
        QString commandExec = commandArgs[0];
        commandArgs.removeFirst();
        QProcess *process = new QProcess;
        
        bool guidShowMsg = false;
        QMessageBox *guidMsgBox = new QMessageBox();
        Qt::WindowFlags flags = guidMsgBox->windowFlags(); \
        flags |= Qt::WindowStaysOnTopHint; \
        guidMsgBox->setWindowFlags(flags);
        QString guidMsg = "";
        
        if (commandExec == "guidInfo" || commandExec == "guidWarning" || commandExec == "guidError") {
            guidShowMsg = true;
            guidMsgBox->setWindowTitle(menuItemName);
            guidMsgBox->setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::TextSelectableByMouse);
            
            if (commandExec == "guidInfo")
                guidMsgBox->setIcon(QMessageBox::Information);
            else if (commandExec == "guidWarning")
                guidMsgBox->setIcon(QMessageBox::Warning);
            else if (commandExec == "guidError")
                guidMsgBox->setIcon(QMessageBox::Critical);
            
            guidMsg = commandArgs.join("\n");
            guidMsgBox->setText(guidMsg);
        }
        
        if (menuItemCommandPrintOutput) {
            if (guidShowMsg) {
                qOut << guidMsg << "|MENU_CLICKED_DATA_END" << Qt::endl;
                guidMsgBox->show();
            } else {
                connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() {
                    QString commandOutput = QString::fromLocal8Bit(process->readAllStandardOutput());
                    QOUT
                    qOut << commandOutput << "|MENU_CLICKED_DATA_END";
                    delete process;
                });
                process->start(commandExec, commandArgs);
                qOut << Qt::endl;
            }
        } else if(guidShowMsg) {
            guidMsgBox->show();
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
                qOut << m_prefixOk + QLocale::system().toString(date, QLocale::ShortFormat) << Qt::endl;
            else
                qOut << m_prefixOk + date.toString(dateFormat) << Qt::endl;
            break;
        }
        case Entry: {
            QInputDialog *dlg = static_cast<QInputDialog*>(sender());
            if (dlg->inputMode() == QInputDialog::DoubleInput) {
                qOut << m_prefixOk + QLocale::c().toString(dlg->doubleValue(), 'f', 2) << Qt::endl;
            } else if (dlg->inputMode() == QInputDialog::IntInput) {
                qOut << m_prefixOk + dlg->intValue() << Qt::endl;
            } else {
                qOut << m_prefixOk + dlg->textValue() << Qt::endl;
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
            qOut << m_prefixOk + result << Qt::endl;
            break;
        }
        case FileSelection: {
            QStringList files = static_cast<QFileDialog*>(sender())->selectedFiles();
            qOut << m_prefixOk + files.join(sender()->property("guid_separator").toString()) << Qt::endl;
            break;
        }
        case ColorSelection: {
            QColorDialog *dlg = static_cast<QColorDialog*>(sender());
            qOut << m_prefixOk + dlg->selectedColor().name() << Qt::endl;
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
            qOut << m_prefixOk + font << Qt::endl;
            break;
        }
        case TextInfo: {
            QTextEdit *te = sender()->findChild<QTextEdit*>();
            if (te && !te->isReadOnly()) {
                qOut << m_prefixOk + te->toPlainText() << Qt::endl;
            }
            break;
        }
        case Scale: {
            QSlider *sld = sender()->findChild<QSlider*>();
            if (sld) {
                qOut << m_prefixOk + QString::number(sld->value()) << Qt::endl;
            }
            break;
        }
        case List: {
            QTreeWidget *tw = sender()->findChild<QTreeWidget*>();
            QStringList result;
            if (tw) {
                if (tw->selectionMode() == QAbstractItemView::NoSelection)
                    break;
                
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
            qOut << m_prefixOk + result.join(sender()->property("guid_separator").toString()) << Qt::endl;
            break;
        }
        case Forms: {
            printForms();
            break;
        }
        default:
            qOutErr << m_prefixErr + "unhandled output" << m_type << Qt::endl;
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

void Guid::listMenu(const QPoint &pos)
{
    QTreeWidget *tw = static_cast<QTreeWidget*>(sender());
    if (!tw)
        return;
    
    QTreeWidgetItem *twi = tw->itemAt(pos);
    if (!twi)
        return;
    
    QMenu *menu = new QMenu();
    
    QAction *actionCopy = new QAction(tr("Copy"), menu);
    QIcon actionIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    actionCopy->setIcon(actionIcon);
    connect(actionCopy, &QAction::triggered, [=]() {
        QString newClipContent = "";
        for (int i = 0; i < twi->columnCount(); ++i) {
            if (!newClipContent.isEmpty())
                newClipContent += ",";
            newClipContent += twi->text(i);
        }
        
        if (!newClipContent.isEmpty()) {
            QClipboard *clip = QGuiApplication::clipboard();
            clip->setText(newClipContent);
        }
    });
    
    menu->addAction(actionCopy);
    menu->exec(tw->viewport()->mapToGlobal(pos));
}

void Guid::minimizeDialog()
{
    QDialog *dlg = static_cast<QDialog*>(m_dialog);
    QSystemTrayIcon *sysTrayIcon = static_cast<QSystemTrayIcon*>(m_sysTray);
    if (dlg && sysTrayIcon) {
        if (!m_sysTrayMsg) {
            QMessageBox *msgBox = new QMessageBox(dlg);
            msgBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
            msgBox->setWindowTitle("Dialog in the system tray");
            msgBox->setIcon(QMessageBox::Information);
            msgBox->setText("The dialog will keep running in the system tray. You can click on its icon to open the dialog again or to close it.");
            m_sysTrayMsg = true;
        }
        dlg->hide();
        setSysTrayAction("Minimize", false);
        setSysTrayAction("Show", true);
    }
}

void Guid::printFormsAfterOKClick()
{
    QFileDialog *dialog = static_cast<QFileDialog*>(m_dialog);
    QGroupBox *footer = dialog->findChild<QGroupBox*>("dialogFooter", Qt::FindDirectChildrenOnly);
    bool ok;
    
    // Print current forms values
    QString values = printForms();
    if (m_okValuesToFooter && footer)
        updateFooterContent(footer, values);
    
    // Clear forms values
    
    QList<QLineEdit*> entries = dialog->findChildren<QLineEdit*>();
    foreach(QLineEdit *entry, entries) {
        entry->clear();
    }
    
    QList<QTextEdit*> textEntries = dialog->findChildren<QTextEdit*>();
    foreach(QTextEdit *textEntry, textEntries) {
        if (!textEntry->isReadOnly())
            textEntry->clear();
    }
    
    QList<QCheckBox*> cb = dialog->findChildren<QCheckBox*>();
    foreach(QCheckBox *cbi, cb) {
        if (cbi->property("guid_checkbox_default").toString() == "checked")
            cbi->setCheckState(Qt::Checked);
        else
            cbi->setCheckState(Qt::Unchecked);
    }
    
    QList<QComboBox*> combos = dialog->findChildren<QComboBox*>();
    foreach(QComboBox *combo, combos) {
        int defaultComboIndex = combo->property("guid_combo_default_index").toInt(&ok);
        if (ok && defaultComboIndex >= 0 && defaultComboIndex < combo->count())
            combo->setCurrentIndex(defaultComboIndex);
        else
            combo->setCurrentIndex(-1);
    }
    
    QList<QSlider*> scales = dialog->findChildren<QSlider*>();
    foreach(QSlider *scale, scales) {
        int defaultScaleValue = scale->property("guid_scale_default").toInt(&ok);
        if (ok && defaultScaleValue != INT_MIN)
            scale->setValue(defaultScaleValue);
        else
            scale->setValue(scale->minimum());
    }
    
    QList<QSpinBox*> spinBoxes = dialog->findChildren<QSpinBox*>();
    foreach(QSpinBox *spinBox, spinBoxes) {
        int defaultSpinBoxValue = spinBox->property("guid_spin_box_default").toInt(&ok);
        if (ok && defaultSpinBoxValue != INT_MIN)
            spinBox->setValue(defaultSpinBoxValue);
        else
            spinBox->setValue(spinBox->minimum());
    }
    
    QList<QDoubleSpinBox*> doubleSpinBoxes = dialog->findChildren<QDoubleSpinBox*>();
    foreach(QDoubleSpinBox *doubleSpinBox, doubleSpinBoxes) {
        int defaultDoubleSpinBoxValue = doubleSpinBox->property("guid_double_spin_box_default").toDouble(&ok);
        if (ok && defaultDoubleSpinBoxValue != -DBL_MAX)
            doubleSpinBox->setValue(defaultDoubleSpinBoxValue);
        else
            doubleSpinBox->setValue(doubleSpinBox->minimum());
    }
    
    QList<QTreeWidget*> tw = dialog->findChildren<QTreeWidget*>();
    foreach(QTreeWidget *twi, tw) {
        twi->clearSelection();
    }
    
    // Run command
    if (!m_okCommand.isEmpty()) {
        QString command = m_okCommand;
        if (command.contains("GUID_VALUES_BASE64_URL")) {
            values = values.toUtf8().toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
            command.replace("GUID_VALUES_BASE64_URL", values);
        } else if (command.contains("GUID_VALUES_BASE64")) {
            values = values.toUtf8().toBase64();
            command.replace("GUID_VALUES_BASE64", values);
        } else {
            command.replace("GUID_VALUES", values);
        }
        
        QStringList commandArgs = command.split("<>");
        
        QString commandExec = commandArgs.at(0);
        commandArgs.removeAt(0);
        QProcess *process = new QProcess;
        
        if (m_okCommandToFooter && footer) {
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() {
                QString commandOutput = QString::fromLocal8Bit(process->readAllStandardOutput());
                updateFooterContent(footer, commandOutput.trimmed());
                
                delete process;
            });
            process->start(commandExec, commandArgs);
        } else {
            process->startDetached(commandExec, commandArgs);
        }
    }
    
    if (!m_okKeepOpen)
        exitGuid();
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
                qOutErr << m_prefixErr + "'icon' command not yet supported - if you know what this is supposed to do, please file a bug" << Qt::endl;
            } else if (line.left(split) == "message" || line.left(split) == "tooltip") {
                userNeedsHelp = false;
                notify(line.mid(split+1));
            } else if (line.left(split) == "visible") {
                userNeedsHelp = false;
                if (m_dialog)
                    m_dialog->setVisible(line.mid(split+1).trimmed().compare("false", Qt::CaseInsensitive) &&
                                         line.mid(split+1).trimmed().compare("0", Qt::CaseInsensitive));
                else
                    qOutErr << m_prefixErr + "'visible' command only supported for failsafe dialog notification" << Qt::endl;
            } else if (line.left(split) == "hints") {
                m_notificationHints = line.mid(split+1);
            }
        }
        if (userNeedsHelp)
            qOutErr << m_prefixErr + "icon: <filename>\nmessage: <UTF-8 encoded text>\ntooltip: <UTF-8 encoded text>\nvisible: <true|false>" << Qt::endl;
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
            bool ok;
            int currentIndex = combo->property("guid_combo_default_index").toInt(&ok);
            if (ok && currentIndex > 0 && currentIndex < combo->count()) {
                combo->setCurrentIndex(currentIndex);
            }
        }
    }
}

void Guid::updateFooter(QString filePath)
{
    if (!QFile::exists(filePath))
        return;
    
    QDialog *dlg = static_cast<QDialog*>(m_dialog);
    if (!dlg)
        return;
    
    QGroupBox *footer = dlg->findChild<QGroupBox*>("dialogFooter", Qt::FindDirectChildrenOnly);
    if (!footer)
        return;
    
    updateFooterContentFromFile(footer, filePath);
}

void Guid::updateList(QString filePath)
{
    // The file containing list values has been updated. However, the way it was edited is not known.
    // Some editors delete the file (event "IN_DELETE_SELF") to replace it with new content, so the
    // watcher will stop monitoring the file. Here's the workaround: wait some time before testing if
    // the file exists, then add it again to the watcher.
    bool pathExists = pathTester(filePath);
    if (!pathExists)
        return;
    QFileSystemWatcher *watcher = static_cast<QFileSystemWatcher*>(sender());
    watcher->addPath(filePath);
    
    foreach (QTreeWidget *tw, watcher->parent()->findChildren<QTreeWidget*>()) {
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
                fileArg += "addValue=" + propAddValue + "@";
            if (!propFileSep.isEmpty())
                fileArg += "sep=" + propFileSep + "@";
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

void Guid::updateText(QString filePath)
{
    bool pathExists = pathTester(filePath);
    if (!pathExists)
        return;
    QFileSystemWatcher *watcher = static_cast<QFileSystemWatcher*>(sender());
    watcher->addPath(filePath);
    
    foreach (QLabel *l, watcher->parent()->findChildren<QLabel*>()) {
        for (int i = 1; i < 10; ++i) {
            QString propMarkerFile = "guid_text_monitor_marker_file_" + QString::number(i);
            if (l->property(propMarkerFile.toStdString().c_str()).toString() == filePath) {
                setText(l);
            }
        }
    }
}

void Guid::updateTextInfo(QString filePath)
{
    bool pathExists = pathTester(filePath);
    if (!pathExists)
        return;
    QFileSystemWatcher *watcher = static_cast<QFileSystemWatcher*>(sender());
    watcher->addPath(filePath);
    
    foreach (QTextEdit *ti, watcher->parent()->findChildren<QTextEdit*>()) {
        if (ti->property("guid_text_filename").toString() == filePath && ti->property("guid_text_monitor_file").toBool()) {
            setTextInfo(ti);
        }
    }
}

// End of "private slots"

/******************************************************************************
 * private (1 of 2): misc.
 ******************************************************************************/

void Guid::createQRCode(QLabel *label, QString text)
{
    qrcodegen::QrCode qrCode = qrcodegen::QrCode::encodeText(text.toUtf8().data(), qrcodegen::QrCode::Ecc::HIGH);
    qint32 qrCodeSize = qrCode.getSize();
    QImage qrCodeImage(qrCodeSize, qrCodeSize, QImage::Format_RGB32);
    QRgb colorBlack = qRgb(0, 0, 0);
    QRgb colorWhite = qRgb(255, 255, 255);
    for (int y = 0; y < qrCodeSize; ++y) {
        for (int x = 0; x < qrCodeSize; ++x) {
            qrCodeImage.setPixel(x, y, qrCode.getModule(x, y) ? colorBlack : colorWhite);
        }
    }
    label->setPixmap(QPixmap::fromImage(qrCodeImage.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation), Qt::MonoOnly));
}

bool Guid::error(const QString message)
{
    QOUT_ERR
    qOutErr << m_prefixErr + message << Qt::endl;
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

QString Guid::printForms()
{
    QOUT
    QFileDialog *dialog = static_cast<QFileDialog*>(m_dialog);
    QList<QFormLayout*> layouts = dialog->findChildren<QFormLayout*>();
    // We skip the first layout used for the top menu.
    QFormLayout *fl = layouts.at(1);
    QStringList resultList;
    ValuePair resultPair;
    QString dateFormat = dialog->property("guid_date_format").toString();
    QString separator = dialog->property("guid_separator").toString();
    QString listRowSeparator = dialog->property("guid_list_row_separator").toString();
    for (int i = 0; i < fl->count(); ++i) {
        if (QLayoutItem *li = fl->itemAt(i, QFormLayout::FieldRole)) {
            resultPair = getFormsWidgetValue(li->widget(), dateFormat, separator, listRowSeparator);
            if (resultPair.first)
                resultList << resultPair.second;
        }
    }
    QString result = m_prefixOk + resultList.join(separator);
    qOut << result << Qt::endl;
    return result;
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
        } else if (args.at(i) == "--always-on-top") {
            m_alwaysOnTop = true;
        } else if (args.at(i) == "--no-taskbar") {
            m_noTaskbar = true;
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

void Guid::updateFooterContent(QGroupBox *footer, QString newEntry)
{
    if (!footer || !(m_okCommandToFooter || m_okValuesToFooter))
        return;
    
    QFormLayout *footerLayout = static_cast<QFormLayout*>(footer->layout());
    if (!footerLayout)
        return;
    
    int nbEntriesToDisplay = footer->property("guid_footer_nb_entries").toInt();
    int footerHeight = footer->height();
    footer->setVisible(true);
    
    QLabel *newLabel = new QLabel();
    newLabel->setText(newEntry);
    newLabel->setWordWrap(true);
    newLabel->setTextInteractionFlags(newLabel->textInteractionFlags()|Qt::TextSelectableByMouse);
    
    footerLayout->insertRow(0, newLabel);
    
    int nbEntries = 0;
    for (int i = 0; i < footerLayout->count(); ++i) {
        nbEntries++;
        if (nbEntries <= nbEntriesToDisplay)
            continue;
        footerLayout->removeRow(nbEntries - 1);
    }
    
    int newFooterHeight = footer->height();
    if (newFooterHeight > footerHeight) {
        QFileDialog *dialog = static_cast<QFileDialog*>(m_dialog);
        if (dialog) {
            QSize dialogSize = dialog->size();
            dialogSize.setHeight(newFooterHeight - footerHeight);
            dialog->resize(dialogSize);
        }
    }
}

void Guid::updateFooterContentFromFile(QGroupBox *footer, QString filePath)
{
    if (!QFile::exists(filePath))
        return;
    
    QStringList newEntries;
    QFile file(filePath);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    if (file.open(QIODevice::ReadOnly)) {
        newEntries = QString::fromLocal8Bit(file.readAll()).trimmed().replace(QRegExp("[\r\n]+"), "\n").split("\n");
        file.close();
    }
    
    int nbNewEntries = newEntries.count();
    for (int i = nbNewEntries - 1; i >= 0; --i) {
        updateFooterContent(footer, newEntries.at(i));
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
            tll->addWidget(label);
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
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
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
    tll->addWidget(cal);
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
            qOutErr << m_prefixErr + "The show-palette parameter is not supported by guid. Sorry." << Qt::endl;
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
                    qOutErr << m_prefixErr + "Cannot read" << path.toLocal8Bit().constData() << Qt::endl;
                }
            } else {
                qOutErr << m_prefixErr + "You have to provide a gimp palette (*.gpl)" << Qt::endl;
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
    QString sample = "The quick brown fox jumps over the lazy dog.";
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
                qOutErr << m_prefixErr + "The output pattern doesn't include a placeholder for the font name..." << Qt::endl;
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
    QSettings guidQSsettings("guid");
    
    /**************************************
     * Container: Dialog: QDialog *dlg
     **************************************/
    
    NEW_DIALOG // *dlg, *tll
    
    dlg->setProperty("guid_separator", "|");
    dlg->setProperty("guid_list_row_separator", "~");
    
    Qt::WindowFlags dlgFlags = Qt::WindowCloseButtonHint;
    dlg->setWindowFlags(dlgFlags);
    
    const int wSpacing = 12; // Form widget spacing
    
    bool noCancelButton = false;
    
    QString sysTrayIconPath = "";
    
    FormsSettings formsSettings;
    
    /**************************************
     * Container: Top-level layout: QVBoxLayout *tll
     **************************************/
    
    tll->setContentsMargins(0, 0, 0, 0);
    tll->setSpacing(0);
    
    // 1. Form label
    
    QLabel *formLabel = new QLabel(dlg);
    formLabel->setVisible(false);
    formLabel->setContentsMargins(wSpacing, wSpacing, wSpacing, 0);
    
    bool formLabelInBold = true;
    
    tll->addWidget(formLabel);
    
    // 2. Top menu layout
    
    QFormLayout *tml = new QFormLayout();
    tml->setContentsMargins(0, 0, 0, 0);
    tml->setSpacing(wSpacing);
    
    tll->addLayout(tml);
    
    // 3. Header
    
    QWidget *header = new QWidget();
    header->setVisible(false);
    header->setContentsMargins(0, 0, 0, 0);
    header->setProperty("guid_header_container", true);
    
    QFormLayout *headerLayout = new QFormLayout();
    headerLayout->setContentsMargins(wSpacing, wSpacing, wSpacing, wSpacing);
    headerLayout->setSpacing(wSpacing);
    
    header->setLayout(headerLayout);
    
    QLabel *headerLabel = NULL;
    bool addingToHeader = false;
    
    tll->addWidget(header);
    
    // 4. Form layout
    
    QFormLayout *fl = new QFormLayout();
    fl->setContentsMargins(wSpacing, wSpacing, wSpacing, wSpacing);
    fl->setSpacing(wSpacing);
    
    tll->addLayout(fl);
    
    // 5. Footer
    
    QFormLayout *footerContainerLayout = new QFormLayout();
    footerContainerLayout->setContentsMargins(wSpacing, 0, wSpacing, wSpacing);
    footerContainerLayout->setSpacing(wSpacing);
    
    QGroupBox *footer = new QGroupBox(tr("Recent activity"));
    footer->setObjectName("dialogFooter");
    footer->setProperty("guid_footer_nb_entries", 3);
    footer->setProperty("guid_footer_file_path", "");
    footer->setProperty("guid_footer_monitor_file", false);
    footer->setVisible(false);
    
    QFileSystemWatcher *footerWatcher = new QFileSystemWatcher(dlg);
    
    QFormLayout *footerLayout = new QFormLayout();
    footerLayout->setContentsMargins(wSpacing, wSpacing, wSpacing, wSpacing);
    footerLayout->setSpacing(wSpacing);
    footer->setLayout(footerLayout);
    
    footerContainerLayout->addRow(footer);
    tll->addLayout(footerContainerLayout);
    
    // 6. Stretch
    
    tll->addStretch();
    
    /**************************************
     * Container: Group
     **************************************/
    
    QGroupBox *lastGroup = NULL;
    QLabel *lastGroupLabel = NULL;
    QFormLayout *lastGroupLayout = NULL;
    QString lastGroupName = "";
    
    /**************************************
     * Container: Tabs
     **************************************/
    
    QTabWidget *lastTabBar = new QTabWidget();
    lastTabBar->setProperty("guid_tab_bar_verbose", false);
    lastTabBar->setStyleSheet(QTABBAR_STYLE);
    
    QLabel *lastTabBarLabel = NULL;
    QFormLayout *lastTabLayout = NULL;
    QWidget *lastTab = NULL;
    QString lastTabName = "";
    int lastTabIndex = -1;
    
    /**************************************
     * Container: Columns
     **************************************/
    
    QString lastColumn = NULL;
    bool hideCol1Label = false;
    
    QWidget *columnsContainer = NULL;
    QHBoxLayout *columnsLayout = NULL;
    
    // Column 1
    Qt::AlignmentFlag col1VAlignFlag = Qt::AlignTop;
    QString col1HSpacer = "";
    QLabel *labelCol1 = NULL;
    
    // Column 2
    Qt::AlignmentFlag col2VAlignFlag = Qt::AlignTop;
    QString col2HSpacer = "";
    
    /**************************************
     * Widgets
     **************************************/
    
    QWidget *lastWidget = NULL;
    QString lastWidgetId = NULL;
    QString lastWidgetVar = "";
    
    // calendar
    QCalendarWidget *lastCalendar = NULL;
    QLabel *lastCalendarLabel = NULL;
    
    // checkbox
    QCheckBox *lastCheckbox = NULL;
    QLabel *lastCheckboxLabel = NULL;
    
    // combo
    QComboBox *lastCombo = NULL;
    QLabel *lastComboLabel = NULL;
    GList lastComboGList = GList();
    QFileSystemWatcher *comboWatcher = new QFileSystemWatcher(dlg);
    
    // entry
    QLineEdit *lastEntry = NULL;
    QLabel *lastEntryLabel = NULL;
    
    // file-sel
    QFileDialog *lastFileSel = NULL;
    QLabel *lastFileSelLabel = NULL;
    QPushButton *lastFileSelButton = NULL;
    QLineEdit *lastFileSelEntry = NULL;
    QHBoxLayout *lastFileSelLayout = NULL;
    QWidget *lastFileSelContainer = NULL;
    
    // hrule
    QLabel *lastHRule = NULL;
    QLabel *lastHRuleLabel = NULL;
    QString lastHRuleCss = NULL;
    
    // list
    QTreeWidget *lastList = NULL;
    QLabel *lastListLabel = NULL;
    QWidget *lastListContainer = NULL;
    QFormLayout *lastListLayout = NULL;
    QPushButton *lastListButton = NULL;
    GList lastListGList = GList();
    bool lastListHeader = false;
    Qt::ItemFlags lastListFlags;
    int lastListHeight = -1;
    QStringList lastListColumns;
    QFileSystemWatcher *listWatcher = new QFileSystemWatcher(dlg);
    
    // menu
    QMenuBar *lastMenu = NULL;
    QLabel *lastMenuLabel = NULL;
    QIcon menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_TitleBarMaxButton);
    bool lastMenuIsTopMenu = false;
    
    // password
    QLineEdit *lastPassword = NULL;
    QLabel *lastPasswordLabel = NULL;
    
    // qr-code
    
    QLabel *lastQRCodeContainer = NULL;
    QLabel *lastQRCodeLabel = NULL;
    
    // scale
    
    QSlider *lastScale = NULL;
    QLabel *lastScaleLabel = NULL;
    QLabel *lastScaleVal = NULL;
    
    QWidget *scaleContainer = NULL;
    QHBoxLayout *scaleHBoxLayout = NULL;
    
    // spin-box || double-spin-box
    QSpinBox *lastSpinBox = NULL;
    QLabel *lastSpinBoxLabel = NULL;
    QDoubleSpinBox *lastDoubleSpinBox = NULL;
    QLabel *lastDoubleSpinBoxLabel = NULL;
    
    // text
    QLabel *lastText = NULL;
    QLabel *lastTextLabel = NULL;
    QFileSystemWatcher *textWatcher = new QFileSystemWatcher(dlg);
    
    // text-info || text-browser
    
    QTextEdit *lastTextInfo = NULL;
    QLabel *lastTextInfoLabel = NULL;
    QTextBrowser *lastTextBrowser = NULL;
    QLabel *lastTextBrowserLabel = NULL;
    
    QFileSystemWatcher *textInfoWatcher = new QFileSystemWatcher(dlg);
    
    // vspacer
    QLabel *lastVSpacer = NULL;
    QLabel *lastVSpacerLabel = NULL;
    int lastVSpacerHeight = 0;
    
    /**************************************
     * Misc.
     **************************************/
    
    QString next_arg;
    QStringList next_arg_split;
    QStringList next_arg_join;
    WidgetSettings ws;
    bool ok;
    
    for (int i = 0; i < args.count(); ++i) {
        /********************************************************************************
         * WIDGET CONTAINERS
         ********************************************************************************/
        
        // --header
        if (args.at(i) == "--header") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (!ws.stop) {
                addingToHeader = true;
                formsSettings.hasHeader = true;
                header->setVisible(true);
                
                if (!ws.hideLabel && !headerLabel) {
                    headerLabel = new QLabel(next_arg);
                    headerLayout->addRow(headerLabel);
                }
                
                QString hcCssRules;
                if (!ws.backgroundColor.isEmpty())
                    hcCssRules += "background-color: " + ws.backgroundColor + ";";
                if (!ws.foregroundColor.isEmpty())
                    hcCssRules += "color: " + ws.foregroundColor + ";";
                if (!hcCssRules.isEmpty())
                    header->setStyleSheet(hcCssRules);
            } else {
                addingToHeader = false;
            }
            
            lastWidgetVar = "";
        }
        
        // --group
        else if (args.at(i) == "--group") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (ws.stop) {
                setGroup(lastGroup, fl, lastGroupLabel, lastGroupName);
            } else {
                lastGroupName = next_arg;
                lastGroup = new QGroupBox(lastGroupName);
                
                if (!ws.addLabel.isEmpty()) {
                    lastGroupLabel = new QLabel(ws.addLabel);
                } else {
                    lastGroupLabel = nullptr;
                }
                
                lastGroupLayout = new QFormLayout();
                lastGroupLayout->setContentsMargins(wSpacing, wSpacing, wSpacing, wSpacing);
                lastGroup->setLayout(lastGroupLayout);
            }
            
            lastWidgetVar = "";
        }
        
        // --tab
        else if (args.at(i) == "--tab") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (ws.stop) {
                setTabBar(lastTabBar, fl, lastTabBarLabel, lastTabName, lastTabIndex);
            } else if (lastTabName.isEmpty() || lastTabName != next_arg) {
                // The tab bar label can be set when adding the first tab.
                if (lastTabName.isEmpty() && !ws.addLabel.isEmpty()) {
                    lastTabBarLabel = new QLabel(ws.addLabel);
                } else {
                    lastTabBarLabel = nullptr;
                }
                
                lastTabIndex++;
                if (!next_arg.isEmpty())
                    lastTabName = next_arg;
                else
                    lastTabName = QString("Tab %1").arg(lastTabIndex);
                lastTab = new QWidget();
                lastTabLayout = new QFormLayout();
                
                lastTab->setLayout(lastTabLayout);
                lastTab->setProperty("guid_tab_disable_buttons", ws.disableButtons);
                
                if (ws.verboseTabBar)
                    lastTabBar->setProperty("guid_tab_bar_verbose", ws.verboseTabBar);
                
                lastTabBar->addTab(lastTab, lastTabName);
                connect(lastTabBar, SIGNAL(currentChanged(int)), this, SLOT(afterTabBarClick(int)), Qt::UniqueConnection);
            }
            
            lastWidgetVar = "";
        }
        
        // --col1
        else if (args.at(i) == "--col1") {
            lastColumn = "col1";
            columnsLayout = new QHBoxLayout();
            lastWidgetVar = "";
        }
        
        // --col2
        else if (args.at(i) == "--col2") {
            lastColumn = "col2";
            lastWidgetVar = "";
        }
        
        /********************************************************************************
         * WIDGETS
         ********************************************************************************/
        
        // QCalendarWidget: --add-calendar
        else if (args.at(i) == "--add-calendar") {
            SWITCH_FORM_WIDGET("calendar")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastCalendar = new QCalendarWidget(dlg);
            lastWidget = lastCalendar;
            lastCalendarLabel = new QLabel(next_arg);
            
            lastCalendar->setProperty("guid_hide", false);
            
            ADD_WIDGET_TO_FORM(lastCalendarLabel, lastCalendar)
        }
        
        // QCheckBox: --add-checkbox
        else if (args.at(i) == "--add-checkbox") {
            SWITCH_FORM_WIDGET("checkbox")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastCheckbox = new QCheckBox(next_arg, dlg);
            lastWidget = lastCheckbox;
            lastCheckboxLabel = new QLabel(ws.addLabel);
            
            lastCheckbox->setProperty("guid_checkbox_default", "unchecked");
            lastCheckbox->setProperty("guid_hide", false);
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            ADD_WIDGET_TO_FORM(lastCheckboxLabel, lastCheckbox)
        }
        
        // QLineEdit: --add-entry
        else if (args.at(i) == "--add-entry") {
            SWITCH_FORM_WIDGET("entry")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastEntry = new QLineEdit(dlg);
            lastWidget = lastEntry;
            lastEntryLabel = new QLabel(next_arg);
            
            lastEntry->setProperty("guid_hide", false);
            
            ADD_WIDGET_TO_FORM(lastEntryLabel, lastEntry)
        }
        
        // QFileDialog: --add-file-selection
        else if (args.at(i) == "--add-file-selection") {
            SWITCH_FORM_WIDGET("file-sel")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastFileSel = new QFileDialog();
            lastWidget = lastFileSel;
            lastFileSelLabel = new QLabel(next_arg);
            
            QString lastFileSelButtonText = tr("Select");
            if (!ws.buttonText.isEmpty())
                lastFileSelButtonText = ws.buttonText;
            lastFileSelButton = new QPushButton(lastFileSelButtonText, lastFileSelContainer);
            
            lastFileSelEntry = new QLineEdit(lastFileSelContainer);
            
            lastFileSelLayout = new QHBoxLayout();
            lastFileSelLayout->setContentsMargins(0, 0, 0, 0);
            lastFileSelLayout->addWidget(lastFileSelButton);
            lastFileSelLayout->addWidget(lastFileSelEntry);
            
            lastFileSelContainer = new QWidget();
            lastFileSelContainer->setProperty("guid_file_sel_container", true);
            lastFileSelContainer->setLayout(lastFileSelLayout);
            
            lastFileSel->setViewMode(guidQSsettings.value("FileDetails", false).toBool() ?
                                     QFileDialog::Detail : QFileDialog::List);
            lastFileSel->setFileMode(QFileDialog::ExistingFile);
            lastFileSel->setOption(QFileDialog::DontUseNativeDialog);
            lastFileSel->setFilter(QDir::AllDirs|QDir::AllEntries|QDir::Hidden|QDir::System);
            lastFileSel->setProperty("guid_file_sel_separator", dlg->property("guid_separator").toString());
            lastFileSel->setProperty("guid_hide", false);
            
            QVariantList guidBookmarksList = guidQSsettings.value("Bookmarks").toList();
            QList<QUrl> lastFileSelBookmarks;
            for (int j = 0; j < guidBookmarksList.count(); ++j)
                lastFileSelBookmarks << guidBookmarksList.at(j).toUrl();
            if (!lastFileSelBookmarks.isEmpty())
                lastFileSel->setSidebarUrls(lastFileSelBookmarks);
            
            QObject::connect(lastFileSelButton, &QPushButton::clicked, [=]() {
                if (lastFileSel->exec()) {
                    QStringList lastFileSelFiles = lastFileSel->selectedFiles();
                    QString lastFileSelEntryText = lastFileSelFiles.join(
                        lastFileSel->property("guid_file_sel_separator").toString()
                    );
                    lastFileSelEntry->setText(lastFileSelEntryText);
                }
            });
            
            ADD_WIDGET_TO_FORM(lastFileSelLabel, lastFileSelContainer)
        }
        
        // QMenuBar: --add-menu
        else if (args.at(i) == "--add-menu") {
            if (lastWidgetId.isEmpty() && lastColumn.isEmpty()) {
                lastMenuIsTopMenu = true;
                formsSettings.hasTopMenu = true;
            } else {
                lastMenuIsTopMenu = false;
            }
            
            SWITCH_FORM_WIDGET("menu")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastMenu = new QMenuBar();
            lastWidget = lastMenu;
            lastMenuLabel = new QLabel(ws.addLabel);
            
            lastMenu->setProperty("guid_hide", false);
            
            if (lastMenuIsTopMenu)
                lastMenu->setStyleSheet("background: white; border-top: 1px solid #F0F0F0;");
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            // Menu items
            
            QStringList menuItemsMainLevel = next_arg.split('|');
            QStringList menuItemChildren;
            
            QStringList menuItemData;
            QString menuItemName;
            int menuItemExitCode;
            QString menuItemCommand;
            bool menuItemCommandPrintOutput;
            QString menuItemIcon;
            
            for (int j = 0; j < menuItemsMainLevel.size(); ++j) {
                menuItemName = "";
                menuItemExitCode = -1;
                menuItemCommand = "";
                menuItemCommandPrintOutput = false;
                menuItemIcon = "";
                
                if (menuItemsMainLevel.at(j).contains('#')) {
                    menuItemData = menuItemsMainLevel.at(j).section('#', 0, 0).split(';');
                    menuItemChildren << menuItemsMainLevel.at(j).section('#', 1, -1).split('#');
                } else {
                    menuItemData = menuItemsMainLevel.at(j).split(';');
                }
                
                SET_FORMS_MENU_ITEM_DATA
                
                if (menuItemName.isEmpty())
                    continue;
                
                if (!ok)
                    menuItemExitCode = -1;
                
                if (!ws.sep.isEmpty() && j > 0) {
                    auto* menuSep = new QAction(ws.sep, this);
                    menuSep->setDisabled(true);
                    lastMenu->addAction(menuSep);
                }
                
                // Menu with submenu items (first level elements are added as QMenu)
                if (!menuItemChildren.isEmpty()) {
                    QMenu *menuItem = new QMenu(menuItemName);
                    lastMenu->addMenu(menuItem);
                    
                    for (int k = 0; k < menuItemChildren.size(); ++k) {
                        menuItemName = "";
                        menuItemExitCode = -1;
                        menuItemCommand = "";
                        menuItemCommandPrintOutput = false;
                        menuItemIcon = "";
                        
                        menuItemData = menuItemChildren.at(k).split(';');
                        SET_FORMS_MENU_ITEM_DATA
                        
                        if (menuItemName.isEmpty())
                            continue;
                        
                        if (!ok)
                            menuItemExitCode = -1;
                        
                        QAction *menuAction = new QAction(menuItemName, this);
                        
                        if (menuItemIcon != "false" || menuItemIcon != "0")
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
            
            ADD_WIDGET_TO_FORM(lastMenuLabel, lastMenu)
        }
        
        // QLineEdit: --add-password
        else if (args.at(i) == "--add-password") {
            SWITCH_FORM_WIDGET("password")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastPassword = new QLineEdit(dlg);
            lastWidget = lastPassword;
            lastPasswordLabel = new QLabel(next_arg);
            
            lastPassword->setEchoMode(QLineEdit::Password);
            lastPassword->setProperty("guid_hide", false);
            
            ADD_WIDGET_TO_FORM(lastPasswordLabel, lastPassword)
        }
        
        // QSpinBox: --add-spin-box
        else if (args.at(i) == "--add-spin-box") {
            SWITCH_FORM_WIDGET("spin-box")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastSpinBox = new QSpinBox();
            lastWidget = lastSpinBox;
            lastSpinBoxLabel = new QLabel(next_arg);
            
            lastSpinBox->setProperty("guid_hide", false);
            lastSpinBox->setProperty("guid_spin_box_default", INT_MIN);
            
            ADD_WIDGET_TO_FORM(lastSpinBoxLabel, lastSpinBox)
        }
        
        // QDoubleSpinBox: --add-double-spin-box
        else if (args.at(i) == "--add-double-spin-box") {
            SWITCH_FORM_WIDGET("double-spin-box")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastDoubleSpinBox = new QDoubleSpinBox();
            lastWidget = lastDoubleSpinBox;
            lastDoubleSpinBoxLabel = new QLabel(next_arg);
            
            QLocale dv_locale(QLocale::C);
            dv_locale.setNumberOptions(QLocale::RejectGroupSeparator);
            lastDoubleSpinBox->setLocale(dv_locale);
            
            lastDoubleSpinBox->setProperty("guid_hide", false);
            lastDoubleSpinBox->setProperty("guid_double_spin_box_default", -DBL_MAX);
            
            ADD_WIDGET_TO_FORM(lastDoubleSpinBoxLabel, lastDoubleSpinBox)
        }
        
        // QLabel: --add-text
        else if (args.at(i) == "--add-text") {
            SWITCH_FORM_WIDGET("text")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastText = new QLabel();
            lastWidget = lastText;
            lastTextLabel = new QLabel(ws.addLabel);
            
            lastText->setProperty("guid_hide", false);
            lastText->setProperty("guid_text_content", "");
            
            lastText->setProperty("guid_text_monitor_marker_file_1", "");
            lastText->setProperty("guid_text_monitor_marker_file_2", "");
            lastText->setProperty("guid_text_monitor_marker_file_3", "");
            lastText->setProperty("guid_text_monitor_marker_file_4", "");
            lastText->setProperty("guid_text_monitor_marker_file_5", "");
            lastText->setProperty("guid_text_monitor_marker_file_6", "");
            lastText->setProperty("guid_text_monitor_marker_file_7", "");
            lastText->setProperty("guid_text_monitor_marker_file_8", "");
            lastText->setProperty("guid_text_monitor_marker_file_9", "");
            
            lastText->setProperty("guid_text_monitor_var_name_1", "");
            lastText->setProperty("guid_text_monitor_var_name_2", "");
            lastText->setProperty("guid_text_monitor_var_name_3", "");
            lastText->setProperty("guid_text_monitor_var_name_4", "");
            lastText->setProperty("guid_text_monitor_var_name_5", "");
            lastText->setProperty("guid_text_monitor_var_name_6", "");
            lastText->setProperty("guid_text_monitor_var_name_7", "");
            lastText->setProperty("guid_text_monitor_var_name_8", "");
            lastText->setProperty("guid_text_monitor_var_name_9", "");
            
            lastText->setProperty("guid_text_def_marker_val_1", "");
            lastText->setProperty("guid_text_def_marker_val_2", "");
            lastText->setProperty("guid_text_def_marker_val_3", "");
            lastText->setProperty("guid_text_def_marker_val_4", "");
            lastText->setProperty("guid_text_def_marker_val_5", "");
            lastText->setProperty("guid_text_def_marker_val_6", "");
            lastText->setProperty("guid_text_def_marker_val_7", "");
            lastText->setProperty("guid_text_def_marker_val_8", "");
            lastText->setProperty("guid_text_def_marker_val_9", "");
            
            lastText->setProperty("guid_text_markers_set", false);
            
            lastText->setContentsMargins(0, 3, 0, 0);
            lastText->setTextInteractionFlags(lastText->textInteractionFlags()|Qt::TextSelectableByMouse);
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            QString lastTextContent = next_arg;
            if (ws.image.startsWith(":/") || QFile::exists(ws.image)) {
                lastTextContent = 
                    "<table><tr>"
                    "<td><img src=\"" + ws.image + "\" /></td>"
                    "<td style=\"padding-left: 5px; vertical-align: middle;\">" + next_arg + "</td>"
                    "</tr></table>";
            }
            lastText->setText(lastTextContent);
            
            if (!ws.monitorMarkerFile1.isEmpty() || !ws.monitorMarkerFile2.isEmpty() || !ws.monitorMarkerFile3.isEmpty() ||
                !ws.monitorMarkerFile4.isEmpty() || !ws.monitorMarkerFile5.isEmpty() || !ws.monitorMarkerFile6.isEmpty() ||
                !ws.monitorMarkerFile7.isEmpty() || !ws.monitorMarkerFile8.isEmpty() || !ws.monitorMarkerFile9.isEmpty()) {
                lastText->setProperty("guid_text_content", lastTextContent);
                
                if (!ws.monitorMarkerFile1.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_1", ws.monitorMarkerFile1);
                    if (QFile::exists(ws.monitorMarkerFile1)) {
                        textWatcher->addPath(ws.monitorMarkerFile1);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName1.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_1", ws.monitorVarName1);
                    
                    if (!ws.defMarkerVal1.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_1", ws.defMarkerVal1);
                }
                
                if (!ws.monitorMarkerFile2.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_2", ws.monitorMarkerFile2);
                    if (QFile::exists(ws.monitorMarkerFile2)) {
                        textWatcher->addPath(ws.monitorMarkerFile2);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName2.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_2", ws.monitorVarName2);
                    
                    if (!ws.defMarkerVal2.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_2", ws.defMarkerVal2);
                }
                
                if (!ws.monitorMarkerFile3.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_3", ws.monitorMarkerFile3);
                    if (QFile::exists(ws.monitorMarkerFile3)) {
                        textWatcher->addPath(ws.monitorMarkerFile3);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName3.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_3", ws.monitorVarName3);
                    
                    if (!ws.defMarkerVal3.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_3", ws.defMarkerVal3);
                }
                
                if (!ws.monitorMarkerFile4.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_4", ws.monitorMarkerFile4);
                    if (QFile::exists(ws.monitorMarkerFile4)) {
                        textWatcher->addPath(ws.monitorMarkerFile4);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName4.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_4", ws.monitorVarName4);
                    
                    if (!ws.defMarkerVal4.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_4", ws.defMarkerVal4);
                }
                
                if (!ws.monitorMarkerFile5.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_5", ws.monitorMarkerFile5);
                    if (QFile::exists(ws.monitorMarkerFile5)) {
                        textWatcher->addPath(ws.monitorMarkerFile5);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName5.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_5", ws.monitorVarName5);
                    
                    if (!ws.defMarkerVal5.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_5", ws.defMarkerVal5);
                }
                
                if (!ws.monitorMarkerFile6.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_6", ws.monitorMarkerFile6);
                    if (QFile::exists(ws.monitorMarkerFile6)) {
                        textWatcher->addPath(ws.monitorMarkerFile6);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName6.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_6", ws.monitorVarName6);
                    
                    if (!ws.defMarkerVal6.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_6", ws.defMarkerVal6);
                }
                
                if (!ws.monitorMarkerFile7.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_7", ws.monitorMarkerFile7);
                    if (QFile::exists(ws.monitorMarkerFile7)) {
                        textWatcher->addPath(ws.monitorMarkerFile7);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName7.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_7", ws.monitorVarName7);
                    
                    if (!ws.defMarkerVal7.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_7", ws.defMarkerVal7);
                }
                
                if (!ws.monitorMarkerFile8.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_8", ws.monitorMarkerFile8);
                    if (QFile::exists(ws.monitorMarkerFile8)) {
                        textWatcher->addPath(ws.monitorMarkerFile8);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName8.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_8", ws.monitorVarName8);
                    
                    if (!ws.defMarkerVal8.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_8", ws.defMarkerVal8);
                }
                
                if (!ws.monitorMarkerFile9.isEmpty()) {
                    lastText->setProperty("guid_text_monitor_marker_file_9", ws.monitorMarkerFile9);
                    if (QFile::exists(ws.monitorMarkerFile9)) {
                        textWatcher->addPath(ws.monitorMarkerFile9);
                        connect(textWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateText(QString)), Qt::UniqueConnection);
                    }
                    
                    if (!ws.monitorVarName9.isEmpty())
                        lastText->setProperty("guid_text_monitor_var_name_9", ws.monitorVarName9);
                    
                    if (!ws.defMarkerVal9.isEmpty())
                        lastText->setProperty("guid_text_def_marker_val_9", ws.defMarkerVal9);
                }
                
                setText(lastText);
            }
            
            ADD_WIDGET_TO_FORM(lastTextLabel, lastText)
        }
        
        // QLabel: --add-hrule
        else if (args.at(i) == "--add-hrule") {
            SWITCH_FORM_WIDGET("hrule")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastHRule = new QLabel();
            lastWidget = lastHRule;
            lastHRuleLabel = new QLabel(ws.addLabel);
            
            lastHRule->setProperty("guid_hide", false);
            
            lastHRuleCss = QString("color: " + next_arg + ";");
            lastHRule->setContentsMargins(0, 0, 0, 0);
            lastHRule->setFrameShape(QFrame::HLine);
            lastHRule->setStyleSheet(lastHRuleCss);
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            ADD_WIDGET_TO_FORM(lastHRuleLabel, lastHRule)
        }
        
        // QLabel: --add-vspacer
        else if (args.at(i) == "--add-vspacer") {
            SWITCH_FORM_WIDGET("vspacer")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastVSpacer = new QLabel();
            lastWidget = lastVSpacer;
            lastVSpacerLabel = new QLabel(ws.addLabel);
            
            lastVSpacerHeight = next_arg.toInt();
            lastVSpacerHeight -= wSpacing;
            if (lastVSpacerHeight < 0)
                lastVSpacerHeight = 0;
            lastVSpacer->setFixedHeight(lastVSpacerHeight);
            lastVSpacer->setContentsMargins(0, 0, 0, 0);
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            ADD_WIDGET_TO_FORM(lastVSpacerLabel, lastVSpacer)
        }
        
        // QTextEdit: --add-text-info
        else if (args.at(i) == "--add-text-info") {
            SWITCH_FORM_WIDGET("text-info")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastTextInfo = new QTextEdit();
            lastWidget = lastTextInfo;
            lastTextInfoLabel = new QLabel(next_arg);
            
            lastTextInfo->setTextInteractionFlags(Qt::TextEditorInteraction);
            lastTextInfo->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            lastTextInfo->setProperty("guid_text_info_nsep", "");
            lastTextInfo->setProperty("guid_text_read_only", true);
            lastTextInfo->setProperty("guid_text_format", "guess");
            lastTextInfo->setProperty("guid_text_curl_path", "");
            lastTextInfo->setProperty("guid_text_filename", "");
            lastTextInfo->setProperty("guid_text_monitor_file", false);
            lastTextInfo->setProperty("guid_text_is_url", false);
            lastTextInfo->setProperty("guid_text_height", -1);
            lastTextInfo->setProperty("guid_hide", false);
            
            ADD_WIDGET_TO_FORM(lastTextInfoLabel, lastTextInfo)
        }
        
        // QTextBrowser: --add-text-browser
        else if (args.at(i) == "--add-text-browser") {
            SWITCH_FORM_WIDGET("text-browser")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastTextBrowser = new QTextBrowser();
            lastWidget = lastTextBrowser;
            lastTextBrowserLabel = new QLabel(next_arg);
            
            lastTextBrowser->setOpenLinks(true);
            lastTextBrowser->setOpenExternalLinks(true);
            lastTextBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction);
            lastTextBrowser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            lastTextBrowser->setProperty("guid_text_read_only", true);
            lastTextBrowser->setProperty("guid_text_format", "html");
            lastTextBrowser->setProperty("guid_text_curl_path", "");
            lastTextBrowser->setProperty("guid_text_filename", "");
            lastTextBrowser->setProperty("guid_text_monitor_file", false); // Not supported yet
            lastTextBrowser->setProperty("guid_text_is_url", false);
            lastTextBrowser->setProperty("guid_text_height", -1);
            lastTextBrowser->setProperty("guid_hide", false);
            
            ADD_WIDGET_TO_FORM(lastTextBrowserLabel, lastTextBrowser)
        }
        
        // QComboBox: --add-combo
        else if (args.at(i) == "--add-combo") {
            SWITCH_FORM_WIDGET("combo")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastCombo = new QComboBox(dlg);
            lastWidget = lastCombo;
            lastComboLabel = new QLabel(next_arg);
            
            lastCombo->setProperty("guid_hide", false);
            lastCombo->setProperty("guid_file_sep", "");
            lastCombo->setProperty("guid_file_path", "");
            lastCombo->setProperty("guid_monitor_file", false);
            lastCombo->setProperty("guid_combo_default_index", 0);
            
            ADD_WIDGET_TO_FORM(lastComboLabel, lastCombo)
            
            lastCombo->addItems(lastComboGList.val);
        }
        
        // QTreeWidget: --add-list
        else if (args.at(i) == "--add-list") {
            SWITCH_FORM_WIDGET("list")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            buildFormsList(&lastList, lastListGList, lastListColumns, lastListHeader, lastListFlags, lastListHeight);
            
            lastList = new QTreeWidget(dlg);
            lastWidget = lastList;
            lastListLabel = new QLabel(next_arg);
            
            lastList->setContextMenuPolicy(Qt::CustomContextMenu);
            lastList->setAutoScroll(false);
            connect(lastList, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(listMenu(const QPoint &)));
            
            lastListButton = new QPushButton();
            lastListLayout = new QFormLayout();
            lastListContainer = new QWidget();
            
            lastList->setProperty("guid_hide", false);
            lastList->setProperty("guid_file_sep", "");
            lastList->setProperty("guid_file_path", "");
            lastList->setProperty("guid_monitor_file", false);
            lastList->setProperty("guid_list_add_value", "");
            lastList->setProperty("guid_list_print_values_mode", "selected");
            lastList->setProperty("guid_list_selection_type", "");
            lastList->setProperty("guid_list_print_column", "1");
            lastList->setProperty("guid_list_read_only_column", -1);
            lastList->setProperty("guid_list_exclude_from_output", false);
            lastList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
            lastList->header()->setStretchLastSection(true);
            
            lastListLayout->addRow(lastList);
            lastListLayout->setSpacing(0);
            lastListLayout->setContentsMargins(0, 0, 0, 0);
            
            if (ws.addNewRowButton) {
                lastListButton = new QPushButton(tr("Add row"), lastList);
                connect(lastListButton, SIGNAL(clicked()), this, SLOT(addListRow()), Qt::UniqueConnection);
                
                lastListLayout->addRow(lastListButton);
                lastListLayout->setAlignment(lastListButton, Qt::AlignLeft);
            }
            
            lastList->setProperty("guid_list_exclude_from_output", ws.excludeFromOutput);
            
            lastListContainer->setLayout(lastListLayout);
            lastListContainer->setProperty("guid_list_container", true);
            
            ADD_WIDGET_TO_FORM(lastListLabel, lastListContainer)
        }
        
        // QrCode: --add-qr-code
        else if (args.at(i) == "--add-qr-code") {
            SWITCH_FORM_WIDGET("qr-code")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastQRCodeContainer = new QLabel();
            lastWidget = lastQRCodeContainer;
            lastQRCodeLabel = new QLabel(ws.addLabel);
            
            lastQRCodeContainer->setProperty("guid_hide", false);
            
            if (ws.addLabel.isEmpty())
                ws.hideLabel = true;
            
            createQRCode(lastQRCodeContainer, next_arg);
            
            ADD_WIDGET_TO_FORM(lastQRCodeLabel, lastQRCodeContainer)
        }
        
        // QSlider: --add-scale
        else if (args.at(i) == "--add-scale") {
            SWITCH_FORM_WIDGET("scale")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastScale = new QSlider(Qt::Horizontal, dlg);
            lastWidget = lastScale;
            lastScaleLabel = new QLabel(next_arg);
            
            lastScale->setProperty("guid_hide", false);
            lastScale->setProperty("guid_scale_default", INT_MIN);
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
            
            ADD_WIDGET_TO_FORM(lastScaleLabel, scaleContainer)
        }
        
        // QLabel: --text (form label)
        else if (args.at(i) == "--text") {
            SWITCH_FORM_WIDGET("form-label")
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            lastWidget = nullptr;
            
            formLabel->setText(labelText(next_arg));
            formLabel->setVisible(true);
            
            formsSettings.hasLabel = true;
            formsSettings.hasTopMenu = false;
        }
        
        /********************************************************************************
         * CONTAINER SETTINGS
         ********************************************************************************/
        
        /******************************
         * col1 || col2
         ******************************/
        
        // --add-hspacer
        else if (args.at(i) == "--add-hspacer") {
            next_arg = NEXT_ARG;
            QString hSpacer = next_arg;
            if (lastColumn == "col1") {
                col1HSpacer = hSpacer;
            } else if (lastColumn == "col2") {
                col2HSpacer = hSpacer;
            } else {
                WARN_UNKNOWN_ARG("--col1");
            }
        }
        
        // --valign
        else if (args.at(i) == "--valign") {
            next_arg = NEXT_ARG;
            if (lastColumn == "col1" || lastColumn == "col2") {
                QString alignment = next_arg;
                Qt::AlignmentFlag colVAlignFlag;
                if (alignment == "top") {
                    colVAlignFlag = Qt::AlignTop;
                } else if (alignment == "center") {
                    colVAlignFlag = Qt::AlignVCenter;
                } else if (alignment == "bottom") {
                    colVAlignFlag = Qt::AlignBottom;
                } else if (alignment == "baseline") {
                    colVAlignFlag = Qt::AlignBaseline;
                } else {
                    qOutErr << m_prefixErr + "argument --valign: unknown value " << alignment << Qt::endl;
                }
                
                if (lastColumn == "col1") {
                    col1VAlignFlag = colVAlignFlag;
                } else if (lastColumn == "col2") {
                    col2VAlignFlag = colVAlignFlag;
                }
            } else {
                WARN_UNKNOWN_ARG("--col1");
            }
        }
        
        /******************************
         * tab
         ******************************/
        
        // --tab-visible
        else if (args.at(i) == "--tab-visible") {
            if (!lastTabName.isEmpty()) {
                lastTabBar->setCurrentIndex(lastTabIndex);
            } else {
                WARN_UNKNOWN_ARG("--tab");
            }
        }
        
        /********************************************************************************
         * WIDGET SETTINGS
         ********************************************************************************/
        
        /******************************
         * calendar || checkbox || entry || file-sel || menu || password || spin-box || double-spin-box ||
         * qr-code || scale || combo || list || text || hrule || text-info || text-browser
         ******************************/
        
        // --hide
        else if (args.at(i) == "--hide") {
            if (lastWidgetId == "calendar" || lastWidgetId == "checkbox" || lastWidgetId == "entry" ||
                lastWidgetId == "file-sel" || lastWidgetId == "menu" || lastWidgetId == "password" ||
                lastWidgetId == "spin-box" || lastWidgetId == "double-spin-box" || lastWidgetId == "qr-code" ||
                lastWidgetId == "scale" || lastWidgetId == "combo" || lastWidgetId == "list" || lastWidgetId == "text" ||
                lastWidgetId == "hrule" || lastWidgetId == "text-info" || lastWidgetId == "text-browser") {
                QSizePolicy hideSizePolicy = lastWidget->sizePolicy();
                hideSizePolicy.setRetainSizeWhenHidden(true);
                lastWidget->setSizePolicy(hideSizePolicy);
                lastWidget->setProperty("guid_hide", true);
                lastWidget->hide();
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * checkbox
         ******************************/
        
        // --checked
        else if (args.at(i) == "--checked") {
            if (lastWidgetId == "checkbox") {
                lastCheckbox->setCheckState(Qt::Checked);
                lastCheckbox->setProperty("guid_checkbox_default", "checked");
            } else {
                WARN_UNKNOWN_ARG("--add-checkbox");
            }
        }
        
        /******************************
         * calendar || checkbox || entry || password || spin-box || double-spin-box || scale || combo || list || text-info
         ******************************/
        
        // --var
        else if (args.at(i) == "--var") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "calendar" || lastWidgetId == "checkbox" || lastWidgetId == "entry" ||
                lastWidgetId == "password" || lastWidgetId == "spin-box" || lastWidgetId == "double-spin-box" ||
                lastWidgetId == "scale" || lastWidgetId == "combo" || lastWidgetId == "list" || lastWidgetId == "text-info") {
                lastWidgetVar = next_arg;
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * entry
         ******************************/
        
        // --int
        else if (args.at(i) == "--int") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "entry") {
                lastEntry->setValidator(new QIntValidator(INT_MIN, INT_MAX, this));
                lastEntry->setText(QString::number(next_arg.toInt()));
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        // --float
        else if (args.at(i) == "--float") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "entry") {
                QLocale dv_locale(QLocale::C);
                dv_locale.setNumberOptions(QLocale::RejectGroupSeparator);
                
                QDoubleValidator *dv = new QDoubleValidator(DBL_MIN, DBL_MAX, 2, this);
                dv->setNotation(QDoubleValidator::StandardNotation);
                dv->setLocale(dv_locale);
                
                lastEntry->setValidator(dv);
                lastEntry->setText(QString::number(next_arg.toDouble(), 'f', 2));
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * entry || text || hrule || password || spin-box || double-spin-box || combo || text-browser || text-info
         ******************************/
        
        // --field-width
        else if (args.at(i) == "--field-width") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "entry") {
                lastEntry->setMaximumWidth(next_arg.toInt());
            } else if (lastWidgetId == "text") {
                lastText->setMaximumWidth(next_arg.toInt());
                lastText->setWordWrap(true);
            } else if (lastWidgetId == "hrule") {
                lastHRule->setMaximumWidth(next_arg.toInt());
            } else if (lastWidgetId == "password") {
                lastPassword->setMaximumWidth(next_arg.toInt());
            } else if (lastWidgetId == "spin-box") {
                lastSpinBox->setFixedWidth(next_arg.toInt());
            } else if (lastWidgetId == "double-spin-box") {
                lastDoubleSpinBox->setFixedWidth(next_arg.toInt());
            } else if (lastWidgetId == "combo") {
                lastCombo->setFixedWidth(next_arg.toInt());
            } else if (lastWidgetId == "list") {
                lastList->setFixedWidth(next_arg.toInt());
            } else if (lastWidgetId == "text-browser") {
                lastTextBrowser->setFixedWidth(next_arg.toInt());
            } else if (lastWidgetId == "text-info") {
                lastTextInfo->setFixedWidth(next_arg.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-entry");
            }
        }
        
        /******************************
         * spin-box || double-spin-box
         ******************************/
        
        // --prefix
        else if (args.at(i) == "--prefix") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "spin-box")
                lastSpinBox->setPrefix(next_arg);
            else if (lastWidgetId == "double-spin-box")
                lastDoubleSpinBox->setPrefix(next_arg);
            else
                WARN_UNKNOWN_ARG("--add-spin-box");
        }
        
        // --suffix
        else if (args.at(i) == "--suffix") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "spin-box")
                lastSpinBox->setSuffix(next_arg);
            else if (lastWidgetId == "double-spin-box")
                lastDoubleSpinBox->setSuffix(next_arg);
            else
                WARN_UNKNOWN_ARG("--add-spin-box");
        }
        
        /******************************
         * double-spin-box
         ******************************/
        
        // --decimals
        else if (args.at(i) == "--decimals") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "double-spin-box")
                lastDoubleSpinBox->setDecimals(next_arg.toInt());
            else
                WARN_UNKNOWN_ARG("--add-double-spin-box");
        }
        
        /******************************
         * spin-box || double-spin-box || scale
         ******************************/
        
        // --value
        else if (args.at(i) == "--value") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "spin-box") {
                lastSpinBox->setValue(next_arg.toInt());
                lastSpinBox->setProperty("guid_spin_box_default", next_arg.toInt());
            } else if (lastWidgetId == "double-spin-box") {
                lastDoubleSpinBox->setValue(next_arg.toDouble());
                lastDoubleSpinBox->setProperty("guid_double_spin_box_default", next_arg.toDouble());
            } else if (lastWidgetId == "scale") {
                lastScale->setValue(next_arg.toInt());
                lastScale->setProperty("guid_scale_default", next_arg.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        // --min-value
        else if (args.at(i) == "--min-value") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "spin-box") {
                lastSpinBox->setMinimum(next_arg.toInt());
            } else if (lastWidgetId == "double-spin-box") {
                lastDoubleSpinBox->setMinimum(next_arg.toDouble());
            } else if (lastWidgetId == "scale") {
                lastScale->setMinimum(next_arg.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        // --max-value
        else if (args.at(i) == "--max-value") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "spin-box") {
                lastSpinBox->setMaximum(next_arg.toInt());
            } else if (lastWidgetId == "double-spin-box") {
                lastDoubleSpinBox->setMaximum(next_arg.toDouble());
            } else if (lastWidgetId == "scale") {
                lastScale->setMaximum(next_arg.toInt());
            } else {
                WARN_UNKNOWN_ARG("--add-spin-box");
            }
        }
        
        /******************************
         * scale
         ******************************/
        
        // --step
        else if (args.at(i) == "--step") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "scale")
                lastScale->setSingleStep(next_arg.toInt());
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        // --print-partial
        else if (args.at(i) == "--print-partial") {
            if (lastWidgetId == "scale")
                connect(lastScale, SIGNAL(valueChanged(int)), SLOT(printInteger(int)));
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        // --hide-value
        else if (args.at(i) == "--hide-value") {
            if (lastWidgetId == "scale")
                lastScaleVal->hide();
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
        /******************************
         * combo
         ******************************/
        
        // --combo-values
        else if (args.at(i) == "--combo-values") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (lastWidgetId == "combo") {
                lastComboGList.val = next_arg.split('|');
                lastCombo->addItems(lastComboGList.val);
                if (ws.defaultIndex > 0 && ws.defaultIndex < lastCombo->count()) {
                    lastCombo->setCurrentIndex(ws.defaultIndex);
                    lastCombo->setProperty("guid_combo_default_index", ws.defaultIndex);
                }
            } else {
                WARN_UNKNOWN_ARG("--add-combo");
            }
        }
        
        // --combo-values-from-file
        else if (args.at(i) == "--combo-values-from-file") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (lastWidgetId == "combo") {
                lastComboGList = listValuesFromFile(next_arg);
                
                lastCombo->setProperty("guid_file_sep", lastComboGList.fileSep);
                lastCombo->setProperty("guid_file_path", lastComboGList.filePath);
                lastCombo->setProperty("guid_monitor_file", lastComboGList.monitorFile);
                
                if (QFile::exists(lastComboGList.filePath)) {
                    comboWatcher->addPath(lastComboGList.filePath);
                    connect(comboWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateCombo(QString)), Qt::UniqueConnection);
                }
                
                lastCombo->addItems(lastComboGList.val);
                if (ws.defaultIndex > 0 && ws.defaultIndex < lastCombo->count()) {
                    lastCombo->setCurrentIndex(ws.defaultIndex);
                    lastCombo->setProperty("guid_combo_default_index", ws.defaultIndex);
                }
            } else {
                WARN_UNKNOWN_ARG("--add-combo");
            }
        }
        
        /******************************
         * combo || list || text-info
         ******************************/
        
        // --editable
        else if (args.at(i) == "--editable") {
            if (lastWidgetId == "list")
                lastListFlags |= Qt::ItemIsEditable;
            else if (lastWidgetId == "combo")
                lastCombo->setEditable(true);
            else if (lastWidgetId == "text-info")
                lastTextInfo->setProperty("guid_text_read_only", false);
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        /******************************
         * list || text || hrule || text-browser || text-info
         ******************************/
        
        // --field-height
        else if (args.at(i) == "--field-height") {
            next_arg = NEXT_ARG;
            int fieldHeight = next_arg.toInt(&ok);
            if (!ok || fieldHeight < 0)
                fieldHeight = -1;
            
            if (lastWidgetId == "list") {
                lastListHeight = fieldHeight;
            } else if (lastWidgetId == "text") {
                lastText->setFixedHeight(fieldHeight);
            } else if (lastWidgetId == "hrule") {
                lastHRule->setFixedHeight(fieldHeight);
            } else if (lastWidgetId == "text-browser" || lastWidgetId == "text-info") {
                if (lastWidgetId == "text-browser")
                    lastTextBrowser->setProperty("guid_text_height", fieldHeight);
                else if (lastWidgetId == "text-info")
                    lastTextInfo->setProperty("guid_text_height", fieldHeight);
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        /******************************
         * list
         ******************************/
        
        // --column-values
        else if (args.at(i) == "--column-values") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list")
                lastListColumns = next_arg.split('|');
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --print-column
        else if (args.at(i) == "--print-column") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list")
                lastList->setProperty("guid_list_print_column", next_arg.toLower());
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --list-values
        else if (args.at(i) == "--list-values") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list")
                lastListGList.val = next_arg.split('|');
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --list-values-from-file
        else if (args.at(i) == "--list-values-from-file") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list") {
                lastListGList = listValuesFromFile(next_arg);
                
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
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list")
                lastList->setProperty("guid_list_print_values_mode", next_arg.toLower());
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --checklist
        else if (args.at(i) == "--checklist") {
            if (lastWidgetId == "list")
                lastList->setProperty("guid_list_selection_type", "checklist");
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --radiolist
        else if (args.at(i) == "--radiolist") {
            if (lastWidgetId == "list")
                lastList->setProperty("guid_list_selection_type", "radiolist");
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --no-selection
        else if (args.at(i) == "--no-selection") {
            if (lastWidgetId == "list") {
                lastList->setSelectionMode(QAbstractItemView::NoSelection);
                lastList->setFocusPolicy(Qt::NoFocus);
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        // --read-only-column
        else if (args.at(i) == "--read-only-column") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "list") {
                int roColumnNumber = next_arg.toInt(&ok);
                if (!ok)
                    roColumnNumber = -1;
                lastList->setProperty("guid_list_read_only_column", roColumnNumber);
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        // --show-header
        else if (args.at(i) == "--show-header") {
            if (lastWidgetId == "list")
                lastListHeader = true;
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        /******************************
         * list || file-sel
         ******************************/
        
        // --multiple
        else if (args.at(i) == "--multiple") {
            if (lastWidgetId == "list") {
                lastList->setSelectionMode(QAbstractItemView::ExtendedSelection);
            } else if (lastWidgetId == "file-sel") {
                lastFileSel->setFileMode(QFileDialog::ExistingFiles);
            } else {
                WARN_UNKNOWN_ARG("--add-list");
            }
        }
        
        /******************************
         * file-sel
         ******************************/
        
        // --directory
        else if (args.at(i) == "--directory") {
            if (lastWidgetId == "file-sel") {
                lastFileSel->setFileMode(QFileDialog::Directory);
                lastFileSel->setOption(QFileDialog::ShowDirsOnly);
            } else {
                WARN_UNKNOWN_ARG("--add-file-selection");
            }
        }
        
        // --file-filter
        else if (args.at(i) == "--file-filter") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "file-sel") {
                QStringList lastFileSelMimeFilters;
                QString lastFileSelMimeFilter = next_arg;
                const int lastFileSelIdx = lastFileSelMimeFilter.indexOf('|');
                if (lastFileSelIdx > -1) {
                    lastFileSelMimeFilter = lastFileSelMimeFilter.left(lastFileSelIdx).trimmed() +
                                            " (" + lastFileSelMimeFilter.mid(lastFileSelIdx + 1).trimmed() +
                                            ")";
                }
                lastFileSelMimeFilters << lastFileSelMimeFilter;
                lastFileSel->setNameFilters(lastFileSelMimeFilters);
            } else {
                WARN_UNKNOWN_ARG("--add-file-selection");
            }
        }
        
        // --file-separator
        else if (args.at(i) == "--file-separator") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "file-sel") {
                lastFileSel->setProperty("guid_file_sel_separator", next_arg);
            } else {
                WARN_UNKNOWN_ARG("--add-file-selection");
            }
        }
        
        /******************************
         * form-label
         ******************************/
        
        // --no-bold
        else if (args.at(i) == "--no-bold") {
            formLabelInBold = false;
        }
        
        /******************************
         * text
         ******************************/
        
        // --tooltip
        else if (args.at(i) == "--tooltip") {
            next_arg = NEXT_ARG;
            lastText->setToolTip(next_arg);
        }
        
        // --wrap
        else if (args.at(i) == "--wrap") {
            lastText->setWordWrap(true);
        }
        
        /******************************
         * form-label || qr-code || text || text-info
         ******************************/
        
        // --align || --bold || --italics || --underline || --small-caps || --font-family || --font-size ||
        // --foreground-color || --background-color
        else if (args.at(i) == "--align" || args.at(i) == "--bold" || args.at(i) == "--italics" ||
                 args.at(i) == "--underline" || args.at(i) == "--small-caps" || args.at(i) == "--font-family" ||
                 args.at(i) == "--font-size" || args.at(i) == "--foreground-color" || args.at(i) == "--background-color") {
            QLabel *labelToSet = NULL;
            if (lastWidgetId == "form-label") {
                labelToSet = formLabel;
            } else if (lastWidgetId == "text") {
                labelToSet = lastText;
            }
            
            // labelToSet
            if (labelToSet) {
                QFont fontToSet = labelToSet->font();
                
                if (args.at(i) == "--align") {
                    next_arg = NEXT_ARG;
                    QString alignment = next_arg;
                    if (alignment == "left") {
                        labelToSet->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        labelToSet->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        labelToSet->setAlignment(Qt::AlignRight);
                    } else {
                        qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
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
                    next_arg = NEXT_ARG;
                    fontToSet.setFamily(next_arg);
                    labelToSet->setFont(fontToSet);
                } else if (args.at(i) == "--font-size") {
                    next_arg = NEXT_ARG;
                    int fontSize = next_arg.toInt(&ok);
                    if (ok) {
                        fontToSet.setPointSize(fontSize);
                        labelToSet->setFont(fontToSet);
                    }
                } else if (args.at(i) == "--foreground-color") {
                    next_arg = NEXT_ARG;
                    QString foregroundColor = next_arg;
                    if (foregroundColor.left(1) != "#") {
                        foregroundColor = "#" + foregroundColor;
                    }
                    
                    QColor *color = new QColor(0, 0, 0);
                    color->setNamedColor(foregroundColor);
                    
                    QPalette labelPalette = labelToSet->palette();
                    labelPalette.setColor(QPalette::WindowText, *color);
                    
                    labelToSet->setPalette(labelPalette);
                } else if (args.at(i) == "--background-color") {
                    next_arg = NEXT_ARG;
                    QString backgroundColor = next_arg;
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
            else if (lastWidgetId == "text-info") {
                if (args.at(i) == "--align") {
                    next_arg = NEXT_ARG;
                    QString alignment = next_arg;
                    if (alignment == "left") {
                        lastTextInfo->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        lastTextInfo->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        lastTextInfo->setAlignment(Qt::AlignRight);
                    } else {
                        qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
                    }
                } else if (args.at(i) == "--bold") {
                    lastTextInfo->setFontWeight(QFont::Bold);
                } else if (args.at(i) == "--italics") {
                    lastTextInfo->setFontItalic(true);
                } else if (args.at(i) == "--underline") {
                    lastTextInfo->setFontUnderline(true);
                } else if (args.at(i) == "--font-family") {
                    next_arg = NEXT_ARG;
                    lastTextInfo->setFontFamily(next_arg);
                } else if (args.at(i) == "--font-size") {
                    next_arg = NEXT_ARG;
                    int fontSize = next_arg.toDouble(&ok);
                    if (ok) {
                        lastTextInfo->setFontPointSize(fontSize);
                    }
                } else if (args.at(i) == "--foreground-color") {
                    next_arg = NEXT_ARG;
                    QString foregroundColor = next_arg;
                    if (foregroundColor.left(1) != "#") {
                        foregroundColor = "#" + foregroundColor;
                    }
                    
                    QColor color = QColor(0, 0, 0);
                    color.setNamedColor(foregroundColor);
                    lastTextInfo->setTextColor(color);
                } else if (args.at(i) == "--background-color") {
                    next_arg = NEXT_ARG;
                    QString backgroundColor = next_arg;
                    if (backgroundColor.left(1) != "#") {
                        backgroundColor = "#" + backgroundColor;
                    }
                    QColor color = QColor(0, 0, 0);
                    color.setNamedColor(backgroundColor);
                    lastTextInfo->setTextBackgroundColor(color);
                }
            }
            
            // qr-code
            else if (lastWidgetId == "qr-code") {
                if (args.at(i) == "--align") {
                    next_arg = NEXT_ARG;
                    QString alignment = next_arg;
                    if (alignment == "left") {
                        lastQRCodeContainer->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        lastQRCodeContainer->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        lastQRCodeContainer->setAlignment(Qt::AlignRight);
                    } else {
                        qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
                    }
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
            next_arg = NEXT_ARG;
            if (lastWidgetId == "text-info") {
                lastTextInfo->setFont(QFont(next_arg));
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --html
        else if (args.at(i) == "--html") {
            if (lastWidgetId == "text-info") {
                lastTextInfo->setProperty("guid_text_format", "html");
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --plain
        else if (args.at(i) == "--plain") {
            if (lastWidgetId == "text-info") {
                lastTextInfo->setProperty("guid_text_format", "plain");
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        // --newline-separator
        else if (args.at(i) == "--newline-separator") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "text-info") {
                lastTextInfo->setProperty("guid_text_info_nsep", next_arg);
            } else {
                WARN_UNKNOWN_ARG("--add-text-info");
            }
        }
        
        /******************************
         * text-browser || text-info
         ******************************/
        
        // --url
        else if (args.at(i) == "--url") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "text-browser") {
                lastTextBrowser->setProperty("guid_text_filename", next_arg);
                lastTextBrowser->setProperty("guid_text_is_url", true);
            } else if (lastWidgetId == "text-info") {
                lastTextInfo->setProperty("guid_text_filename", next_arg);
                lastTextInfo->setProperty("guid_text_is_url", true);
            } else {
                WARN_UNKNOWN_ARG("--text-info");
            }
        }
        
        // --curl-path
        else if (args.at(i) == "--curl-path") {
            next_arg = NEXT_ARG;
            if (lastWidgetId == "text-browser")
                lastTextBrowser->setProperty("guid_text_curl_path", next_arg);
            else if (lastWidgetId == "text-info")
                lastTextInfo->setProperty("guid_text_curl_path", next_arg);
            else
                WARN_UNKNOWN_ARG("--text-info");
        }
        
        /******************************
         * text-browser || text-info || file-sel
         ******************************/
        
        // --filename
        else if (args.at(i) == "--filename") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (lastWidgetId == "text-browser") {
                lastTextBrowser->setProperty("guid_text_filename", next_arg);
            } else if (lastWidgetId == "text-info") {
                lastTextInfo->setProperty("guid_text_filename", next_arg);
                if (ws.monitorFile) {
                    lastTextInfo->setProperty("guid_text_monitor_file", true);
                    if (QFile::exists(next_arg)) {
                        textInfoWatcher->addPath(next_arg);
                        connect(textInfoWatcher, SIGNAL(fileChanged(QString)),
                                this, SLOT(updateTextInfo(QString)), Qt::UniqueConnection);
                    }
                }
            } else if (lastWidgetId == "file-sel") {
                QString lastFileSelPath = next_arg;
                if (lastFileSelPath.endsWith("/."))
                    lastFileSel->setDirectory(lastFileSelPath);
                else
                    lastFileSel->selectFile(lastFileSelPath);
            } else {
                WARN_UNKNOWN_ARG("--text-info");
            }
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
        
        // --action-after-ok-click
        else if (args.at(i) == "--action-after-ok-click") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            m_okCommand = ws.command;
            m_okCommandToFooter = ws.commandToFooter;
            m_okKeepOpen = ws.keepOpen;
            m_okValuesToFooter = ws.valuesToFooter;
            
            if (m_okCommand.isEmpty())
                m_okCommandToFooter = false;
            else if (!m_okCommand.contains(QRegExp("\bGUID_VALUES(_BASE64)?\b")))
                m_okCommand += "<>GUID_VALUES";
            
            if (m_okCommandToFooter)
                m_okValuesToFooter = false;
            
            if (!m_okKeepOpen) {
                m_okCommandToFooter = false;
                m_okValuesToFooter = false;
            }
        }
        
        // --no-cancel
        else if (args.at(i) == "--no-cancel") {
            noCancelButton = true;
        }
        
        // --close-to-systray
        else if (args.at(i) == "--close-to-systray") {
            m_closeToSysTray = true;
        }
        
        // --systray-icon
        else if (args.at(i) == "--systray-icon") {
            next_arg = NEXT_ARG;
            sysTrayIconPath = next_arg;
        }
        
        // --footer-name
        else if (args.at(i) == "--footer-name") {
            next_arg = NEXT_ARG;
            footer->setTitle(next_arg);
        }
        
        // --footer-entries
        else if (args.at(i) == "--footer-entries") {
            next_arg = NEXT_ARG;
            int nbFooterEntries = next_arg.toInt(&ok);
            if (ok && nbFooterEntries > 0)
                footer->setProperty("guid_footer_nb_entries", nbFooterEntries);
        }
        
        // --footer-from-file
        else if (args.at(i) == "--footer-from-file") {
            next_arg = NEXT_ARG;
            SET_WIDGET_SETTINGS(next_arg)
            
            if (QFile::exists(next_arg)) {
                footer->setProperty("guid_footer_file_path", next_arg);
                updateFooterContentFromFile(footer, next_arg);
                
                if (ws.monitorFile) {
                    footer->setProperty("guid_footer_monitor_file", true);
                    footerWatcher->addPath(next_arg);
                    connect(footerWatcher, SIGNAL(fileChanged(QString)), this, SLOT(updateFooter(QString)), Qt::UniqueConnection);
                }
            }
        }
        
        // --forms-date-format
        else if (args.at(i) == "--forms-date-format") {
            next_arg = NEXT_ARG;
            dlg->setProperty("guid_date_format", next_arg);
        }
        
        // --forms-align
        else if (args.at(i) == "--forms-align") {
            next_arg = NEXT_ARG;
            QString alignment = next_arg;
            if (alignment == "left") {
                fl->setLabelAlignment(Qt::AlignLeft);
            } else if (alignment == "center") {
                fl->setLabelAlignment(Qt::AlignCenter);
            } else if (alignment == "right") {
                fl->setLabelAlignment(Qt::AlignRight);
            } else {
                qOutErr << m_prefixErr + "argument --forms-align: unknown value" << args.at(i) << Qt::endl;
            }
        }
        
        // --separator
        else if (args.at(i) == "--separator") {
            next_arg = NEXT_ARG;
            dlg->setProperty("guid_separator", next_arg);
        }
        
        // --list-row-separator
        else if (args.at(i) == "--list-row-separator") {
            next_arg = NEXT_ARG;
            dlg->setProperty("guid_list_row_separator", next_arg);
        }
        
        // --comment
        else if (args.at(i) == "--comment") {
            next_arg = NEXT_ARG;
        }
        
        // else
        else {
            WARN_UNKNOWN_ARG("--forms");
        }
        
        lastComboGList = GList();
    }
    
    SWITCH_FORM_WIDGET(lastWidgetId)
    if (!lastGroupName.isEmpty())
        setGroup(lastGroup, fl, lastGroupLabel, lastGroupName);
    if (!lastTabName.isEmpty())
        setTabBar(lastTabBar, fl, lastTabBarLabel, lastTabName, lastTabIndex);
    buildFormsList(&lastList, lastListGList, lastListColumns, lastListHeader, lastListFlags, lastListHeight);
    
    if (formLabelInBold) {
        QFont mainLabelFont = formLabel->font();
        mainLabelFont.setBold(true);
        formLabel->setFont(mainLabelFont);
    }
    
    if (formsSettings.hasLabel && formsSettings.hasHeader) {
        formLabel->setContentsMargins(wSpacing, wSpacing, wSpacing, wSpacing);
    }
    
    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    btns->setContentsMargins(wSpacing, 0, wSpacing, wSpacing);
    
    if (noCancelButton)
        btns->button(QDialogButtonBox::Cancel)->hide();
    
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
    tll->addWidget(lbl = new QLabel(dlg));

    QTreeWidget *tw;
    tll->addWidget(tw = new QTreeWidget(dlg));
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
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
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
                tll->addWidget(filter = new QLineEdit(dlg));
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
            qOutErr << m_prefixErr + "unspecific argument" << args.at(i) << Qt::endl;
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
            tll->addWidget(new QLabel(tr("Enter username"), dlg));
            tll->addWidget(username = new QLineEdit(dlg));
            username->setObjectName("guid_username");
            break;
        } else if (args.at(i) == "--prompt") {
            prompt = NEXT_ARG;
        } { WARN_UNKNOWN_ARG("--password") }
    }

    tll->addWidget(new QLabel(prompt, dlg));
    tll->addWidget(password = new QLineEdit(dlg));
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

    tll->addWidget(lbl = new QLabel("Enter a value", dlg));
    tll->addLayout(hl);
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
                    qOutErr << m_prefixErr + "argument --align: unknown value" << args.at(i) << Qt::endl;
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
    tll->addWidget(te = new QTextBrowser(dlg));
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
            tll->addWidget(cb = new QCheckBox(NEXT_ARG, dlg));
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

// vim:set et sw=4 ts=4