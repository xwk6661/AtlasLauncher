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

#include <QList>
#include <QString>
#include <QStringList>

namespace maps {

/// 大厅查询条件（对应 AIO 的 SearchOpts）
struct HallQuery {
    QString text;
    /// 系列（精确匹配）；空 = 不限。值来自 facets，不写死枚举
    QString series;
    QStringList difficulties;
    QStringList lengths;
    QString edition;
    QString year;
    int ratingMin = 0;
    int playtimeMax = 0;
    /// release_desc | release_asc | rating_desc | rating_asc | popularity_desc | popularity_asc | playtime_desc | playtime_asc
    QString sort = QStringLiteral("release_desc");
    int offset = 0;
    int limit = 20;
};

/// 列表里的一行
struct HallItem {
    QString id;
    QString slug;
    /// `user_maps` / 评论都按这个 UUID 关联（不是 slug）
    QString mapUuid;
    QString title;
    QString subtitle;
    QString author;
    QStringList creators;
    QStringList tags;
    QString coverUrl;
    QString pageUrl;
    QString downloadUrl;
    QString meta;

    QString series;
    int seriesNum = -1;
    QString edition;
    QString version;
    QString difficulty;
    QString length;
    int objectives = -1;
    int bonuses = -1;
    double rating = -1;
    double minutes = -1;
    int plays = -1;
    QString releaseDate;
    bool lostMedia = false;
};

/// 一条游玩记录里的评论（`user_maps` 表）
struct HallReview {
    QString id;
    /// 评论所属地图（`user_maps.map_uuid`）：本地缓存按它分组
    QString mapUuid;
    QString userUuid;
    /// 显示名，由 users 表补；取不到时界面显示「匿名玩家」
    QString userName;
    int rating = -1;
    QString text;
    QString playedDate;
    int playtimeMinutes = -1;
    int deaths = -1;
    int bonusObjectives = -1;
    int hearts = -1;
    bool speedrun = false;
};

/// 详情页
struct HallDetail {
    HallItem item;
    QString description;
    QString trailerUrl;
    QStringList downloadLinks;
};

/// 筛选选项（从整表里抓出现过的值，本地去重）
struct HallFacets {
    QStringList series;
    QStringList difficulties;
    QStringList lengths;
    QStringList editions;
    QStringList years;
};

/// 网络失败分类：界面要给「人话」，技术细节折叠
enum class HallError {
    None,
    Dns,
    Tls,
    /// 连接被对端直接关掉 / 重置：没拿到任何 HTTP 响应。
    /// 和 Http 分开，是因为"站点拒绝了这次请求"和"根本没连上"是两回事，
    /// 提示和后续动作完全不同（前者等一会儿，后者只能换出口）。
    Blocked,
    Timeout,
    Http,
    Empty,
    Parse,
};

QString hallErrorToString(HallError error);

}  // namespace maps
