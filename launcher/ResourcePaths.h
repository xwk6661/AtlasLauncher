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

/**
 * 三份公共资源的实际位置。
 *
 * 以前它们都写死成相对路径（`QDir("libraries")`、`QDir("assets/")`），
 * 等于永远跟着数据目录走。现在统一从这里取绝对路径：
 *   · 设置了「共享资源目录」→ `<共享目录>/assets|libraries|java`；
 *   · 没设置 → 数据目录下的同名目录（行为与以前完全一致）。
 *
 * 不用目录联接，迁移就是真的把文件搬过去，路径也是真的指过去。
 */
namespace resource {

/// 设置里的共享目录（已规范化）；空 = 用数据目录
QString sharedRoot();

/// 三个资源目录的绝对路径
QString assetsDir();
QString librariesDir();
QString javaDir();

/// 某个名字（assets / libraries / java）对应的绝对路径
QString dirFor(const QString& name);

}  // namespace resource
