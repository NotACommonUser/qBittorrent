/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2012  Christophe Dumez <chris@qbittorrent.org>
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

#include "addnewtorrentdialog.h"

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QString>
#include <QUrl>
#include <QVector>

#include "base/bittorrent/downloadpriority.h"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/magneturi.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/global.h"
#include "base/net/downloadmanager.h"
#include "base/settingsstorage.h"
#include "base/torrentfileguard.h"
#include "base/utils/compare.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "autoexpandabledialog.h"
#include "properties/proplistdelegate.h"
#include "raisedmessagebox.h"
#include "torrentcontentfiltermodel.h"
#include "torrentcontentmodel.h"
#include "ui_addnewtorrentdialog.h"
#include "uithememanager.h"
#include "utils.h"

namespace
{
#define SETTINGS_KEY(name) "AddNewTorrentDialog/" name
    const QString KEY_ENABLED = QStringLiteral(SETTINGS_KEY("Enabled"));
    const QString KEY_DEFAULTCATEGORY = QStringLiteral(SETTINGS_KEY("DefaultCategory"));
    const QString KEY_TREEHEADERSTATE = QStringLiteral(SETTINGS_KEY("TreeHeaderState"));
    const QString KEY_TOPLEVEL = QStringLiteral(SETTINGS_KEY("TopLevel"));
    const QString KEY_SAVEPATHHISTORY = QStringLiteral(SETTINGS_KEY("SavePathHistory"));
    const QString KEY_SAVEPATHHISTORYLENGTH = QStringLiteral(SETTINGS_KEY("SavePathHistoryLength"));
    const QString KEY_REMEMBERLASTSAVEPATH = QStringLiteral(SETTINGS_KEY("RememberLastSavePath"));

    // just a shortcut
    inline SettingsStorage *settings()
    {
        return SettingsStorage::instance();
    }
}

const int AddNewTorrentDialog::minPathHistoryLength;
const int AddNewTorrentDialog::maxPathHistoryLength;

AddNewTorrentDialog::AddNewTorrentDialog(const BitTorrent::AddTorrentParams &inParams, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::AddNewTorrentDialog)
    , m_contentModel(nullptr)
    , m_contentDelegate(nullptr)
    , m_hasMetadata(false)
    , m_oldIndex(0)
    , m_torrentParams(inParams)
    , m_storeDialogSize(SETTINGS_KEY("DialogSize"))
    , m_storeSplitterState(SETTINGS_KEY("SplitterState"))
{
    // TODO: set dialog file properties using m_torrentParams.filePriorities
    m_ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    m_ui->lblMetaLoading->setVisible(false);
    m_ui->progMetaLoading->setVisible(false);
    m_ui->buttonSave->setVisible(false);
    connect(m_ui->buttonSave, &QPushButton::clicked, this, &AddNewTorrentDialog::saveTorrentFile);

    m_ui->savePath->setMode(FileSystemPathEdit::Mode::DirectorySave);
    m_ui->savePath->setDialogCaption(tr("Choose save path"));
    m_ui->savePath->setMaxVisibleItems(20);

    const auto *session = BitTorrent::Session::instance();

    m_ui->startTorrentCheckBox->setChecked(!m_torrentParams.addPaused.value_or(session->isAddTorrentPaused()));

    m_ui->comboTTM->blockSignals(true); // the TreeView size isn't correct if the slot does it job at this point
    m_ui->comboTTM->setCurrentIndex(!session->isAutoTMMDisabledByDefault());
    m_ui->comboTTM->blockSignals(false);
    populateSavePathComboBox();
    connect(m_ui->savePath, &FileSystemPathEdit::selectedPathChanged, this, &AddNewTorrentDialog::onSavePathChanged);

    const bool rememberLastSavePath = settings()->loadValue(KEY_REMEMBERLASTSAVEPATH, false);
    m_ui->checkBoxRememberLastSavePath->setChecked(rememberLastSavePath);

    m_ui->contentLayoutComboBox->setCurrentIndex(
                static_cast<int>(m_torrentParams.contentLayout.value_or(session->torrentContentLayout())));

    m_ui->sequentialCheckBox->setChecked(m_torrentParams.sequential);
    m_ui->firstLastCheckBox->setChecked(m_torrentParams.firstLastPiecePriority);

    m_ui->skipCheckingCheckBox->setChecked(m_torrentParams.skipChecking);
    m_ui->doNotDeleteTorrentCheckBox->setVisible(TorrentFileGuard::autoDeleteMode() != TorrentFileGuard::Never);

    // Load categories
    QStringList categories = session->categories().keys();
    std::sort(categories.begin(), categories.end(), Utils::Compare::NaturalLessThan<Qt::CaseInsensitive>());
    auto defaultCategory = settings()->loadValue<QString>(KEY_DEFAULTCATEGORY);

    if (!m_torrentParams.category.isEmpty())
        m_ui->categoryComboBox->addItem(m_torrentParams.category);
    if (!defaultCategory.isEmpty())
        m_ui->categoryComboBox->addItem(defaultCategory);
    m_ui->categoryComboBox->addItem("");

    for (const QString &category : asConst(categories))
        if (category != defaultCategory && category != m_torrentParams.category)
            m_ui->categoryComboBox->addItem(category);

    m_ui->contentTreeView->header()->setSortIndicator(0, Qt::AscendingOrder);
    loadState();
    // Signal / slots
    connect(m_ui->doNotDeleteTorrentCheckBox, &QCheckBox::clicked, this, &AddNewTorrentDialog::doNotDeleteTorrentClicked);
    QShortcut *editHotkey = new QShortcut(Qt::Key_F2, m_ui->contentTreeView, nullptr, nullptr, Qt::WidgetShortcut);
    connect(editHotkey, &QShortcut::activated
            , this, [this]() { m_ui->contentTreeView->renameSelectedFile(m_torrentInfo); });
    connect(m_ui->contentTreeView, &QAbstractItemView::doubleClicked
            , this, [this]() { m_ui->contentTreeView->renameSelectedFile(m_torrentInfo); });

    m_ui->buttonBox->button(QDialogButtonBox::Ok)->setFocus();
}

AddNewTorrentDialog::~AddNewTorrentDialog()
{
    saveState();

    delete m_contentDelegate;
    delete m_ui;
}

bool AddNewTorrentDialog::isEnabled()
{
    return SettingsStorage::instance()->loadValue(KEY_ENABLED, true);
}

void AddNewTorrentDialog::setEnabled(bool value)
{
    SettingsStorage::instance()->storeValue(KEY_ENABLED, value);
}

bool AddNewTorrentDialog::isTopLevel()
{
    return SettingsStorage::instance()->loadValue(KEY_TOPLEVEL, true);
}

void AddNewTorrentDialog::setTopLevel(bool value)
{
    SettingsStorage::instance()->storeValue(KEY_TOPLEVEL, value);
}

int AddNewTorrentDialog::savePathHistoryLength()
{
    const int defaultHistoryLength = 8;
    const int value = settings()->loadValue(KEY_SAVEPATHHISTORYLENGTH, defaultHistoryLength);
    return qBound(minPathHistoryLength, value, maxPathHistoryLength);
}

void AddNewTorrentDialog::setSavePathHistoryLength(int value)
{
    const int clampedValue = qBound(minPathHistoryLength, value, maxPathHistoryLength);
    const int oldValue = savePathHistoryLength();
    if (clampedValue == oldValue)
        return;

    settings()->storeValue(KEY_SAVEPATHHISTORYLENGTH, clampedValue);
    settings()->storeValue(KEY_SAVEPATHHISTORY
        , QStringList(settings()->loadValue<QStringList>(KEY_SAVEPATHHISTORY).mid(0, clampedValue)));
}

void AddNewTorrentDialog::loadState()
{
    Utils::Gui::resize(this, m_storeDialogSize);
    m_ui->splitter->restoreState(m_storeSplitterState);
    m_headerState = settings()->loadValue<QByteArray>(KEY_TREEHEADERSTATE);
}

void AddNewTorrentDialog::saveState()
{
    m_storeDialogSize = size();
    m_storeSplitterState = m_ui->splitter->saveState();
    if (m_contentModel)
        settings()->storeValue(KEY_TREEHEADERSTATE, m_ui->contentTreeView->header()->saveState());
}

void AddNewTorrentDialog::show(const QString &source, const BitTorrent::AddTorrentParams &inParams, QWidget *parent)
{
    auto *dlg = new AddNewTorrentDialog(inParams, parent);

    if (Net::DownloadManager::hasSupportedScheme(source))
    {
        // Launch downloader
        Net::DownloadManager::instance()->download(
                    Net::DownloadRequest(source).limit(MAX_TORRENT_SIZE)
                    , dlg, &AddNewTorrentDialog::handleDownloadFinished);
        return;
    }

    const BitTorrent::MagnetUri magnetUri(source);
    const bool isLoaded = magnetUri.isValid()
        ? dlg->loadMagnet(magnetUri)
        : dlg->loadTorrentFile(source);

    if (isLoaded)
        dlg->QDialog::show();
    else
        delete dlg;
}

void AddNewTorrentDialog::show(const QString &source, QWidget *parent)
{
    show(source, BitTorrent::AddTorrentParams(), parent);
}

bool AddNewTorrentDialog::loadTorrentFile(const QString &torrentPath)
{
    const QString decodedPath = torrentPath.startsWith("file://", Qt::CaseInsensitive)
        ? QUrl::fromEncoded(torrentPath.toLocal8Bit()).toLocalFile()
        : torrentPath;

    const nonstd::expected<BitTorrent::TorrentInfo, QString> result = BitTorrent::TorrentInfo::loadFromFile(decodedPath);
    m_torrentInfo = result.value_or(BitTorrent::TorrentInfo());
    if (!result)
    {
        RaisedMessageBox::critical(this, tr("Invalid torrent")
            , tr("Failed to load the torrent: %1.\nError: %2", "Don't remove the '\n' characters. They insert a newline.")
                .arg(Utils::Fs::toNativePath(decodedPath), result.error()));
        return false;
    }

    m_torrentGuard = std::make_unique<TorrentFileGuard>(decodedPath);

    return loadTorrentImpl();
}

bool AddNewTorrentDialog::loadTorrentImpl()
{
    m_hasMetadata = true;
    const auto torrentID = BitTorrent::TorrentID::fromInfoHash(m_torrentInfo.infoHash());

    // Prevent showing the dialog if download is already present
    if (BitTorrent::Session::instance()->isKnownTorrent(torrentID))
    {
        BitTorrent::Torrent *const torrent = BitTorrent::Session::instance()->findTorrent(torrentID);
        if (torrent)
        {
            if (torrent->isPrivate() || m_torrentInfo.isPrivate())
            {
                RaisedMessageBox::warning(this, tr("Torrent is already present"), tr("Torrent '%1' is already in the transfer list. Trackers haven't been merged because it is a private torrent.").arg(torrent->name()), QMessageBox::Ok);
            }
            else
            {
                torrent->addTrackers(m_torrentInfo.trackers());
                torrent->addUrlSeeds(m_torrentInfo.urlSeeds());
                RaisedMessageBox::information(this, tr("Torrent is already present"), tr("Torrent '%1' is already in the transfer list. Trackers have been merged.").arg(torrent->name()), QMessageBox::Ok);
            }
        }
        else
        {
            RaisedMessageBox::information(this, tr("Torrent is already present"), tr("Torrent is already queued for processing."), QMessageBox::Ok);
        }
        return false;
    }

    m_ui->labelInfohash1Data->setText(m_torrentInfo.infoHash().v1().isValid() ? m_torrentInfo.infoHash().v1().toString() : tr("N/A"));
    m_ui->labelInfohash2Data->setText(m_torrentInfo.infoHash().v2().isValid() ? m_torrentInfo.infoHash().v2().toString() : tr("N/A"));
    setupTreeview();
    TMMChanged(m_ui->comboTTM->currentIndex());
    return true;
}

bool AddNewTorrentDialog::loadMagnet(const BitTorrent::MagnetUri &magnetUri)
{
    if (!magnetUri.isValid())
    {
        RaisedMessageBox::critical(this, tr("Invalid magnet link"), tr("This magnet link was not recognized"));
        return false;
    }

    m_torrentGuard = std::make_unique<TorrentFileGuard>();

    const auto torrentID = BitTorrent::TorrentID::fromInfoHash(magnetUri.infoHash());
    // Prevent showing the dialog if download is already present
    if (BitTorrent::Session::instance()->isKnownTorrent(torrentID))
    {
        BitTorrent::Torrent *const torrent = BitTorrent::Session::instance()->findTorrent(torrentID);
        if (torrent)
        {
            if (torrent->isPrivate())
            {
                RaisedMessageBox::warning(this, tr("Torrent is already present"), tr("Torrent '%1' is already in the transfer list. Trackers haven't been merged because it is a private torrent.").arg(torrent->name()), QMessageBox::Ok);
            }
            else
            {
                torrent->addTrackers(magnetUri.trackers());
                torrent->addUrlSeeds(magnetUri.urlSeeds());
                RaisedMessageBox::information(this, tr("Torrent is already present"), tr("Magnet link '%1' is already in the transfer list. Trackers have been merged.").arg(torrent->name()), QMessageBox::Ok);
            }
        }
        else
        {
            RaisedMessageBox::information(this, tr("Torrent is already present"), tr("Magnet link is already queued for processing."), QMessageBox::Ok);
        }
        return false;
    }

    connect(BitTorrent::Session::instance(), &BitTorrent::Session::metadataDownloaded, this, &AddNewTorrentDialog::updateMetadata);

    // Set dialog title
    const QString torrentName = magnetUri.name();
    setWindowTitle(torrentName.isEmpty() ? tr("Magnet link") : torrentName);

    setupTreeview();
    TMMChanged(m_ui->comboTTM->currentIndex());

    BitTorrent::Session::instance()->downloadMetadata(magnetUri);
    setMetadataProgressIndicator(true, tr("Retrieving metadata..."));
    m_ui->labelInfohash1Data->setText(magnetUri.infoHash().v1().isValid() ? magnetUri.infoHash().v1().toString() : tr("N/A"));
    m_ui->labelInfohash2Data->setText(magnetUri.infoHash().v2().isValid() ? magnetUri.infoHash().v2().toString() : tr("N/A"));

    m_magnetURI = magnetUri;
    return true;
}

void AddNewTorrentDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!isTopLevel()) return;

    activateWindow();
    raise();
}

void AddNewTorrentDialog::saveSavePathHistory() const
{
    // Get current history
    auto history = settings()->loadValue<QStringList>(KEY_SAVEPATHHISTORY);
    QVector<QDir> historyDirs;
    for (const QString &path : asConst(history))
        historyDirs << QDir {path};

    const QDir selectedSavePath {m_ui->savePath->selectedPath()};
    const int selectedSavePathIndex = historyDirs.indexOf(selectedSavePath);
    if (selectedSavePathIndex > 0)
        history.removeAt(selectedSavePathIndex);
    if (selectedSavePathIndex != 0)
        // Add last used save path to the front of history
        history.push_front(selectedSavePath.absolutePath());

    // Save history
    settings()->storeValue(KEY_SAVEPATHHISTORY, QStringList {history.mid(0, savePathHistoryLength())});
}

// savePath is a folder, not an absolute file path
int AddNewTorrentDialog::indexOfSavePath(const QString &savePath)
{
    QDir saveDir(savePath);
    for (int i = 0; i < m_ui->savePath->count(); ++i)
        if (QDir(m_ui->savePath->item(i)) == saveDir)
            return i;
    return -1;
}

void AddNewTorrentDialog::updateDiskSpaceLabel()
{
    // Determine torrent size
    qlonglong torrentSize = 0;

    if (m_hasMetadata)
    {
        if (m_contentModel)
        {
            const QVector<BitTorrent::DownloadPriority> priorities = m_contentModel->model()->getFilePriorities();
            Q_ASSERT(priorities.size() == m_torrentInfo.filesCount());
            for (int i = 0; i < priorities.size(); ++i)
            {
                if (priorities[i] > BitTorrent::DownloadPriority::Ignored)
                    torrentSize += m_torrentInfo.fileSize(i);
            }
        }
        else
        {
            torrentSize = m_torrentInfo.totalSize();
        }
    }

    const QString sizeString = tr("%1 (Free space on disk: %2)").arg(
        ((torrentSize > 0) ? Utils::Misc::friendlyUnit(torrentSize) : tr("Not available", "This size is unavailable."))
        , Utils::Misc::friendlyUnit(Utils::Fs::freeDiskSpaceOnPath(m_ui->savePath->selectedPath())));
    m_ui->labelSizeData->setText(sizeString);
}

void AddNewTorrentDialog::onSavePathChanged(const QString &newPath)
{
    Q_UNUSED(newPath);
    // Remember index
    m_oldIndex = m_ui->savePath->currentIndex();
    updateDiskSpaceLabel();
}

void AddNewTorrentDialog::categoryChanged(int index)
{
    Q_UNUSED(index);

    if (m_ui->comboTTM->currentIndex() == 1)
    {
        QString savePath = BitTorrent::Session::instance()->categorySavePath(m_ui->categoryComboBox->currentText());
        m_ui->savePath->setSelectedPath(Utils::Fs::toNativePath(savePath));
        updateDiskSpaceLabel();
    }
}

void AddNewTorrentDialog::setSavePath(const QString &newPath)
{
    int existingIndex = indexOfSavePath(newPath);
    if (existingIndex < 0)
    {
        // New path, prepend to combo box
        m_ui->savePath->insertItem(0, newPath);
        existingIndex = 0;
    }
    m_ui->savePath->setCurrentIndex(existingIndex);
    onSavePathChanged(newPath);
}

void AddNewTorrentDialog::saveTorrentFile()
{
    Q_ASSERT(m_hasMetadata);

    const QString torrentFileExtension {C_TORRENT_FILE_EXTENSION};
    const QString filter {tr("Torrent file (*%1)").arg(torrentFileExtension)};

    QString path = QFileDialog::getSaveFileName(
                this, tr("Save as torrent file")
                , QDir::home().absoluteFilePath(m_torrentInfo.name() + torrentFileExtension)
                , filter);
    if (path.isEmpty()) return;

    if (!path.endsWith(torrentFileExtension, Qt::CaseInsensitive))
        path += torrentFileExtension;

    const nonstd::expected<void, QString> result = m_torrentInfo.saveToFile(path);
    if (!result)
    {
        QMessageBox::critical(this, tr("I/O Error")
            , tr("Couldn't export torrent metadata file '%1'. Reason: %2.").arg(path, result.error()));
    }
}

void AddNewTorrentDialog::populateSavePathComboBox()
{
    m_ui->savePath->clear();

    // Load save path history
    const auto savePathHistory {settings()->loadValue<QStringList>(KEY_SAVEPATHHISTORY)};
    for (const QString &savePath : savePathHistory)
        m_ui->savePath->addItem(savePath);

    const bool rememberLastSavePath {settings()->loadValue(KEY_REMEMBERLASTSAVEPATH, false)};
    const QString defSavePath {BitTorrent::Session::instance()->defaultSavePath()};

    if (!m_torrentParams.savePath.isEmpty())
        setSavePath(m_torrentParams.savePath);
    else if (!rememberLastSavePath)
        setSavePath(defSavePath);
    // else last used save path will be selected since it is the first in the list
}

void AddNewTorrentDialog::displayContentTreeMenu(const QPoint &)
{
    const QModelIndexList selectedRows = m_ui->contentTreeView->selectionModel()->selectedRows(0);

    const auto applyPriorities = [this](const BitTorrent::DownloadPriority prio)
    {
        const QModelIndexList selectedRows = m_ui->contentTreeView->selectionModel()->selectedRows(0);
        for (const QModelIndex &index : selectedRows)
        {
            m_contentModel->setData(index.sibling(index.row(), PRIORITY)
                , static_cast<int>(prio));
        }
    };
    const auto applyPrioritiesByOrder = [this]()
    {
        // Equally distribute the selected items into groups and for each group assign
        // a download priority that will apply to each item. The number of groups depends on how
        // many "download priority" are available to be assigned

        const QModelIndexList selectedRows = m_ui->contentTreeView->selectionModel()->selectedRows(0);

        const qsizetype priorityGroups = 3;
        const auto priorityGroupSize = std::max<qsizetype>((selectedRows.length() / priorityGroups), 1);

        for (qsizetype i = 0; i < selectedRows.length(); ++i)
        {
            auto priority = BitTorrent::DownloadPriority::Ignored;
            switch (i / priorityGroupSize)
            {
            case 0:
                priority = BitTorrent::DownloadPriority::Maximum;
                break;
            case 1:
                priority = BitTorrent::DownloadPriority::High;
                break;
            default:
            case 2:
                priority = BitTorrent::DownloadPriority::Normal;
                break;
            }

            const QModelIndex &index = selectedRows[i];
            m_contentModel->setData(index.sibling(index.row(), PRIORITY)
                , static_cast<int>(priority));
        }
    };

    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (selectedRows.size() == 1)
    {
        menu->addAction(UIThemeManager::instance()->getIcon("edit-rename"), tr("Rename...")
            , this, [this]() { m_ui->contentTreeView->renameSelectedFile(m_torrentInfo); });
        menu->addSeparator();

        QMenu *priorityMenu = menu->addMenu(tr("Priority"));
        priorityMenu->addAction(tr("Do not download"), priorityMenu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Ignored);
        });
        priorityMenu->addAction(tr("Normal"), priorityMenu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Normal);
        });
        priorityMenu->addAction(tr("High"), priorityMenu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::High);
        });
        priorityMenu->addAction(tr("Maximum"), priorityMenu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Maximum);
        });
        priorityMenu->addSeparator();
        priorityMenu->addAction(tr("By shown file order"), priorityMenu, applyPrioritiesByOrder);
    }
    else
    {
        menu->addAction(tr("Do not download"), menu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Ignored);
        });
        menu->addAction(tr("Normal priority"), menu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Normal);
        });
        menu->addAction(tr("High priority"), menu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::High);
        });
        menu->addAction(tr("Maximum priority"), menu, [applyPriorities]()
        {
            applyPriorities(BitTorrent::DownloadPriority::Maximum);
        });
        menu->addSeparator();
        menu->addAction(tr("Priority by shown file order"), menu, applyPrioritiesByOrder);
    }

    menu->popup(QCursor::pos());
}

void AddNewTorrentDialog::accept()
{
    // TODO: Check if destination actually exists
    m_torrentParams.skipChecking = m_ui->skipCheckingCheckBox->isChecked();

    // Category
    m_torrentParams.category = m_ui->categoryComboBox->currentText();
    if (m_ui->defaultCategoryCheckbox->isChecked())
        settings()->storeValue(KEY_DEFAULTCATEGORY, m_torrentParams.category);

    settings()->storeValue(KEY_REMEMBERLASTSAVEPATH, m_ui->checkBoxRememberLastSavePath->isChecked());

    // Save file priorities
    if (m_contentModel)
        m_torrentParams.filePriorities = m_contentModel->model()->getFilePriorities();

    m_torrentParams.addPaused = !m_ui->startTorrentCheckBox->isChecked();
    m_torrentParams.contentLayout = static_cast<BitTorrent::TorrentContentLayout>(m_ui->contentLayoutComboBox->currentIndex());

    m_torrentParams.sequential = m_ui->sequentialCheckBox->isChecked();
    m_torrentParams.firstLastPiecePriority = m_ui->firstLastCheckBox->isChecked();

    QString savePath = m_ui->savePath->selectedPath();
    if (m_ui->comboTTM->currentIndex() != 1)
    { // 0 is Manual mode and 1 is Automatic mode. Handle all non 1 values as manual mode.
        m_torrentParams.useAutoTMM = false;
        m_torrentParams.savePath = savePath;
        saveSavePathHistory();
    }
    else
    {
        m_torrentParams.useAutoTMM = true;
    }

    setEnabled(!m_ui->checkBoxNeverShow->isChecked());

    // Add torrent
    if (!m_hasMetadata)
        BitTorrent::Session::instance()->addTorrent(m_magnetURI, m_torrentParams);
    else
        BitTorrent::Session::instance()->addTorrent(m_torrentInfo, m_torrentParams);

    m_torrentGuard->markAsAddedToSession();
    QDialog::accept();
}

void AddNewTorrentDialog::reject()
{
    if (!m_hasMetadata)
    {
        setMetadataProgressIndicator(false);
        BitTorrent::Session::instance()->cancelDownloadMetadata(m_magnetURI.infoHash().toTorrentID());
    }

    QDialog::reject();
}

void AddNewTorrentDialog::updateMetadata(const BitTorrent::TorrentInfo &metadata)
{
    if (metadata.infoHash() != m_magnetURI.infoHash()) return;

    disconnect(BitTorrent::Session::instance(), &BitTorrent::Session::metadataDownloaded, this, &AddNewTorrentDialog::updateMetadata);

    if (!metadata.isValid())
    {
        RaisedMessageBox::critical(this, tr("I/O Error"), ("Invalid metadata."));
        setMetadataProgressIndicator(false, tr("Invalid metadata"));
        return;
    }

    // Good to go
    m_torrentInfo = metadata;
    m_hasMetadata = true;
    setMetadataProgressIndicator(true, tr("Parsing metadata..."));

    // Update UI
    setupTreeview();
    setMetadataProgressIndicator(false, tr("Metadata retrieval complete"));

    m_ui->buttonSave->setVisible(true);
    if (m_torrentInfo.infoHash().v2().isValid())
    {
        m_ui->buttonSave->setEnabled(false);
        m_ui->buttonSave->setToolTip(tr("Cannot create v2 torrent until its data is fully downloaded."));
    }
}

void AddNewTorrentDialog::setMetadataProgressIndicator(bool visibleIndicator, const QString &labelText)
{
    // Always show info label when waiting for metadata
    m_ui->lblMetaLoading->setVisible(true);
    m_ui->lblMetaLoading->setText(labelText);
    m_ui->progMetaLoading->setVisible(visibleIndicator);
}

void AddNewTorrentDialog::setupTreeview()
{
    if (!m_hasMetadata)
    {
        m_ui->labelCommentData->setText(tr("Not Available", "This comment is unavailable"));
        m_ui->labelDateData->setText(tr("Not Available", "This date is unavailable"));
    }
    else
    {
        // Set dialog title
        setWindowTitle(m_torrentInfo.name());

        // Set torrent information
        m_ui->labelCommentData->setText(Utils::Misc::parseHtmlLinks(m_torrentInfo.comment().toHtmlEscaped()));
        m_ui->labelDateData->setText(!m_torrentInfo.creationDate().isNull() ? QLocale().toString(m_torrentInfo.creationDate(), QLocale::ShortFormat) : tr("Not available"));

        // Prepare content tree
        m_contentModel = new TorrentContentFilterModel(this);
        connect(m_contentModel->model(), &TorrentContentModel::filteredFilesChanged, this, &AddNewTorrentDialog::updateDiskSpaceLabel);
        m_ui->contentTreeView->setModel(m_contentModel);
        m_contentDelegate = new PropListDelegate(nullptr);
        m_ui->contentTreeView->setItemDelegate(m_contentDelegate);
        connect(m_ui->contentTreeView, &QAbstractItemView::clicked, m_ui->contentTreeView
                , qOverload<const QModelIndex &>(&QAbstractItemView::edit));
        connect(m_ui->contentTreeView, &QWidget::customContextMenuRequested, this, &AddNewTorrentDialog::displayContentTreeMenu);

        // List files in torrent
        m_contentModel->model()->setupModelData(m_torrentInfo);
        if (!m_headerState.isEmpty())
            m_ui->contentTreeView->header()->restoreState(m_headerState);

        // Hide useless columns after loading the header state
        m_ui->contentTreeView->hideColumn(PROGRESS);
        m_ui->contentTreeView->hideColumn(REMAINING);
        m_ui->contentTreeView->hideColumn(AVAILABILITY);

        // Expand single-item folders recursively
        QModelIndex currentIndex;
        while (m_contentModel->rowCount(currentIndex) == 1)
        {
            currentIndex = m_contentModel->index(0, 0, currentIndex);
            m_ui->contentTreeView->setExpanded(currentIndex, true);
        }
    }

    updateDiskSpaceLabel();
}

void AddNewTorrentDialog::handleDownloadFinished(const Net::DownloadResult &downloadResult)
{
    switch (downloadResult.status)
    {
    case Net::DownloadStatus::Success:
        {
            const nonstd::expected<BitTorrent::TorrentInfo, QString> result = BitTorrent::TorrentInfo::load(downloadResult.data);
            m_torrentInfo = result.value_or(BitTorrent::TorrentInfo());
            if (!result)
            {
                RaisedMessageBox::critical(this, tr("Invalid torrent"), tr("Failed to load from URL: %1.\nError: %2")
                                           .arg(downloadResult.url, result.error()));
                return;
            }

            m_torrentGuard = std::make_unique<TorrentFileGuard>();

            if (loadTorrentImpl())
                open();
            else
                deleteLater();
        }
        break;
    case Net::DownloadStatus::RedirectedToMagnet:
        if (loadMagnet(BitTorrent::MagnetUri(downloadResult.magnet)))
            open();
        else
            deleteLater();
        break;
    default:
        RaisedMessageBox::critical(this, tr("Download Error"),
            tr("Cannot download '%1': %2").arg(downloadResult.url, downloadResult.errorString));
        deleteLater();
    }
}

void AddNewTorrentDialog::TMMChanged(int index)
{
    if (index != 1)
    { // 0 is Manual mode and 1 is Automatic mode. Handle all non 1 values as manual mode.
        populateSavePathComboBox();
        m_ui->groupBoxSavePath->setEnabled(true);
        m_ui->savePath->blockSignals(false);
        m_ui->savePath->setCurrentIndex(m_oldIndex < m_ui->savePath->count() ? m_oldIndex : m_ui->savePath->count() - 1);
    }
    else
    {
        m_ui->groupBoxSavePath->setEnabled(false);
        m_ui->savePath->blockSignals(true);
        m_ui->savePath->clear();
        QString savePath = BitTorrent::Session::instance()->categorySavePath(m_ui->categoryComboBox->currentText());
        m_ui->savePath->addItem(savePath);
        updateDiskSpaceLabel();
    }
}

void AddNewTorrentDialog::doNotDeleteTorrentClicked(bool checked)
{
    m_torrentGuard->setAutoRemove(!checked);
}
