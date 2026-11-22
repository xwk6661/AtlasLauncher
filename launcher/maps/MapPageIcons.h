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

#include <QIcon>

namespace maps::pageIcons {

/**
 * 「本地库」页图标：三层书脊 + 底板。
 *
 * 为什么画而不是用主题图标：包里那套 breeze 图标只有 `worlds` / `viewfolder` 这类通用图形，
 * 与「导入」页撞图；而补一份 SVG 要同时改 9 个主题目录的 qrc。用 QPainter 画一次，
 * 取色走 `MapTheme` 的 palette，深色/浅色主题都自己跟着变。
 */
QIcon library();

/// 「CTMMaps」页图标：地球 + 定位点（站点/在线地图，与本地库区分开）
QIcon ctmmaps();

}  // namespace maps::pageIcons
