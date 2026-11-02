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
#include <QString>

namespace maps {

/**
 * 玩家自己的数据：评分 / 笔记 / 中文名，单独存 `notes.json`。
 *
 * 为什么**不**塞进 map.json（照 AIO `UserNote` 的分工）：
 *   · `map.json` 回答「这张图是什么」—— 由扫描与导入产生，**重装/重导入就该被覆盖**；
 *   · 评分/笔记/中文名回答「我怎么看这张图」—— 由人产生，**任何自动流程都不该碰**。
 * 混在一份文件里，早晚出现"重装地图把笔记冲掉"这种事。
 *
 * 中文名单独存在这里的另一个理由：`title` 是这张图**自称**什么（来自 level.dat，
 * 可能是 `[SP03] Entropy` 这种英文/带编号的名字），中文名是玩家**怎么称呼**它 ——
 * 属于人的判断，重装地图不该覆盖。它只影响显示（卡片、右侧标题、窗口标题）与搜索。
 *
 * 评分照 AIO 存 1~10（半星粒度），界面按 10 → 5 星显示。
 */
struct MapNotes {
    static constexpr int MAX_ALIAS = 40;
    static constexpr int MAX_NOTE = 2000;
    static constexpr int MAX_RATING = 10;

    /// 0 = 未评分；1..10 = 半星粒度
    int rating = 0;
    QString note;
    /// 玩家自己起的中文名；空 = 用导入时识别出的标题
    QString alias;
    QDateTime updatedAt;

    static QString fileName() { return QStringLiteral("notes.json"); }

    /// 读不到/损坏时返回空对象（笔记丢了也不能拦住实例加载）
    static MapNotes load(const QString& instanceRoot);

    /// 写盘前统一裁剪：评分钳到 0..10、中文名去空白并截到 40、笔记截到 2000
    bool save(const QString& instanceRoot, QString* error = nullptr) const;

    MapNotes normalized() const;
    bool isEmpty() const;

    /// 显示名：中文名优先，空则用 fallback（导入时识别出的标题）
    QString displayName(const QString& fallback) const { return alias.isEmpty() ? fallback : alias; }

    /// "4.0 / 5"，未评分返回空串
    QString ratingText() const;
};

}  // namespace maps
