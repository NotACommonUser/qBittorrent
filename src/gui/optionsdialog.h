/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#pragma once

#include <QDialog>

#include "base/settingvalue.h"

class QCloseEvent;
class QListWidgetItem;
class QTableWidget;

class AdvancedSettings;
class ScheduleDay;

// actions on double-click on torrents
enum DoubleClickAction
{
    TOGGLE_PAUSE,
    OPEN_DEST,
    PREVIEW_FILE,
    NO_ACTION
};

namespace Net
{
    enum class ProxyType;
}

namespace Ui
{
    class OptionsDialog;
}

class OptionsDialog final : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OptionsDialog)

    using ThisType = OptionsDialog;

    enum Tabs
    {
        TAB_UI,
        TAB_DOWNLOADS,
        TAB_CONNECTION,
        TAB_SPEED,
        TAB_BITTORRENT,
        TAB_RSS,
        TAB_WEBUI,
        TAB_ADVANCED
    };

    enum class ShowError
    {
        NotShow,
        Show
    };

public:
    // Constructor / Destructor
    OptionsDialog(QWidget *parent = nullptr);
    ~OptionsDialog() override;

public slots:
    void showConnectionTab();

private slots:
    void enableProxy(int index);
    void on_buttonBox_accepted();
    void closeEvent(QCloseEvent *e) override;
    void on_buttonBox_rejected();
    void applySettings();
    void enableApplyButton();
    void toggleComboRatioLimitAct();
    void changePage(QListWidgetItem *, QListWidgetItem *);
    void loadSplitterState();
    void handleWatchedFolderViewSelectionChanged();
    void editWatchedFolderOptions(const QModelIndex &index);
    void on_IpFilterRefreshBtn_clicked();
    void handleIPFilterParsed(bool error, int ruleCount);
    void on_banListButton_clicked();
    void on_IPSubnetWhitelistButton_clicked();
    void on_randomButton_clicked();
    void on_addWatchedFolderButton_clicked();
    void on_editWatchedFolderButton_clicked();
    void on_removeWatchedFolderButton_clicked();
    void on_registerDNSBtn_clicked();
    void setLocale(const QString &localeStr);
    void webUIHttpsCertChanged(const QString &path, ShowError showError);
    void webUIHttpsKeyChanged(const QString &path, ShowError showError);

private:
    // Methods
    void saveOptions();
    void loadOptions();
    void initializeLanguageCombo();

    // General options
    QString getLocale() const;
#ifndef Q_OS_MACOS
    bool systrayIntegration() const;
    bool minimizeToTray() const;
    bool closeToTray() const;
#endif
    bool startMinimized() const;
    bool isSplashScreenDisabled() const;
#ifdef Q_OS_WIN
    bool WinStartup() const;
#endif
    // Downloads
    bool preAllocateAllFiles() const;
    bool useAdditionDialog() const;
    bool addTorrentsInPause() const;
    QString getTorrentExportDir() const;
    QString getFinishedTorrentExportDir() const;
    QString askForExportDir(const QString &currentExportPath);
    int getActionOnDblClOnTorrentDl() const;
    int getActionOnDblClOnTorrentFn() const;
    // Connection options
    int getPort() const;
    bool isUPnPEnabled() const;
    // Speed options
    void initializeSchedulerTables();
    static void populateScheduleDayTable(QTableWidget *scheduleTable, const ScheduleDay *scheduleDay);
    void openScheduleEntryDialog(ScheduleDay *scheduleDay);
    void removeSelectedScheduleEntries(int day);
    void showScheduleDayContextMenu(int day);
    // Bittorrent options
    int getMaxConnections() const;
    int getMaxConnectionsPerTorrent() const;
    int getMaxUploads() const;
    int getMaxUploadsPerTorrent() const;
    bool isDHTEnabled() const;
    bool isLSDEnabled() const;
    int getEncryptionSetting() const;
    qreal getMaxRatio() const;
    int getMaxSeedingMinutes() const;
    // Proxy options
    bool isProxyEnabled() const;
    bool isProxyAuthEnabled() const;
    QString getProxyIp() const;
    unsigned short getProxyPort() const;
    QString getProxyUsername() const;
    QString getProxyPassword() const;
    Net::ProxyType getProxyType() const;
    // IP Filter
    bool isIPFilteringEnabled() const;
    QString getFilter() const;
    // Queueing system
    bool isQueueingSystemEnabled() const;
    int getMaxActiveDownloads() const;
    int getMaxActiveUploads() const;
    int getMaxActiveTorrents() const;
    // WebUI
    bool isWebUiEnabled() const;
    QString webUiUsername() const;
    QString webUiPassword() const;
    bool webUIAuthenticationOk();
    bool isAlternativeWebUIPathValid();

    Ui::OptionsDialog *m_ui;
    SettingValue<QSize> m_storeDialogSize;
    SettingValue<QStringList> m_storeHSplitterSize;
    SettingValue<int> m_storeLastViewedPage;

    QPushButton *m_applyButton;

    AdvancedSettings *m_advancedSettings;

    QVector<QTableWidget*> m_scheduleDayTables = QVector<QTableWidget*>(7);

    bool m_refreshingIpFilter = false;
};
