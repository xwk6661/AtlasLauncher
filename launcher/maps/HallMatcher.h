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

#include <QJsonArray>
#include <QList>
#include <QString>

namespace maps {

/// 一条候选：站上的某张图 + 为什么像
struct HallMatchCandidate {
    QString slug;
    QString name;
    QString version;  ///< 站上标的 MC 版本（可能是 "1.20.1 Forge"）
    QString series;
    double score = 0.0;
    QString reason;
};

struct HallMatchQuery {
    QString englishName;
    QString chineseName;
    QString fileName;
    QString mcVersion;  ///< 本地判出来的版本，可空
};

/**
 * 把本地压缩包关联到 CTM Maps 的某张图（slug）。
 *
 * 为什么需要它：索引按**文件名**做键，而文件名是人写的 ——
 * 同一张图的汉化版常常叫「Torre de Piedra v1.2 石之恐惧（简中汉化v1.0）」，
 * 原版叫「Torre de Piedra」，英文名甚至写成 `Bitstream - by Gibbs & Asometric`。
 * 靠字符串相等只能配上少数几组（实测 6 组里只配上 2 组）。
 *
 * 所以这里做三件事：归一化、打分、**给理由**。理由会原样显示在界面上，
 * 让玩家判断"这确实是同一张图"，而不是让程序替他决定 —— 自动结果永远只是建议，
 * 只有人工确认过才写进索引（`hall.verified = true`）。
 *
 * 纯函数 `rank()` 不碰文件，单测用它；`suggest()` 只是读一次大厅缓存再调 `rank()`。
 */
class HallMatcher {
   public:
    /// 读一次 `hall-cache.json` 并排序（大厅没同步过时返回空）
    static QList<HallMatchCandidate> suggest(const HallMatchQuery& query, int max = 3);

    /// 纯函数：`rows` 是 `HallSnapshot::listRows`（接口原始行）
    static QList<HallMatchCandidate> rank(const QJsonArray& rows, const HallMatchQuery& query, int max = 3);

    /// 低于这个分的候选不显示（宁可不给，也不给一堆噪声）
    static constexpr double kMinScore = 0.45;
};

}  // namespace maps
