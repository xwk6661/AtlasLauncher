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

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QWidget>

#include "HallTypes.h"
#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace maps {

class HallAPI;

/**
 * 「新建实例 → CTM 地图」页：把地图大厅搬进新建流程。
 *
 * 形态照 Modrinth / CurseForge 两个页：左边列表（封面 + 标题 + 一句话），
 * 右边详情（大封面 / 11 项属性 / 简介 / 两个动作）。
 *
 * 两个动作对应 AIO 大厅那套"只发现、不代下"的约定：
 *   · 「打开下载页」—— 交给系统浏览器（Cloudflare 只认可见浏览器，AIO §40 的教训）；
 *   · 「选择已下载的文件」—— 选完就走一遍扫描 + 版本判定 + 导入确认，
 *     确认后把 MapImportTask 交给对话框，「确定」按钮就变成"建实例"。
 */
class MapHallPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit MapHallPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);

    QString id() const override { return QStringLiteral("ctmmaps"); }
    QString displayName() const override { return tr("CTM 地图"); }
    QIcon icon() const override;
    QString helpPage() const override { return {}; }
    void openedImpl() override;

   private:
    void buildUi();
    void startSearch();
    void loadMore();
    void onSearchDone(const QList<HallItem>& items);
    void onDetail(const HallDetail& detail);
    void onFailed(HallError kind, const QString& humanReason, const QString& techDetail);
    void onFacets(const HallFacets& facets);
    void showRow(int index);
    /// 「我的」页：本机哪张实例关联了这张图，它的评分/笔记/统计如何
    void refreshMineTab();
    /// 评论：一页 6 条，翻页靠 offset（浏览器通道拿不到总数时只显示已加载的条数）
    void requestReviews(int offset);
    void onReviews(const QList<HallReview>& rows, int total, bool totalKnown, int offset);
    void onUserNames(const QHash<QString, QString>& names);
    void clearReviews();
    /// 把一条评论排成卡片（名字 + 星级 + 游玩时长 + 日期 + 正文）
    QWidget* buildReviewCard(const HallReview& review);
    void requestCover(const QString& url);
    /// 真图到手：回填所有引用它的行 + 当前详情头图
    void applyCover(const QString& url, const QPixmap& pixmap);
    /// 同步本地库（首次自动，之后由「更新数据」触发）
    void syncLibrary();
    void updateLibraryHint();
    void pickLocalArchive();
    /// 空态面板：照 AIO 的 renderHallFail（标题 / 说明 / 三条建议 / 四个动作 / 技术细节）
    void showFailure(HallError kind, const QString& reason, const QString& detail);
    void runProbe();
    /// 「反代模板」：把可直接粘贴到 Cloudflare Workers 的 20 行代码摊开，附一键复制
    void showRelayHelp();

    HallQuery currentQuery() const;
    /// 优先点开详情里的直链，没有就退回站点页面
    QString currentDownloadUrl() const;

    NewInstanceDialog* m_dialog = nullptr;
    HallAPI* m_api = nullptr;

    QLineEdit* m_search = nullptr;
    QComboBox* m_sort = nullptr;
    QComboBox* m_series = nullptr;
    QComboBox* m_difficulty = nullptr;
    /// 长度（short/medium/long/very long）与年份：站上就有的两个维度，本地筛选早已支持，只是以前没接 UI
    QComboBox* m_length = nullptr;
    QComboBox* m_year = nullptr;
    /// 一键清空所有筛选条件（搜索框 + 排序 + 四个下拉）
    QToolButton* m_resetFilters = nullptr;
    QListWidget* m_list = nullptr;
    /// 左栏在「列表 / 失败面板」之间切换
    QStackedWidget* m_leftStack = nullptr;
    QWidget* m_failBox = nullptr;
    QLabel* m_failTitle = nullptr;
    QLabel* m_failLead = nullptr;
    QLabel* m_failTips = nullptr;
    QLabel* m_failDetail = nullptr;
    QLabel* m_probeResult = nullptr;
    QToolButton* m_failDetailToggle = nullptr;
    QPushButton* m_relayButton = nullptr;
    QPushButton* m_loadMore = nullptr;
    /// 「更新数据」/「下载地图库」
    QPushButton* m_syncButton = nullptr;
    QLabel* m_status = nullptr;
    bool m_syncing = false;

    QLabel* m_cover = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    /// 详情顶部的一行统计 chip（★ 评分 / 平均时长 / 游玩人数），照 AIO hall-detail 的 stat
    QWidget* m_statBox = nullptr;
    QHBoxLayout* m_statRow = nullptr;
    QLabel* m_facts = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_hint = nullptr;

    // ── 评论（详情面板底部） ──
    /// 简介 / 评论 / 我的 三个页签（各自独占一屏）
    class QTabWidget* m_tabs = nullptr;
    int m_reviewsTabIndex = 1;
    QLabel* m_mineState = nullptr;
    QWidget* m_reviewBox = nullptr;
    QVBoxLayout* m_reviewLayout = nullptr;
    QPushButton* m_reviewMore = nullptr;
    QList<HallReview> m_reviews;
    /// 当前详情属于哪张图（翻页/补名字都用它，避免切换选中时串数据）
    QString m_reviewMapUuid;
    int m_reviewTotal = -1;
    bool m_reviewTotalKnown = false;
    bool m_reviewsLoading = false;
    /// 名字是异步补的：uuid → 那条评论里的名字 label
    QHash<QString, QLabel*> m_reviewNameLabels;

    QList<HallItem> m_items;
    QHash<QString, HallDetail> m_details;
    QHash<QString, QPixmap> m_covers;
    QSet<QString> m_coverPending;
    /// 已经确认取不到的封面 URL：别再反复请求（每次都要等超时）
    QSet<QString> m_coverFailed;
    bool m_loadingMore = false;
    /// 第一次被选中才发请求（没人点这个页签就不占带宽）
    bool m_started = false;
    /// facets 只取一次；且必须等搜索回来再发（HallAPI 同一时刻只允许一个请求）
    bool m_facetsRequested = false;
};

}  // namespace maps
