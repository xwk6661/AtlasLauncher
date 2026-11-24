// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "LocalLibraryPage.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "MapArchiveScanner.h"
#include "HallLinkReviewDialog.h"
#include "MapImportDialog.h"
#include "MapImportTask.h"
#include "MapLibraryPanel.h"
#include "MapPageIcons.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"

namespace maps {

LocalLibraryPage::LocalLibraryPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    m_library = new MapLibraryPanel(this);
    connect(m_library, &MapLibraryPanel::importRequested, this, &LocalLibraryPage::importFromPath);
    root->addWidget(m_library, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->addStretch(1);

    auto* importButton = new QPushButton(tr("从外部导入…"), this);
    importButton->setToolTip(tr("选一个压缩包导入，导入后自动收进本库"));
    bottomRow->addWidget(importButton);
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString filter = tr("Minecraft 地图 (*.zip *.rar *.7z *.tar *.gz *.mcworld);;所有文件 (*)");
        const QString file = QFileDialog::getOpenFileName(this, tr("选择地图压缩包"), QString(), filter);
        if (!file.isEmpty())
            importFromPath(file);
    });

    /**
     * 「待确认关联」：自动匹配只写 `verified:false`，人工确认入口原来只有实例页
     * 一条条点。现在把未确认的十几条一次列出来批量处理（[04 P1 #7]）。
     */
    m_reviewButton = new QPushButton(tr("待确认关联…"), this);
    m_reviewButton->setToolTip(tr("把自动匹配、还没人工确认的关联一次列出来：\n"
                                  "认可就批量确认；不认可就改归属或解除。"));
    bottomRow->addWidget(m_reviewButton);
    connect(m_reviewButton, &QPushButton::clicked, this, [this] {
        HallLinkReviewDialog::review(m_library->library(), this);
        m_library->refresh();
        updateReviewButton();
    });

    auto* normalizeButton = new QPushButton(tr("规范命名…"), this);
    normalizeButton->setToolTip(tr("把库内文件统一改成「[MC 版本]英文原名 地图版本」；\n"
                                   "索引键与实例里记的压缩包路径会一起更新"));
    bottomRow->addWidget(normalizeButton);
    connect(normalizeButton, &QPushButton::clicked, this, [this] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("规范命名"));
        box.setText(tr("把库里的文件名统一成下面这种形式？"));
        box.setInformativeText(tr("[MC 版本]英文原名 地图版本.zip\n\n"
                                  "只改文件名，不动压缩包内容；索引与实例记录的路径会一起更新。"));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.button(QMessageBox::Yes)->setText(tr("重命名"));
        box.button(QMessageBox::Cancel)->setText(tr("取消"));
        if (box.exec() != QMessageBox::Yes)
            return;

        QString error;
        const int changed = m_library->normalizeNames(&error);
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("规范命名"), error);
        else if (changed == 0)
            QMessageBox::information(this, tr("规范命名"), tr("文件名已符合规范"));
        else
            QMessageBox::information(this, tr("规范命名"), tr("已重命名 %1 个文件。").arg(changed));
    });

    root->addLayout(bottomRow);
}

QIcon LocalLibraryPage::icon() const
{
    // 书脊 + 抽屉：库是"我手上有哪些包"，与大厅的地球图标区分
    return pageIcons::library();
}

void LocalLibraryPage::openedImpl()
{
    if (m_library == nullptr)
        return;

    // 每次打开重扫一遍：玩家可能刚往库目录里丢过包
    m_library->refresh();

    /**
     * 首次打开顺手把"还没关联"的包按名称挂到 CTM 条目上（只写 `verified:false`）。
     *
     * 关联之后列表按站上 slug 分组、"CTM 地图"页也能反查本地包 —— 而玩家
     * 之前是一个个右键挂的。判据很严（见 `MapLibrary::autoLinkUnassigned`），
     * 宁可少关联也不关联错；错的可以右键解除。
     */
    if (!m_autoLinkedOnce) {
        m_autoLinkedOnce = true;
        if (m_library->autoLinkUnassigned() > 0)
            m_library->refresh();
    }

    updateReviewButton();
}

void LocalLibraryPage::updateReviewButton()
{
    if (m_reviewButton == nullptr || m_library == nullptr)
        return;

    const int pending = m_library->unverifiedLinkCount();
    m_reviewButton->setText(pending > 0 ? tr("待确认关联 (%1)…").arg(pending) : tr("待确认关联…"));
    // 0 条时置灰：点进去也只是一句「都确认过了」，不如直接不给点
    m_reviewButton->setEnabled(pending > 0);
}

void LocalLibraryPage::importFromPath(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    /**
     * 扫描要打开归档、遍历条目、读 `level.dat`：大包在真机上要几秒。
     * 以前是同步调用 + 等待光标 —— 那几秒里整个窗口都不动（审查 P1-5）。
     * 现在丢线程池，扫完再回 UI 线程弹对话框。
     */
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto* watcher = new QFutureWatcher<Result<MapScanResult>>(this);
    connect(watcher, &QFutureWatcher<Result<MapScanResult>>::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        QApplication::restoreOverrideCursor();

        const auto scanned = watcher->result();
        if (!scanned || scanned->worlds.isEmpty()) {
            CustomMessageBox::selectable(this, tr("不是地图存档"),
                                         tr("这个文件里没有找到包含 level.dat 的 Minecraft 世界。\n"
                                            "如果它其实是整合包，请到「导入」页当作整合包导入。"),
                                         QMessageBox::Warning)
                ->show();
            return;
        }

        // 与导入页同一条路：确认版本 / 加载器 / 关联 / 语言，点导入就建实例
        const auto options = MapImportDialog::ask(*scanned, this);
        if (!options)
            return;

        auto chosen = *options;
        if (chosen.displayName.trimmed().isEmpty())
            chosen.displayName = QFileInfo(path).completeBaseName();

        if (m_dialog == nullptr)
            return;
        m_dialog->setSuggestedPack(chosen.displayName, new MapImportTask(path, *scanned, chosen));
        m_dialog->setSuggestedIcon("default");
        m_dialog->accept();
    });

    watcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(),
                                         [path]() { return MapArchiveScanner::scan(path); }));
}

}  // namespace maps
