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

#ifndef GUID_H
#define GUID_H

class QDialog;
class QTreeWidgetItem;

#include <QApplication>
#include <QGroupBox>
#include <QLabel>
#include <QPair>
#include <QSystemTrayIcon>
#include <QTreeWidget>
#include <QWidget>

struct GList {
    QString addValue;
    QString filePath;
    QString fileSep;
    bool monitorFile;
    QStringList val;
};

struct FormsSettings {
    bool hasLabel = false;
    bool hasTopMenu = false;
    bool hasHeader = false;
};

struct WidgetSettings {
    QString addLabel = "";
    bool addNewRowButton = false;
    QString backgroundColor = "";
    QString buttonText = "";
    QString color = "";
    QString command = "";
    bool commandToFooter = false;
    int defaultIndex = 0;
    QString defMarkerVal1 = "";
    QString defMarkerVal2 = "";
    QString defMarkerVal3 = "";
    QString defMarkerVal4 = "";
    QString defMarkerVal5 = "";
    QString defMarkerVal6 = "";
    QString defMarkerVal7 = "";
    QString defMarkerVal8 = "";
    QString defMarkerVal9 = "";
    bool disableButtons = false;
    bool excludeFromOutput = false;
    QString foregroundColor = "";
    bool hideLabel = false;
    QString image = "";
    bool keepOpen = false;
    bool monitorFile = false;
    QString monitorMarkerFile1 = "";
    QString monitorMarkerFile2 = "";
    QString monitorMarkerFile3 = "";
    QString monitorMarkerFile4 = "";
    QString monitorMarkerFile5 = "";
    QString monitorMarkerFile6 = "";
    QString monitorMarkerFile7 = "";
    QString monitorMarkerFile8 = "";
    QString monitorMarkerFile9 = "";
    QString monitorVarName1 = "";
    QString monitorVarName2 = "";
    QString monitorVarName3 = "";
    QString monitorVarName4 = "";
    QString monitorVarName5 = "";
    QString monitorVarName6 = "";
    QString monitorVarName7 = "";
    QString monitorVarName8 = "";
    QString monitorVarName9 = "";
    QString sep = "";
    bool stop = false;
    bool valuesToFooter = false;
    bool verboseTabBar = false;
};

class Guid : public QApplication
{
    Q_OBJECT

public:
    Guid(int &argc, char **argv);
    enum Type {
        Invalid, Calendar, Entry, Error, Info, FileSelection, List, Notification, Progress,
        Question, Warning, Scale, TextInfo, ColorSelection, FontSelection, Password, Forms
    };
    static void printHelp(const QString &category = QString());
    using  QApplication::notify;

private:
    // Misc.
    void createQRCode(QLabel *label, QString text);
    bool error(const QString message);
    QString labelText(const QString &s) const; // m_zenity requires \n and \t interpretation in html.
    void listenToStdIn();
    void notify(const QString message, bool noClose = false);
    QString printForms();
    bool readGeneral(QStringList &args);
    void setSysTrayAction(QString actionId, bool valueToSet);
    void updateFooterContent(QGroupBox *footer, QString newEntry);
    void updateFooterContentFromFile(QGroupBox *footer, QString filePath);
    
    // Show dialogs
    char showCalendar(const QStringList &args);
    char showColorSelection(const QStringList &args);
    char showEntry(const QStringList &args);
    char showFileSelection(const QStringList &args);
    char showFontSelection(const QStringList &args);
    char showForms(const QStringList &args);
    char showList(const QStringList &args);
    char showMessage(const QStringList &args, char type);
    char showNotification(const QStringList &args);
    char showPassword(const QStringList &args);
    char showProgress(const QStringList &args);
    char showScale(const QStringList &args);
    char showText(const QStringList &args);

private slots:
    void addListRow();
    void afterTabBarClick(int i);
    void afterCloseButtonClick();
    void afterMenuClick();
    void dialogFinished(int status);
    void exitGuid(int exitCode = 0, bool minimize = false);
    void finishProgress();
    void listMenu(const QPoint &pos);
    void minimizeDialog();
    void printFormsAfterOKClick();
    void printInteger(int v);
    void quitDialog();
    void readStdIn();
    void showDialog();
    void showSysTrayMenu(QSystemTrayIcon::ActivationReason reason);
    void toggleItems(QTreeWidgetItem *item, int column);
    void updateCombo(QString filePath);
    void updateFooter(QString filePath);
    void updateList(QString filePath);
    void updateText(QString filePath);
    void updateTextInfo(QString filePath);

private:
    bool             m_alwaysOnTop;
    QString          m_cancel;
    QString          m_caption;
    bool             m_closeToSysTray;
    QDialog         *m_dialog;
    bool             m_helpMission;
    QString          m_icon;
    bool             m_modal;
    bool             m_noTaskbar;
    QString          m_notificationHints;
    uint             m_notificationId;
    QString          m_ok;
    QString          m_okCommand;
    bool             m_okCommandToFooter;
    bool             m_okKeepOpen;
    bool             m_okValuesToFooter;
    int              m_parentWindow;
    QString          m_prefixErr;
    QString          m_prefixOk;
    bool             m_selectableLabel;
    QSize            m_size;
    QSystemTrayIcon *m_sysTray;
    bool             m_sysTrayMsg;
    int              m_timeout;
    Type             m_type;
    bool             m_zenity;
};

#endif //GUID_H

// vim:set et sw=4 ts=4