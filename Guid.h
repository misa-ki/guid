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

struct GList {
    QStringList val;
    QString fileSep;
    QString filePath;
    bool monitorFile;
};

class Guid : public QApplication
{
    Q_OBJECT
public:
    Guid(int &argc, char **argv);
    enum Type { Invalid, Calendar, Entry, Error, Info, FileSelection, List, Notification, Progress, Question, Warning,
                Scale, TextInfo, ColorSelection, FontSelection, Password, Forms };
    static void printHelp(const QString &category = QString());
    using  QApplication::notify;
private:
    char showCalendar(const QStringList &args);
    char showEntry(const QStringList &args);
    char showPassword(const QStringList &args);

    char showMessage(const QStringList &args, char type);

    char showFileSelection(const QStringList &args);
    char showList(const QStringList &args);
    char showNotification(const QStringList &args);
    char showProgress(const QStringList &args);
    char showScale(const QStringList &args);
    char showText(const QStringList &args);
    char showColorSelection(const QStringList &args);
    char showFontSelection(const QStringList &args);
    char showForms(const QStringList &args);
    bool readGeneral(QStringList &args);
    bool error(const QString message);
    void listenToStdIn();
    void notify(const QString message, bool noClose = false);

    QString labelText(const QString &s) const; // m_zenity requires \n and \t interpretation in html.
private slots:
    void dialogFinished(int status);
    void printInteger(int v);
    void quitOnError();
    void readStdIn();
    void toggleItems(QTreeWidgetItem *item, int column);
    void finishProgress();
    void afterMenuClick();
    void updateCombo(QString filePath);
    void updateList(QString filePath);
private:
    bool m_helpMission, m_modal, m_zenity, m_selectableLabel;
    QString m_caption, m_icon, m_ok, m_cancel, m_notificationHints;
    QSize m_size;
    int m_parentWindow, m_timeout;
    uint m_notificationId;
    QDialog *m_dialog;
    Type m_type;
    QString m_prefix_ok;
    QString m_prefix_err;
};

#endif //GUID_H