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

#include <QString>

namespace maps::series {

/**
 * 实例名里的「系列代码」↔ CTMMaps 站上的系列名。
 *
 * 玩家用 `[US1] Lacuna Sanctum`、`[RC3] The Prophecy` 这种写法标系列，但代码本身
 * （US / RC / SP）对不熟的人等于没写。所以维护一张对照表，把代码翻成站上的系列名：
 * `RC3` → `Ragecraft 3`。
 *
 * 表在 `:/maps/series.json`（随包），由 `tools/ctm_series_codes.py` 从大厅缓存的
 * `series` / `series_num` 学习后生成；新系列出现时重跑那个脚本即可，表里没有的代码
 * 原样显示 —— 猜一个系列名比不翻更糟（会把两张不同的图叫成同一系列）。
 */
namespace detail {
struct Entry {
    QString name;
    /// 站上的系列名太长时的短写（`Simulation Protocol` → `Sim. Protocol`）；空 = 用 name
    QString shortName;
};
}  // namespace detail

/// 代码 → 表项。`RC` / `rc` 都认；没有返回空 name 的项
detail::Entry entryForCode(const QString& code);

/// `RC3` → `Ragecraft 3`。表里没有、或者标签里没有数字，就原样返回 `tag`
QString displayLabel(const QString& tag);

}  // namespace maps::series
