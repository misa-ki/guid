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
    void centerWidget(QWidget *widget, QWidget *host = Q_NULLPTR);
    bool error(const QString message);
    QString labelText(const QString &s) const; // m_zenity requires \n and \t interpretation in html.
    void listenToStdIn();
    void notify(const QString message, bool noClose = false);
    bool readGeneral(QStringList &args);
    void setSysTrayAction(QString actionId, bool valueToSet);
    
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
    void afterCloseButtonClick();
    void afterMenuClick();
    void dialogFinished(int status);
    void exitGuid(int exitCode = 0, bool minimize = false);
    void finishProgress();
    void minimizeDialog();
    void printInteger(int v);
    void quitDialog();
    void readStdIn();
    void showDialog();
    void showSysTrayMenu(QSystemTrayIcon::ActivationReason reason);
    void toggleItems(QTreeWidgetItem *item, int column);
    void updateCombo(QString filePath);
    void updateList(QString filePath);

private:
    QString          m_cancel;
    QString          m_caption;
    bool             m_closeToSysTray;
    QDialog         *m_dialog;
    bool             m_helpMission;
    QString          m_icon;
    bool             m_modal;
    QString          m_notificationHints;
    uint             m_notificationId;
    QString          m_ok;
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