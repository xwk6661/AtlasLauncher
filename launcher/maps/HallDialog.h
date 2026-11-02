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

#include <QDialog>
#include <QHash>
#include <QPixmap>
#include <QSet>

#include "HallTypes.h"

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QListWidget;

namespace maps {

class HallAPI;

/**
 * 地图大厅：搜索 / 看详情 / 打开下载页。
 *
 * **不在启动器里下载**：`downloadUrl` 一律交给系统浏览器。
 * AIO 的教训（§40）：分享页的 Cloudflare 只认可见浏览器，七种「藏窗口」方案全部被 403 打回。
 */
class HallDialog : public QDialog {
    Q_OBJECT

   public:
    explicit HallDialog(QWidget* parent = nullptr);

   private slots:
    void startSearch();
    void loadMoreResults();
    void openSelected();
    void showDetails(int row, int column);
    void onFailed(maps::HallError kind, const QString& humanReason, const QString& techDetail);
    void applyFacets(const maps::HallFacets& facets);
    void loadMoreReviews();
    void onReviews(const QList<maps::HallReview>& rows, int total, bool totalKnown, int offset);
    void onUserNames(const QHash<QString, QString>& nameByUuid);

   private:
    /// 把界面上的筛选/排序/搜索框收集成一次查询（分页时只改 offset）
    HallQuery currentQuery() const;
    /// 异步取封面并按 url 回填所在行；同一 url 只请求一次
    void requestCover(const QString& url);
    void applyCoverIcon(const QString& url);

    HallAPI* m_api = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QComboBox* m_seriesCombo = nullptr;
    QComboBox* m_difficultyCombo = nullptr;
    QComboBox* m_lengthCombo = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    /// 列表分页：「加载更多」按当前 offset 再取一页（站方单页上限 60）
    QPushButton* m_loadMore = nullptr;
    bool m_loadingMore = false;
    QListWidget* m_reviews = nullptr;
    QLabel* m_reviewsStatus = nullptr;
    QPushButton* m_moreReviews = nullptr;
    QList<HallReview> m_reviewRows;
    int m_reviewTotal = -1;
    QString m_currentMapUuid;
    QPushButton* m_openButton = nullptr;
    QList<HallItem> m_items;
    /// 详情按 slug 存：点过 A 再点 B 时，B 失败不会让「打开下载页」误开到 A
    QHash<QString, HallDetail> m_details;
    /// 封面缩略图缓存：原图动辄几 MB，缩到卡片尺寸再存（AIO 实测 158 MiB → 2.98 MiB）
    QHash<QString, QPixmap> m_coverCache;
    QSet<QString> m_coverPending;
    /// 当前筛选条件（搜索时会填 text）
    HallQuery m_query;
};

}  // namespace maps
