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
#pragma once

#include <QWidget>

#include "MapMeta.h"
#include "MapHallLink.h"
#include "MapNotes.h"
#include "ui/pages/BasePage.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QPushButton;
class MinecraftInstance;

namespace maps {

class HallAPI;
struct HallItem;

/// 五星评分控件（半星粒度，内部 1~10），定义在 MapInstancePage.cpp
class StarRatingWidget;

/**
 * 实例窗口里的「信息」页签：map.json 部分**只读**，「我的记录」可编辑。
 *
 * 不列"世界目录"：一个地图对应一个实例，实例里只有它自己的 saves/<世界>，
 * 把「实例内目录 ↔ 压缩包内路径」这张表摆出来只会让人以为要手动搬目录。
 *
 * 为什么要分开：
 *   · map.json 是导入时判定的留痕 —— 在这里改了就无法复现"当时凭什么判成这个版本"。
 *     要改版本请走「重装地图」；要删就删实例。
 *   · 中文名 / 我的评分 / 笔记是**玩家自己的数据**（notes.json），照 AIO 的分工，
 *     任何自动流程（重装、重导入）都不该覆盖它们，所以给编辑入口。
 */
class MapInstancePage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit MapInstancePage(MinecraftInstance* instance, QWidget* parent = nullptr);

    QString id() const override { return QStringLiteral("map"); }
    QString displayName() const override;
    QIcon icon() const override;
    /// 只有带 map.json 的实例才显示这一页
    bool shouldDisplay() const override;

    void openedImpl() override;

   private:
    void reload();
    void reinstallWorlds();

    // ── 「简介 / 评论」两个页签：读本地大厅库，没关联就说明原因 ──
    QWidget* buildAboutPage();
    QWidget* buildReviewsPage();
    void refreshHallTabs();
    /// 简介页封面：先画占位、再异步换真图（拿不到就一直是占位，不留空白）
    void requestAboutCover(const QString& url, const QString& title);
    void applyAboutCover(const QImage& image);
    void pickHallMap();
    void linkTo(const HallItem& item);
    void ensureHallApi();

    /// 把 notes.json 的当前值刷到界面（中文名 / 星级 / 笔记 / 状态文字）
    void applyNotesToUi();
    /// 写 notes.json 并刷新状态文字
    void saveNotes();

    MinecraftInstance* m_instance = nullptr;
    MapMeta m_meta;
    MapNotes m_notes;

    QLineEdit* m_aliasEdit = nullptr;
    QPushButton* m_aliasSave = nullptr;
    QLabel* m_aliasState = nullptr;
    StarRatingWidget* m_stars = nullptr;
    QLabel* m_ratingValue = nullptr;
    QPlainTextEdit* m_noteEdit = nullptr;
    QPushButton* m_noteSave = nullptr;
    QLabel* m_noteCount = nullptr;
    QLabel* m_noteState = nullptr;

    QLabel* m_title = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_archive = nullptr;
    QLabel* m_stats = nullptr;
    QLabel* m_evidence = nullptr;
    /// 实验性设置 / 缺加载器 这类「不做就不能正常玩」的提示
    QLabel* m_notice = nullptr;

    // ── 大厅联动 ──
    QTabWidget* m_tabs = nullptr;
    /// 「简介」页
    QWidget* m_aboutPage = nullptr;
    QVBoxLayout* m_aboutLayout = nullptr;
    /// 简介页顶部：封面在左、基本信息在右（2026-11-02 按用户要求排版）
    QLabel* m_aboutCover = nullptr;
    QLabel* m_aboutInfo = nullptr;
    /// 当前封面 URL：异步回图时用它判断是不是这张图，避免快速切图串味
    QString m_aboutCoverUrl;
    QLabel* m_aboutState = nullptr;
    QPushButton* m_aboutLinkButton = nullptr;
    QPushButton* m_aboutUnlinkButton = nullptr;
    /// 评论是"点开评论页才拉"（按图请求要几秒，别在打开页面时就卡住）
    bool m_reviewsRequested = false;
    /// 「评论」页
    QWidget* m_reviewsPage = nullptr;
    QVBoxLayout* m_reviewsLayout = nullptr;
    QPushButton* m_reviewsMore = nullptr;
    QLabel* m_reviewsState = nullptr;
    HallAPI* m_hall = nullptr;
    MapHallLink m_link;
    /// 当前「简介/评论」属于哪张图（异步回来时防止串数据）
    QString m_hallSlug;
    QString m_hallUuid;
    int m_reviewsShown = 0;
};

}  // namespace maps
