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

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace maps {

/**
 * 大厅数据快照（本地缓存）。
 *
 * ## 为什么要有它
 *
 * 这条线路上每次请求都得借无头浏览器，一次 3–10 秒（见 `BrowserFetch`）。
 * 按「点一次刷新一次」的用法，翻十张图要等一两分钟 —— 实测就是用户抱怨的那样。
 *
 * 而 CTMMaps 的数据量并不大：几百张图 + 一千来条评论。所以正确做法是
 * **一次性全下来存本地**，搜索/排序/翻页/看详情全部本地算，只在用户点
 * 「更新数据」时再拉一次。
 *
 * ## 存的是什么
 *
 * 存**接口原始行**（QJsonObject），不存解析后的结构：站方改字段时，
 * 解析逻辑（`itemFromJson` / `reviewFromJson`）改一处就够，缓存不用跟着迁移。
 */
struct HallSnapshot {
    QDateTime fetchedAt;
    /// 视图 `maps_search_view` 的行（列表：评分/人数/时长这些聚合列只在这）
    QJsonArray listRows;
    /// 表 `maps` 的行（详情：description_1..3 / download_links / trailer 只在这）
    QJsonArray detailRows;
    /// 表 `users` 的行（评论者名字）
    QJsonArray userRows;
    /// 表 `user_maps` 的行（评论）
    QJsonArray reviewRows;

    bool isValid() const { return fetchedAt.isValid() && !listRows.isEmpty(); }
};

/**
 * 快照的读写。
 *
 * 位置：`<数据目录>/maps/hall-cache.json` —— 跟地图库放一起，
 * 备份/迁移数据目录时不会漏掉它。
 */
class HallCache {
   public:
    static QString path();

    static HallSnapshot load();
    /// 写盘（原子替换：先写 .part 再 rename，避免半个文件把缓存弄坏）
    static bool save(const HallSnapshot& snapshot, QString* error = nullptr);

/**
 * 详情覆盖检查：列表里每个 slug 都有对应详情行才算完整。
 *
 * 为什么要它：旧版同步的详情按 maps 表默认顺序取前 1000，列表却按 sort_date
 * 取前 1000 —— 两表排序不同，集合差一大块；缺详情的图点开只能回落网络
 * （这条线路上就是"一直加载不出来"）。载入缓存后用它判断要不要自动重同步。
 */
static bool detailsCoverList(const HallSnapshot& snapshot);
    static void clear();

    /// 缓存多大（给设置页显示"已缓存 X MB"）
    static qint64 sizeBytes();

    /**
     * 按 slug 从本地快照里取一行原始数据（列表视图 `maps_search_view` 的行）。
     *
     * 给「实例侧栏要显示作者/系列」这类同步场合用：只读本地、不联网，
     * 解析结果按文件 mtime 缓存在进程内，反复刷新侧栏不会每次都啃 2.6 MB JSON。
     * 没缓存、没命中都返回空对象 —— 调用方据此决定"不显示"。
     */
    static QJsonObject findListRow(const QString& slug);
};

}  // namespace maps
