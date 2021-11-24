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
#include <QFontDialog>
#include <QFormLayout>
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
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSignalMapper>
#include <QSlider>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QStringBuilder>
#include <QStringList>
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

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    #define SKIP_EMPTY Qt::SkipEmptyParts
#else
    #define SKIP_EMPTY QString::SkipEmptyParts
#endif

class InputGuard : public QObject
{
public:
    InputGuard() : QObject(), m_guardedWidget(NULL), m_checkTimer(0) {}
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
    QWidget *m_guardedWidget;
    int m_checkTimer;
    static InputGuard *s_instance;
};

InputGuard *InputGuard::s_instance = NULL;

#ifdef WS_X11
#include <QX11Info>
#include <X11/Xlib.h>
#endif

typedef QPair<QString, QString> Help;
typedef QList<Help> HelpList;
typedef QPair<QString, HelpList> CategoryHelp;
typedef QMap<QString, CategoryHelp> HelpDict;

Guid::Guid(int &argc, char **argv) : QApplication(argc, argv)
, m_modal(false)
, m_selectableLabel(false)
, m_parentWindow(0)
, m_timeout(0)
, m_notificationId(0)
, m_dialog(NULL)
, m_type(Invalid)
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
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
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

bool Guid::error(const QString message)
{
    QTextStream qOut(stdout);
    qOut.setCodec("UTF-8");
    
    qOut << "Error:" << message;
    QMetaObject::invokeMethod(this, "quitOnError", Qt::QueuedConnection);
    return true;
}

#define IF_IS(_TYPE_) if (const _TYPE_ *t = qobject_cast<const _TYPE_*>(w))

static QString value(const QWidget *w, const QString &pattern)
{
    if (!w)
        return QString();

    IF_IS(QLineEdit) {
        return t->text();
    } else IF_IS(QTreeWidget) {
        QString selected_items;
        QString selected_row;
        foreach (QTreeWidgetItem *item, t->selectedItems()) {
            selected_row = "";
            for (int i = 0; i < t->columnCount(); ++i) {
                if (!selected_row.isEmpty())
                    selected_row += ',';
                selected_row += item->text(i);
            }
            if (!selected_items.isEmpty())
                selected_items += w->property("guid_list_row_separator").toString();
            selected_items += selected_row;
        }
        return selected_items;
    } else IF_IS(QComboBox) {
        return t->currentText();
    } else IF_IS(QCalendarWidget) {
        if (pattern.isNull())
            return QLocale::system().toString(t->selectedDate(), QLocale::ShortFormat);
        return t->selectedDate().toString(pattern);
    } else IF_IS(QCheckBox) {
        return t->isChecked() ? "true" : "false";
    } else IF_IS(QSlider) {
        return QString::number(t->value());
    } else IF_IS(QSpinBox) {
        return QString::number(t->value());
    } else IF_IS(QWidget) {
        QString widgets_value;
        QString widget_value;
        if (t->property("guid_cols_container").toBool() || t->property("guid_scale_container").toBool()) {
            QList<QWidget*> wChildren = t->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            foreach(QWidget *widget, wChildren) {
                widget_value = value(widget, pattern);
                if (qstrcmp(widget->metaObject()->className(), "QLabel") == 0)
                    continue;
                if (!widgets_value.isEmpty())
                    widgets_value += t->property("guid_separator").toString();
                widgets_value += widget_value;
            }
        }
        return widgets_value;
    }
    return QString();
}

void Guid::dialogFinished(int status)
{
    QTextStream qOut(stdout);
    qOut.setCodec("UTF-8");
    
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
        exit(1);
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
            QString format = sender()->property("guid_date_format").toString();
            QDate date = sender()->findChild<QCalendarWidget*>()->selectedDate();
            if (format.isEmpty())
                qOut << QLocale::system().toString(date, QLocale::ShortFormat);
            else
                qOut << date.toString(format);
            break;
        }
        case Entry: {
            QInputDialog *dlg = static_cast<QInputDialog*>(sender());
            if (dlg->inputMode() == QInputDialog::DoubleInput) {
                qOut << QLocale::c().toString(dlg->doubleValue(), 'f', 2);
            } else if (dlg->inputMode() == QInputDialog::IntInput) {
                qOut << dlg->intValue();
            } else {
                qOut << dlg->textValue();
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
            qOut << result;
            break;
        }
        case FileSelection: {
            QStringList files = static_cast<QFileDialog*>(sender())->selectedFiles();
            qOut << files.join(sender()->property("guid_separator").toString());
            break;
        }
        case ColorSelection: {
            QColorDialog *dlg = static_cast<QColorDialog*>(sender());
            qOut << dlg->selectedColor().name();
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
            qOut << font;
            break;
        }
        case TextInfo: {
            QTextEdit *te = sender()->findChild<QTextEdit*>();
            if (te && !te->isReadOnly()) {
                qOut << te->toPlainText();
            }
            break;
        }
        case Scale: {
            QSlider *sld = sender()->findChild<QSlider*>();
            if (sld) {
                qOut << QString::number(sld->value());
            }
            break;
        }
        case List: {
            QTreeWidget *tw = sender()->findChild<QTreeWidget*>();
            QStringList result;
            if (tw) {
                bool done(false);
                QString selected;
                foreach (const QTreeWidgetItem *twi, tw->selectedItems()) {
                    done = true;
                    selected = "";
                    for (int i = 0; i < tw->columnCount(); ++i) {
                        if (sender()->property("guid_print_column") == "ALL" || sender()->property("guid_print_column") == i + 1) {
                            if (!selected.isEmpty())
                                selected += ',';
                            selected += twi->text(i);
                        }
                    }
                    result << selected;
                }
                if (!done) { // checkable
                    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                        const QTreeWidgetItem *twi = tw->topLevelItem(i);
                        if (twi->checkState(0) == Qt::Checked) {
                            selected = "";
                            for (int i = 0; i < tw->columnCount(); ++i) {
                                if (sender()->property("guid_print_column") == "ALL" || sender()->property("guid_print_column") == i + 1) {
                                    if (!selected.isEmpty())
                                        selected += ',';
                                    selected += twi->text(i);
                                }
                            }
                            result << selected;
                        }
                    }
                }
            }
            qOut << result.join(sender()->property("guid_separator").toString());
            break;
        }
        case Forms: {
            QList<QFormLayout*> layouts = sender()->findChildren<QFormLayout*>();
            // We skip the first layout used for the top menu
            QFormLayout *fl = layouts.at(1);
            QStringList result;
            QString format = sender()->property("guid_date_format").toString();
            for (int i = 0; i < fl->count(); ++i) {
                if (QLayoutItem *li = fl->itemAt(i, QFormLayout::FieldRole)) {
                    li->widget()->setProperty("guid_list_row_separator", sender()->property("guid_list_row_separator").toString());
                    result << value(li->widget(), format);
                }
            }
            qOut << result.join(sender()->property("guid_separator").toString());
            break;
        }
        default:
            qDebug() << "unhandled output" << m_type;
            break;
    }
    exit (0);
}

void Guid::exitAfterMenuClick(int i)
{
    showMenuClick(i);
    
    exit(i);
}

void Guid::showMenuClick(int i)
{
    qInfo() << "MENU CLICKED:" << i;
}

void Guid::quitOnError()
{
    exit(1);
}

#define NEXT_ARG QString((++i < args.count()) ? args.at(i) : QString())
#define WARN_UNKNOWN_ARG(_KNOWN_) if (args.at(i).startsWith("--") && args.at(i) != _KNOWN_) qDebug() << "unspecific argument" << args.at(i);
#define SHOW_DIALOG m_dialog = dlg; connect(dlg, SIGNAL(finished(int)), SLOT(dialogFinished(int))); dlg->show();

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
            QTimer::singleShot(t*1000, this, SLOT(quit()));
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
        } else {
            remains << args.at(i);
        }
    }
    args = remains;
    return true;
}

#define NEW_DIALOG QDialog *dlg = new QDialog; QVBoxLayout *vl = new QVBoxLayout(dlg);
#define FINISH_DIALOG(_BTNS_)   QDialogButtonBox *btns = new QDialogButtonBox(_BTNS_, Qt::Horizontal, dlg);\
                                vl->addWidget(btns);\
                                connect(btns, SIGNAL(accepted()), dlg, SLOT(accept()));\
                                connect(btns, SIGNAL(rejected()), dlg, SLOT(reject()));

char Guid::showCalendar(const QStringList &args)
{
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
                    qDebug() << "argument --align: unknown value" << args.at(i);
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

char Guid::showMessage(const QStringList &args, char type)
{
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
            qDebug() << "unspecific argument" << args.at(i);
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

static void addItems(QTreeWidget *tw, QStringList &values, bool editable, bool checkable, bool icons)
{
    for (int i = 0; i < values.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < tw->columnCount(); ++j) {
            itemValues << values.at(i++);
            if (i == values.count())
                break;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
        Qt::ItemFlags flags = item->flags();
        if (editable)
            flags |= Qt::ItemIsEditable;
        if (checkable) {
            flags |= Qt::ItemIsUserCheckable;
            item->setCheckState(0, Qt::Unchecked);
        }
        if (icons)
            item->setIcon(0, QPixmap(item->text(0)));
        if (checkable || icons) {
            item->setData(0, Qt::EditRole, item->text(0));
            item->setText(0, QString());
        }
        item->setFlags(flags);
        tw->addTopLevelItem(item);
    }
}

#define QTREEWIDGET_STYLE "QHeaderView::section {border: 1px solid #E0E0E0; background: #F7F7F7; padding-left: 3px; font-weight: bold;}"

static QStringList listValuesFromFile(QString data)
{
    QStringList values;
    QString separator;
    QString filename;
    
    if (data.contains(QRegExp("^.//"))) {
        separator = data.left(1);
        filename = data.remove(0, 3);
    } else {
        separator = "|";
        filename = data;
    }
    
    QFile file(filename);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    
    if (file.open(QIODevice::ReadOnly)) {
        values = QString::fromLocal8Bit(file.readAll()).replace(QRegExp("[\r\n]+"), separator).split(separator);
        file.close();
    }
    
    return values;
}

char Guid::showList(const QStringList &args)
{
    NEW_DIALOG

    QLabel *lbl;
    vl->addWidget(lbl = new QLabel(dlg));

    QTreeWidget *tw;
    vl->addWidget(tw = new QTreeWidget(dlg));
    tw->setSelectionBehavior(QAbstractItemView::SelectRows);
    tw->setSelectionMode(QAbstractItemView::SingleSelection);
    tw->setRootIsDecorated(false);
    tw->setAllColumnsShowFocus(true);

    bool editable(false), checkable(false), exclusive(false), icons(false), ok, needFilter(true);
    QStringList columns;
    QStringList values;
    QList<int> hiddenCols;
    dlg->setProperty("guid_print_column", 1);
    dlg->setProperty("guid_separator", "|");
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
                    qDebug() << "argument --align: unknown value" << args.at(i);
            } else
                WARN_UNKNOWN_ARG("--text");
        } else if (args.at(i) == "--multiple")
            tw->setSelectionMode(QAbstractItemView::ExtendedSelection);
        else if (args.at(i) == "--column") {
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
            dlg->setProperty("guid_print_column", NEXT_ARG);
        } else if (args.at(i) == "--checklist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
            checkable = true;
        } else if (args.at(i) == "--radiolist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
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
        } else if (args.at(i) == "--list-values-from-file") {
            values = listValuesFromFile(NEXT_ARG);
        } else if (args.at(i) != "--list") {
            values << args.at(i);
        }
    }
    if (values.isEmpty())
        listenToStdIn();

    if (checkable)
        editable = false;

    tw->setProperty("guid_list_flags", int(editable | checkable << 1 | icons << 2));

    int columnCount = qMax(columns.count(), 1);
    tw->setColumnCount(columnCount);
    tw->setHeaderLabels(columns);
    tw->setStyleSheet(QTREEWIDGET_STYLE);
    foreach (const int &i, hiddenCols)
        tw->setColumnHidden(i, true);

    addItems(tw, values, editable, checkable, icons);

    if (exclusive) {
        connect (tw, SIGNAL(itemChanged(QTreeWidgetItem*, int)), SLOT(toggleItems(QTreeWidgetItem*, int)));
    }
    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
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
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
    return 0;
}

static QFile *gs_stdin = 0;

void Guid::finishProgress()
{
    Q_ASSERT(m_type == Progress);
    QProgressDialog *dlg = static_cast<QProgressDialog*>(m_dialog);
    if (dlg->property("guid_autoclose").toBool())
        QTimer::singleShot(250, this, SLOT(quit()));
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

void Guid::readStdIn()
{
    if (!gs_stdin->isOpen())
        return;
    QSocketNotifier *notifier = qobject_cast<QSocketNotifier*>(sender());
    if (notifier)
        notifier->setEnabled(false);

    QByteArray ba = m_type == TextInfo ? gs_stdin->readAll() : gs_stdin->readLine();
    if (ba.isEmpty() && notifier) {
        gs_stdin->close();
//         gs_stdin->deleteLater(); // hello segfault...
//         gs_stdin = NULL;
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
                qWarning("'icon' command not yet supported - if you know what this is supposed to do, please file a bug");
            } else if (line.left(split) == "message" || line.left(split) == "tooltip") {
                userNeedsHelp = false;
                notify(line.mid(split+1));
            } else if (line.left(split) == "visible") {
                userNeedsHelp = false;
                if (m_dialog)
                    m_dialog->setVisible(line.mid(split+1).trimmed().compare("false", Qt::CaseInsensitive) &&
                                         line.mid(split+1).trimmed().compare("0", Qt::CaseInsensitive));
                else
                    qWarning("'visible' command only supported for failsafe dialog notification");
            } else if (line.left(split) == "hints") {
                m_notificationHints = line.mid(split+1);
            }
        }
        if (userNeedsHelp)
            qDebug() << "icon: <filename>\nmessage: <UTF-8 encoded text>\ntooltip: <UTF-8 encoded text>\nvisible: <true|false>";
    } else if (m_type == List) {
        if (QTreeWidget *tw = m_dialog->findChild<QTreeWidget*>()) {
            const int twflags = tw->property("guid_list_flags").toInt();
            addItems(tw, input, twflags & 1, twflags & 1<<1, twflags & 1<<2);
        }
    }
    if (notifier)
        notifier->setEnabled(true);
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

void Guid::printInteger(int v)
{
    printf("%d\n", v);
}

char Guid::showScale(const QStringList &args)
{
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
                    qDebug() << "argument --align: unknown value" << args.at(i);
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

char Guid::showColorSelection(const QStringList &args)
{
    QColorDialog *dlg = new QColorDialog;
    QVariantList l = QSettings("guid").value("CustomPalette").toList();
    for (int i = 0; i < l.count() && i < dlg->customCount(); ++i)
        dlg->setCustomColor(i, QColor(l.at(i).toUInt()));
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--color") {
            dlg->setCurrentColor(QColor(NEXT_ARG));
        } else if (args.at(i) == "--show-palette") {
            qWarning("The show-palette parameter is not supported by guid. Sorry.");
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
                    qWarning("Cannot read %s", path.toLocal8Bit().constData());
                }
            } else {
                qWarning("You have to provide a gimp palette (*.gpl)");
            }
        }{ WARN_UNKNOWN_ARG("--color-selection") }
    }
    SHOW_DIALOG
    return 0;
}

char Guid::showFontSelection(const QStringList &args)
{
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
                qWarning("The output pattern doesn't include a placeholder for the font name...");
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

static void buildFormsList(QTreeWidget **tree, QStringList &values, QStringList &columns, bool &showHeader, Qt::ItemFlags &flags)
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


    for (int i = 0; i < values.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < columnCount; ++j) {
            itemValues << values.at(i++);
            if (i == values.count())
                break;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
        flags |= item->flags();
        item->setFlags(flags);
        item->setTextAlignment(0, Qt::AlignLeft);
        tw->addTopLevelItem(item);
    }

    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    tw->setStyleSheet(QTREEWIDGET_STYLE);

    values.clear();
    columns.clear();
    showHeader = false;
    flags = Qt::NoItemFlags;
    *tree = NULL;
}

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
    fl->addRow(labelCol1, colsContainer);

static void setFormsColumns(QWidget* &colsContainer, QHBoxLayout* &colsHBoxLayout, QString prop_separator, bool &columnsAreSet)
{
    colsContainer = new QWidget();
    colsContainer->setProperty("guid_cols_container", true);
    colsContainer->setProperty("guid_separator", prop_separator);
    colsHBoxLayout->setContentsMargins(0, 0, 0, 0);
    colsContainer->setLayout(colsHBoxLayout);
    columnsAreSet = true;
}

static void setCol2Label(QHBoxLayout* &colsHBoxLayout, QLabel* label)
{
    label->setContentsMargins(0, 3, 0, 0);
    colsHBoxLayout->addWidget(label);
    colsHBoxLayout->setAlignment(label, Qt::AlignTop);
}

char Guid::showForms(const QStringList &args)
{
    NEW_DIALOG
    dlg->setProperty("guid_separator", "|");
    dlg->setProperty("guid_list_row_separator", "~");

    Qt::WindowFlags dlgFlags;
    dlgFlags |= Qt::WindowMinimizeButtonHint;
    dlgFlags |= Qt::WindowMaximizeButtonHint;
    dlgFlags |= Qt::WindowCloseButtonHint;
    dlg->setWindowFlags(dlgFlags);

    QLabel *label;
    vl->addWidget(label = new QLabel(dlg));
    label->setVisible(false);
    bool labelInBold = true;

    QFormLayout *flTopMenu = new QFormLayout();
    vl->addLayout(flTopMenu);

    QFormLayout *fl = new QFormLayout();
    vl->addLayout(fl);
    
    vl->layout()->setSpacing(15);
    vl->addStretch();

    bool columnsAreSet = false;
    QLabel *labelCol1 = NULL;
    QHBoxLayout *colsHBoxLayout = NULL;
    QWidget *colsContainer = NULL;
    
    QLineEdit *lastEntry = NULL;
    
    QLineEdit *lastPassword = NULL;
    
    QLabel *lastText = NULL;
    
    QTreeWidget *lastList = NULL;
    QStringList lastListValues, lastListColumns, lastComboValues;
    bool lastListHeader(false);
    Qt::ItemFlags lastListFlags;
    
    QComboBox *lastCombo = NULL;
    
    QHBoxLayout *scaleHBoxLayout = NULL;
    QWidget *scaleContainer = NULL;
    QLabel *lastScaleLabel = NULL;
    QSlider *lastScale = NULL;
    QLabel *lastScaleVal = NULL;
    
    QSpinBox *lastSpinBox = NULL;
    QDoubleSpinBox *lastDoubleSpinBox = NULL;
    
    QString lastColumn = NULL;
    QString lastWidget = NULL;
    
    QMenuBar *lastMenu = NULL;
    QSignalMapper *menuSignalMapper = new QSignalMapper(this);
    bool topMenu = false;
    
    bool ok;
    
    for (int i = 0; i < args.count(); ++i) {
        /********************************************************************************
         * WIDGET CONTAINERS
         ********************************************************************************/
        
        // --col1
        if (args.at(i) == "--col1") {
            lastColumn = "col1";
            colsHBoxLayout = new QHBoxLayout;
        }
        
        // --col2
        else if (args.at(i) == "--col2") {
            lastColumn = "col2";
        }
        
        /********************************************************************************
         * WIDGETS
         ********************************************************************************/
        
        // QCalendarWidget: --add-calendar
        else if (args.at(i) == "--add-calendar") {
            lastWidget = "calendar";
            QLabel *labelCal = new QLabel(NEXT_ARG);
            QCalendarWidget *cal = new QCalendarWidget(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelCal, cal)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelCal, cal)
            } else {
                fl->addRow(labelCal, cal);
            }
        }
        
        // QCheckBox: --add-checkbox
        else if (args.at(i) == "--add-checkbox") {
            lastWidget = "checkbox";
            QCheckBox *cb = new QCheckBox(NEXT_ARG, dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), cb)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), cb)
            } else {
                fl->addRow(cb);
            }
        }
        
        // QLineEdit: --add-entry
        else if (args.at(i) == "--add-entry") {
            lastWidget = "entry";
            QLabel *labelEntry = new QLabel(NEXT_ARG);
            lastEntry = new QLineEdit(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelEntry, lastEntry)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelEntry, lastEntry)
            } else {
                fl->addRow(labelEntry, lastEntry);
            }
        }
        
        // QMenuBar: --add-menu
        else if (args.at(i) == "--add-menu") {
            if (lastWidget.isEmpty() && lastColumn.isEmpty()) {
                topMenu = true;
            }
            lastWidget = "menu";
            lastMenu = new QMenuBar();
            
            QString next_arg = NEXT_ARG;
            
            bool menuWithSeparators = false;
            if (next_arg.left(1) == "|") {
                menuWithSeparators = true;
                next_arg.remove(0, 1);
            }
            
            bool closeDlgAfterMenuClick = false;
            if (next_arg.left(1) == "$") {
                closeDlgAfterMenuClick = true;
                next_arg.remove(0, 1);
            }
            
            QStringList menuItemsMainLevel = next_arg.split('|');
            QStringList menuItemChildren;
            
            QString menuItemData;
            QString menuItemName;
            int menuItemOutputCode;
            QIcon menuActionIcon = QApplication::style()->standardIcon(QStyle::SP_TitleBarMaxButton);
            
            for (int i = 0; i < menuItemsMainLevel.size(); ++i) {
                if (menuItemsMainLevel.at(i).contains('#')) {
                    menuItemData = menuItemsMainLevel.at(i).section('#', 0, 0);
                    menuItemChildren << menuItemsMainLevel.at(i).section('#', 1, -1).split('#');
                } else {
                    menuItemData = menuItemsMainLevel.at(i);
                }
                
                menuItemName = menuItemData.section(':', 0, -2);
                menuItemOutputCode = menuItemData.section(':', -1, -1).toInt(&ok);
                
                if (menuItemName.isEmpty()) {
                    menuItemName = menuItemData;
                    menuItemOutputCode = 0;
                    ok = true;
                }
                
                if (menuItemName.isEmpty() || !ok || menuItemOutputCode < 0 || menuItemOutputCode > 255) {
                    continue;
                }
                
                if (menuWithSeparators && i > 0) {
                    auto* menuSep = new QAction(QStringLiteral("|"), this);
                    menuSep->setDisabled(true);
                    lastMenu->addAction(menuSep);
                }
                
                // Menu with submenu items (first level elements are added as QMenu)
                if (!menuItemChildren.isEmpty()) {
                    QMenu *menuItem = new QMenu(menuItemName);
                    lastMenu->addMenu(menuItem);
                    
                    for (int j = 0; j < menuItemChildren.size(); ++j) {
                        menuItemName = menuItemChildren.at(j).section(':', 0, -2);
                        menuItemOutputCode = menuItemChildren.at(j).section(':', -1, -1).toInt(&ok);
                        
                        if (menuItemName.isEmpty() || !ok || menuItemOutputCode < 0 || menuItemOutputCode > 255) {
                            continue;
                        }
                        
                        QAction *menuAction = new QAction(menuItemName, this);
                        menuAction->setIcon(menuActionIcon);
                        menuItem->addAction(menuAction);
                        
                        connect(menuAction, SIGNAL(triggered()), menuSignalMapper, SLOT(map()), Qt::UniqueConnection);
                        menuSignalMapper->setMapping(menuAction, menuItemOutputCode);
                        if (closeDlgAfterMenuClick) {
                            connect(menuSignalMapper, SIGNAL(mapped(int)), this, SLOT(exitAfterMenuClick(int)), Qt::UniqueConnection);
                        } else {
                            connect(menuSignalMapper, SIGNAL(mapped(int)), this, SLOT(showMenuClick(int)), Qt::UniqueConnection);
                        }
                    }
                    
                    menuItemChildren.clear();
                }
                
                // Menu without submenu items (first level elements are added as QAction)
                else {
                    QAction *menuAction = new QAction(menuItemName, this);
                    lastMenu->addAction(menuAction);
                    
                    connect(menuAction, SIGNAL(triggered()), menuSignalMapper, SLOT(map()), Qt::UniqueConnection);
                    menuSignalMapper->setMapping(menuAction, menuItemOutputCode);
                    if (closeDlgAfterMenuClick) {
                        connect(menuSignalMapper, SIGNAL(mapped(int)), this, SLOT(exitAfterMenuClick(int)), Qt::UniqueConnection);
                    } else {
                        connect(menuSignalMapper, SIGNAL(mapped(int)), this, SLOT(showMenuClick(int)), Qt::UniqueConnection);
                    }
                }
            }
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastMenu)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastMenu)
            } else if (topMenu) {
                lastMenu->setStyleSheet("background: white; border-top: 1px solid #F0F0F0;");
                flTopMenu->addRow(lastMenu);
            } else {
                fl->addRow(lastMenu);
            }
        }
        
        // QLineEdit: --add-password
        else if (args.at(i) == "--add-password") {
            lastWidget = "password";
            QLabel *labelPassword = new QLabel(NEXT_ARG);
            lastPassword = new QLineEdit(dlg);
            lastPassword->setEchoMode(QLineEdit::Password);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelPassword, lastPassword)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelPassword, lastPassword)
            } else {
                fl->addRow(labelPassword, lastPassword);
            }
        }
        
        // QSpinBox: --add-spin-box
        else if (args.at(i) == "--add-spin-box") {
            lastWidget = "spin-box";
            QLabel *labelSpinBox = new QLabel(NEXT_ARG);
            lastSpinBox = new QSpinBox();
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelSpinBox, lastSpinBox)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelSpinBox, lastSpinBox)
            } else {
                fl->addRow(labelSpinBox, lastSpinBox);
            }
        }
        
        // QDoubleSpinBox: --add-double-spin-box
        else if (args.at(i) == "--add-double-spin-box") {
            lastWidget = "double-spin-box";
            QLabel *labelDoubleSpinBox = new QLabel(NEXT_ARG);
            lastDoubleSpinBox = new QDoubleSpinBox();
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelDoubleSpinBox, lastDoubleSpinBox)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelDoubleSpinBox, lastDoubleSpinBox)
            } else {
                fl->addRow(labelDoubleSpinBox, lastDoubleSpinBox);
            }
        }
        
        // QLabel: --add-text
        else if (args.at(i) == "--add-text") {
            lastWidget = "text";
            lastText = new QLabel(NEXT_ARG);
            lastText->setContentsMargins(3, 3, 0, 0);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(new QLabel(), lastText)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(new QLabel(), lastText)
                colsHBoxLayout->setSpacing(0);
            } else {
                fl->addRow(lastText);
            }
        }
        
        // QComboBox: --add-combo
        else if (args.at(i) == "--add-combo") {
            lastWidget = "combo";
            QLabel *labelCombo = new QLabel(NEXT_ARG);
            lastCombo = new QComboBox(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelCombo, lastCombo)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelCombo, lastCombo)
            } else {
                fl->addRow(labelCombo, lastCombo);
            }
            
            lastCombo->addItems(lastComboValues);
        }
        
        // QTreeWidget: --add-list
        else if (args.at(i) == "--add-list") {
            lastWidget = "list";
            QLabel *labelList = new QLabel(NEXT_ARG);
            buildFormsList(&lastList, lastListValues, lastListColumns, lastListHeader, lastListFlags);
            lastList = new QTreeWidget(dlg);
            
            if (lastColumn == "col1") {
                SET_FORMS_COL1(labelList, lastList)
            } else if (lastColumn == "col2") {
                SET_FORMS_COL2(labelList, lastList)
            } else {
                fl->addRow(labelList, lastList);
            }
        }
        
        // QSlider: --add-scale
        else if (args.at(i) == "--add-scale") {
            lastWidget = "scale";
            
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
            } else {
                fl->addRow(lastScaleLabel, scaleContainer);
            }
        }
        
        // labelText: --text
        else if (args.at(i) == "--text") {
            lastWidget = "label";
            topMenu = false;
            
            label->setText(labelText(NEXT_ARG));
            label->setVisible(true);
        }
        
        /********************************************************************************
         * WIDGET SETTINGS
         ********************************************************************************/
        
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
         * entry || password || spin-box || double-spin-box || combo
         ******************************/
        
        // --field-width
        else if (args.at(i) == "--field-width") {
            if (lastWidget == "entry") {
                lastEntry->setMaximumWidth(NEXT_ARG.toInt());
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
         * combo
         ******************************/
        
        // --combo-values
        else if (args.at(i) == "--combo-values") {
            lastComboValues = NEXT_ARG.split('|');
            if (lastWidget == "combo") {
                lastCombo->addItems(lastComboValues);
            }
        }
        
        /******************************
         * combo || list
         ******************************/
        
        // --editable
        else if (args.at(i) == "--editable") {
            if (lastWidget == "list")
                lastListFlags |= Qt::ItemIsEditable;
            else if (lastWidget == "combo")
                lastCombo->setEditable(true);
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        /******************************
         * list
         ******************************/
        
        // --column-values
        else if (args.at(i) == "--column-values") {
            lastListColumns = NEXT_ARG.split('|');
        }
        
        // --list-values
        else if (args.at(i) == "--list-values") {
            lastListValues = NEXT_ARG.split('|');
        }
        
        // --list-values-from-file
        else if (args.at(i) == "--list-values-from-file") {
            lastListValues = listValuesFromFile(NEXT_ARG);
        }
        
        // --multiple
        else if (args.at(i) == "--multiple") {
            if (lastWidget == "list")
                lastList->setSelectionMode(QAbstractItemView::ExtendedSelection);
            else
                WARN_UNKNOWN_ARG("--add-list");
        }
        
        // --show-header
        else if (args.at(i) == "--show-header") {
            lastListHeader = true;
        }
        
        /******************************
         * scale
         ******************************/
        
        // --value
        else if (args.at(i) == "--value") {
            if (lastWidget == "scale")
                lastScale->setValue(NEXT_ARG.toInt());
            else
                WARN_UNKNOWN_ARG("--add-scale");
        }
        
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
         * label
         ******************************/
        
        // --align || --bold || --italics || --font-size || --foreground-color || --background-color
        else if (args.at(i) == "--align" || args.at(i) == "--bold" || args.at(i) == "--italics" ||
                 args.at(i) == "--font-size" || args.at(i) == "--foreground-color" || args.at(i) == "--background-color") {
            QLabel *labelToSet = NULL;
            if (lastWidget == "label") {
                labelToSet = label;
            } else if (lastWidget == "text") {
                labelToSet = lastText;
            }
            
            QFont labelToSetFont = labelToSet->font();
            
            if (labelToSet) {
                if (args.at(i) == "--align") {
                    QString alignment = NEXT_ARG;
                    if (alignment == "left") {
                        labelToSet->setAlignment(Qt::AlignLeft);
                    } else if (alignment == "center") {
                        labelToSet->setAlignment(Qt::AlignCenter);
                    } else if (alignment == "right") {
                        labelToSet->setAlignment(Qt::AlignRight);
                    } else {
                        qDebug() << "argument --align: unknown value" << args.at(i);
                    }
                } else if (args.at(i) == "--bold") {
                    labelToSetFont.setBold(true);
                    labelToSet->setFont(labelToSetFont);
                } else if (args.at(i) == "--italics") {
                    labelToSetFont.setItalic(true);
                    labelToSet->setFont(labelToSetFont);
                } else if (args.at(i) == "--font-size") {
                    int fontSize = NEXT_ARG.toInt(&ok);
                    if (ok) {
                        labelToSetFont.setPointSize(fontSize);
                        labelToSet->setFont(labelToSetFont);
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
            } else {
                WARN_UNKNOWN_ARG("--add-text");
            }
        }
        
        // --no-bold
        else if (args.at(i) == "--no-bold") {
            labelInBold = false;
        }
        
        /********************************************************************************
         * DIALOG SETTINGS
         ********************************************************************************/
        
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
                qDebug() << "argument --forms-align: unknown value" << args.at(i);
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
        
        lastComboValues.clear();
        if (columnsAreSet) {
            labelCol1 = new QLabel();
            lastColumn = "";
            columnsAreSet = false;
        }
    }
    
    buildFormsList(&lastList, lastListValues, lastListColumns, lastListHeader, lastListFlags);
    
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
    
    SHOW_DIALOG
    
    return 0;
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


void Guid::printHelp(const QString &category)
{
    static HelpDict helpDict;
    QString main_separator = "================================================================================";
    if (helpDict.isEmpty()) {
        helpDict["help"] = CategoryHelp(tr("Help options"), HelpList() <<
                            Help("-h, --help", tr("Show help options")) <<
                            Help("--help-all", tr("Show all help options")) <<
                            Help("--help-general", tr("Show general options")) <<
                            Help("--help-misc", tr("Show miscellaneous options")) <<
                            Help("--help-qt", tr("Show Qt Options")) <<
                            Help("", tr("")) <<
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
                            
        helpDict["misc"] = CategoryHelp(tr("Miscellaneous options"), HelpList() <<
                            Help("--about", tr("About guid")) <<
                            Help("--version", tr("Print version")));
                            
        helpDict["qt"] = CategoryHelp(tr("Qt options"), HelpList() <<
                            Help("--foo", tr("Foo")) <<
                            Help("--bar", tr("Bar")));
                            
        helpDict["general"] = CategoryHelp(tr("General options"), HelpList() <<
                            Help("--title=TITLE", tr("Set the dialog title")) <<
                            Help("--window-icon=ICONPATH", tr("Set the window icon")) <<
                            Help("--width=WIDTH", tr("Set the width")) <<
                            Help("--height=HEIGHT", tr("Set the height")) <<
                            Help("", tr("")) <<
                            Help("--ok-label=TEXT", tr("Sets the label of the Ok button")) <<
                            Help("--cancel-label=TEXT", tr("Sets the label of the Cancel button")) <<
                            Help("", tr("")) <<
                            Help("--attach=WINDOW", tr("Set the parent window to attach to")) <<
                            Help("--modal", tr("Set the modal hint")) <<
                            Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")));
                            
        helpDict["application"] = CategoryHelp(tr("Application Options"), HelpList() <<
                            Help("--display=DISPLAY", tr("X display to use")) <<
                            Help("", tr("")) <<
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
                            
        helpDict["calendar"] = CategoryHelp(tr("Calendar options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
                            Help("", tr("")) <<
                            Help("--day=DAY", tr("Set the calendar day")) <<
                            Help("--month=MONTH", tr("Set the calendar month")) <<
                            Help("--year=YEAR", tr("Set the calendar year")) <<
                            Help("--date-format=PATTERN", tr("Set the format for the returned date")) <<
                            Help("", tr("")) <<
                            Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")));
                            
        helpDict["color-selection"] = CategoryHelp(tr("Color selection options"), HelpList() <<
                            Help("--color=VALUE", tr("Set the color")) <<
                            Help("--custom-palette=path/to/some.gpl",  "GUID ONLY! " + tr("Load a custom GPL for standard colors")) <<
                            Help("--show-palette", tr("Show the palette")));
                            
        helpDict["entry"] = CategoryHelp(tr("Text entry options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("", tr("")) <<
                            Help("--entry-text=TEXT", tr("Set the entry text")) <<
                            Help("--hide-text", tr("Hide the entry text")) <<
                            Help("", tr("")) <<
                            Help("--float=floating_point", "GUID ONLY! " + tr("Floating point input only, preset given value")) <<
                            Help("--int=integer", "GUID ONLY! " + tr("Integer input only, preset given value")) <<
                            Help("--values=v1|v2|v3|...", "GUID ONLY! " + tr("Offer preset values to pick from")));
                            
        helpDict["error"] = CategoryHelp(tr("Error options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
                            Help("--no-markup", tr("Do not enable html markup")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("", tr("")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("", tr("")) <<
                            Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
                            
        helpDict["file-selection"] = CategoryHelp(tr("File selection options"), HelpList() <<
                            Help("--filename=FILENAME", tr("Set the filename")) <<
                            Help("--file-filter=NAME | PATTERN1 PATTERN2 ...", tr("Sets a filename filter")) <<
                            Help("--directory", tr("Activate directory-only selection")) <<
                            Help("--multiple", tr("Allow multiple files to be selected")) <<
                            Help("", tr("")) <<
                            Help("--confirm-overwrite", tr("Confirm file selection if filename already exists")) <<
                            Help("--save", tr("Activate save mode")) <<
                            Help("", tr("")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")));
                            
        helpDict["font-selection"] = CategoryHelp(tr("Font selection options"), HelpList() <<
                            Help("--sample=TEXT", tr("Sample text, defaults to the foxdogthing")) <<
                            Help("--pattern=%1-%2:%3:%4", tr("Output pattern, %1: Name, %2: Size, %3: weight, %4: slant")) <<
                            Help("--type=[vector][,bitmap][,fixed][,variable]", tr("Filter fonts (default: all)")));
                            
        helpDict["forms"] = CategoryHelp(tr("Forms dialog options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
                            Help("--no-bold", "GUID ONLY! " + tr("Remove bold for the dialog text")) <<
                            Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
                            Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
                            Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example: guid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
                            Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example: guid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
                            Help("", tr("")) <<
                            Help("--col1", "GUID ONLY! " + tr("Start a two-field row. The next form field specified will be added in the first column. See --col2 for details.")) <<
                            Help("--col2", "GUID ONLY! " + tr("Finish a two-field row. The next form field specified will be added in the second column. Example: guid --forms --width=500 --text=\"The next row has two columns:\" --col1 --add-entry=\"Left label\" --int=5 --col2 --add-entry=\"Right label\" --field-width=100 --add-entry=\"Full-width row\"")) <<
                            Help("", tr("")) <<
                            Help("--add-calendar=Calendar field name", tr("Add a new Calendar in forms dialog")) <<
                            Help("", tr("")) <<
                            Help("--add-checkbox=Checkbox label", "GUID ONLY! " + tr("Add a new Checkbox forms dialog")) <<
                            Help("", tr("")) <<
                            Help("--add-combo=Combo box field name", tr("Add a new combo box in forms dialog")) <<
                            Help("--combo-values=List of values separated by |", tr("List of values for combo box")) <<
                            Help("--editable", "GUID ONLY! " + tr("Allow changes to text")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
                            Help("", tr("")) <<
                            Help("--add-entry=Field name", tr("Add a new Entry in forms dialog")) <<
                            Help("--float=floating_point", "GUID ONLY! " + tr("Floating point input only, preset given value")) <<
                            Help("--int=integer", "GUID ONLY! " + tr("Integer input only, preset given value")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
                            Help("", tr("")) <<
                            Help("--add-list=List field and header name", tr("Add a new List in forms dialog")) <<
                            Help("--column-values=List of values separated by |", tr("List of values for columns")) <<
                            Help("--list-values=List of values separated by |", tr("List of values for List")) <<
                            Help("--list-values-from-file=[SEP//]FILENAME", "GUID ONLY! " + tr("Open file and use content as list values. Example: guid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\" --show-header --list-values-from-file=\"/path/to/file\"")) <<
                            Help("", tr("By default, the symbol \"|\" is used as separator between values. To use another separator, specify it followed by \"//\". Example with \",\" as separator: guid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\" --show-header --list-values-from-file=\",///path/to/file\"")) <<
                            Help("--editable", "GUID ONLY! " + tr("Allow changes to text")) <<
                            Help("--multiple", "GUID ONLY! " + tr("Allow multiple rows to be selected")) <<
                            Help("--list-row-separator=SEPARATOR", "GUID ONLY! " + tr("Set output separator character for list rows (default is ~)")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
                            Help("--show-header", tr("Show the columns header")) <<
                            Help("", tr("")) <<
                            Help("--add-menu=Menu settings", "GUID ONLY! " + tr("Add a new Menu in forms dialog.")) <<
                            Help("", tr("First level menu items must be separated with the symbol \"|\".")) <<
                            Help("", tr("Second level menu items must be separated with the symbol \"#\".")) <<
                            Help("", tr("Each menu item without children must have an output code specified as follows: \"MenuItemName:OutputCode\".")) <<
                            Help("", tr("By default, the output code is printed on the console after a click on the menu item, and the dialog stays open.")) <<
                            Help("", tr("We can specify to close the dialog by adding the symbol \"$\" at the beginning of menu settings. Example: guid --forms --add-menu=\"$First Item#First Subitem:10|Second Item:11|Third Item:12\"")) <<
                            Help("", tr("To add separators between first level menu items, add the symbol \"|\" at the beginning of menu settings. Example: guid --forms --add-menu=\"|First Item#First Subitem:10#Second Subitem:11|Second Item:12|Third Item:13\"")) <<
                            Help("", tr("Example of form menu with separators and with the dialog closing after a click on a menu item: guid --forms --add-menu=\"|$First Item#First Subitem:10#Second Subitem:11|Second Item:12|Third Item:13|Fourth Item#First Subitem:14\" --add-entry=\"Text field\"")) <<
                            Help("", tr("Note that if a main label is set for the form with the argument \"--text=TEXT\", it'll be displayed on top of the dialog. Example: guid --forms --text=\"Form description\" --add-menu=\"First Item:10|Second Item:11\" --add-entry=\"Text field\"")) <<
                            Help("", tr("If we want to have a menu on top of the dialog but still have a main label for the form, we must add the label as new text with \"--add-text=TEXT\" after the menu. Example: guid --forms --add-menu=\"First Item:10|Second Item:11\" --add-text=\"Form description\" --bold --add-entry=\"Text field\"")) <<
                            Help("", tr("")) <<
                            Help("--add-password=Field name", tr("Add a new Password Entry in forms dialog")) <<
                            Help("", tr("")) <<
                            Help("--add-scale=Field name", tr("Add a new Scale/Slider in forms dialog")) <<
                            Help("--value=VALUE", tr("Set initial value")) <<
                            Help("--min-value=VALUE", tr("Set minimum value")) <<
                            Help("--max-value=VALUE", tr("Set maximum value")) <<
                            Help("--step=VALUE", tr("Set step size")) <<
                            Help("--hide-value", tr("Hide value")) <<
                            Help("--print-partial", tr("Print partial values")) <<
                            Help("", tr("")) <<
                            Help("--add-spin-box=Spin box name", "GUID ONLY! " + tr("Add a new spin box in forms dialog")) <<
                            Help("--min-value=VALUE", "GUID ONLY! " + tr("Set minimum value")) <<
                            Help("--max-value=VALUE", "GUID ONLY! " + tr("Set maximum value")) <<
                            Help("--prefix=PREFIX", "GUID ONLY! " + tr("Set prefix")) <<
                            Help("--suffix=SUFFIX", "GUID ONLY! " + tr("Set suffix")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
                            Help("", tr("")) <<
                            Help("--add-double-spin-box=Double spin box name", "GUID ONLY! " + tr("Add a new double spin box in forms dialog")) <<
                            Help("--decimals=VALUE", "GUID ONLY! " + tr("Set the number of decimals")) <<
                            Help("--min-value=VALUE", "GUID ONLY! " + tr("Set minimum value")) <<
                            Help("--max-value=VALUE", "GUID ONLY! " + tr("Set maximum value")) <<
                            Help("--prefix=PREFIX", "GUID ONLY! " + tr("Set prefix")) <<
                            Help("--suffix=SUFFIX", "GUID ONLY! " + tr("Set suffix")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")) <<
                            Help("", tr("")) <<
                            Help("--add-text=TEXT", "GUID ONLY! " + tr("Add text without field")) <<
                            Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
                            Help("--bold", "GUID ONLY! " + tr("Set text in bold")) <<
                            Help("--italics", "GUID ONLY! " + tr("Set text in italics")) <<
                            Help("--font-size=SIZE", "GUID ONLY! " + tr("Set font size")) <<
                            Help("--foreground-color=COLOR", "GUID ONLY! " + tr("Set text color. Example: guid --forms --text=\"Form description\" --color=\"#0000FF\"")) <<
                            Help("--background-color=COLOR", "GUID ONLY! " + tr("Set text background color. Example: guid --forms --text=\"Form description\" --background-color=\"#0000FF\"")) <<
                            Help("", tr("")) <<
                            Help("--forms-date-format=PATTERN", tr("Set the format for the returned date")) <<
                            Help("--forms-align=left|center|right", "GUID ONLY! " + tr("Set label alignment for the entire form")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")));
                            
        helpDict["info"] = CategoryHelp(tr("Info options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
                            Help("--no-markup", tr("Do not enable html markup")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("", tr("")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("", tr("")) <<
                            Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
                            
        helpDict["list"] = CategoryHelp(tr("List options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
                            Help("", tr("")) <<
                            Help("--checklist", tr("Use check boxes for first column")) <<
                            Help("--imagelist", tr("Use an image for first column")) <<
                            Help("--radiolist", tr("Use radio buttons for first column")) <<
                            Help("", tr("")) <<
                            Help("--column=COLUMN", tr("Set the column header")) <<
                            Help("--hide-column=NUMBER", tr("Hide a specific column")) <<
                            Help("--print-column=NUMBER", tr("Print a specific column (Default is 1. 'ALL' can be used to print all columns)")) <<
                            Help("--hide-header", tr("Hides the column headers")) <<
                            Help("", tr("")) <<
                            Help("--list-values-from-file=[SEP//]FILENAME", "GUID ONLY! " + tr("Open file and use content as list values. Example: guid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\" --show-header --list-values-from-file=\"/path/to/file\"")) <<
                            Help("", tr("By default, the symbol \"|\" is used as separator between values. To use another separator, specify it followed by \"//\". Example with \",\" as separator: guid --forms --add-list=\"List description\" --column-values=\"Column 1|Column 2\" --show-header --list-values-from-file=\",///path/to/file\"")) <<
                            Help("", tr("")) <<
                            Help("--editable", tr("Allow changes to text")) <<
                            Help("--multiple", tr("Allow multiple rows to be selected")) <<
                            Help("", tr("")) <<
                            Help("--mid-search", tr("Change list default search function searching for text in the middle, not on the beginning")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")));
                            
        helpDict["notification"] = CategoryHelp(tr("Notification icon options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("", tr("")) <<
                            Help("--hint=TEXT", tr("Set the notification hints")) <<
                            Help("", tr("")) <<
                            Help("--listen", tr("Listen for commands on stdin")) <<
                            Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
                            
        helpDict["password"] = CategoryHelp(tr("Password dialog options"), HelpList() <<
                            Help("--prompt=TEXT", "GUID ONLY! " + tr("The prompt for the user")) <<
                            Help("--username", tr("Display the username option")) <<
                            Help("--field-width=WIDTH", "GUID ONLY! " + tr("Set the field width")));
                            
        helpDict["progress"] = CategoryHelp(tr("Progress options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("", tr("")) <<
                            Help("--percentage=PERCENTAGE", tr("Set initial percentage")) <<
                            Help("--pulsate", tr("Pulsate progress bar")) <<
                            Help("", tr("")) <<
                            Help("--auto-close", tr("Dismiss the dialog when 100% has been reached")) <<
                            Help("--auto-kill", tr("Kill parent process if Cancel button is pressed")) <<
                            Help("--no-cancel", tr("Hide Cancel button")));
        helpDict["question"] = CategoryHelp(tr("Question options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
                            Help("--no-markup", tr("Do not enable html markup")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("", tr("")) <<
                            Help("--default-cancel", tr("Give cancel button focus by default")) <<
                            Help("", tr("")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("", tr("")) <<
                            Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
                            
        helpDict["scale"] = CategoryHelp(tr("Scale options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--align=left|center|right", "GUID ONLY! " + tr("Set text alignment")) <<
                            Help("", tr("")) <<
                            Help("--value=VALUE", tr("Set initial value")) <<
                            Help("--min-value=VALUE", tr("Set minimum value")) <<
                            Help("--max-value=VALUE", tr("Set maximum value")) <<
                            Help("--step=VALUE", tr("Set step size")) <<
                            Help("", tr("")) <<
                            Help("--hide-value", tr("Hide value")) <<
                            Help("--print-partial", tr("Print partial values")));
                            
        helpDict["text-info"] = CategoryHelp(tr("Text information options"), HelpList() <<
                            Help("--filename=FILENAME", tr("Open file")) <<
                            Help("", tr("")) <<
                            Help("--url=URL", "REQUIRES CURL BINARY! " + tr("Set an URL instead of a file. Only works if you use --html option")) <<
                            Help("--curl-path=PATH", "GUID ONLY! " + tr("Set the path to the curl binary. Default is \"curl\".")) <<
                            Help("", tr("")) <<
                            Help("--checkbox=TEXT", tr("Enable an I read and agree checkbox")) <<
                            Help("", tr("")) <<
                            Help("--editable", tr("Allow changes to text")) <<
                            Help("--font=TEXT", tr("Set the text font")) <<
                            Help("--plain", "GUID ONLY! " + tr("Force plain text, zenity default limitation")) <<
                            Help("--html", tr("Enable HTML support")) <<
                            Help("", tr("")) <<
                            Help("--auto-scroll", tr("Auto scroll the text to the end. Only when text is captured from stdin")) <<
                            Help("--no-interaction", tr("Do not enable user interaction with the WebView. Only works if you use --html option")));
                            
        helpDict["warning"] = CategoryHelp(tr("Warning options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--ellipsize", tr("Do wrap text, zenity has a rather special problem here")) <<
                            Help("--no-markup", tr("Do not enable html markup")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("", tr("")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("", tr("")) <<
                            Help("--selectable-labels", "GUID ONLY! " + tr("Allow to select text for copy and paste")));
                            
    }

    if (category == "all") {
        foreach(const CategoryHelp &el, helpDict) {
            printf("\n%s\n", qPrintable(el.first));
            printf("------------------------------\n\n");
            foreach (const Help &help, el.second)
                printf(" %-47s%s\n", qPrintable(help.first), qPrintable(help.second));
            printf("\n%s\n", qPrintable(main_separator));
        }
        return;
    }

    HelpDict::const_iterator it = helpDict.constEnd();
    if (!category.isEmpty())
        it = helpDict.find(category);

    if (it == helpDict.constEnd()) {
        printf("\nUsage:\n  %s [OPTION ...]\n\n", qPrintable(applicationName()));
        printf("%s\n\n", qPrintable(main_separator));
        printHelp("help");
        printf("%s\n\n", qPrintable(main_separator));
        printHelp("application");
        return;
    }

    printf("%s\n", qPrintable(it->first));
    foreach (const Help &help, it->second)
        printf(" %-47s%s\n", qPrintable(help.first), qPrintable(help.second));
    printf("\n");
}

int main (int argc, char **argv)
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