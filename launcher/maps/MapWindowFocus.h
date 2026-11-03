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

class QObject;
class MinecraftInstance;

namespace maps {

/**
 * 把前台让给刚启动的游戏窗口。
 *
 * 为什么需要：`--quickPlaySingleplayer` 会**跳过标题画面**直接进图。MC 在 `screen == null`
 * 的那一帧就调用 grabMouse()，而它只靠「是否收到过失焦事件」来判断要不要暂停 ——
 * 如果窗口从创建起就没拿到过焦点（启动器窗口还占着前台），MC 既不会暂停、也没法把光标
 * 真正锁进窗口，于是每帧读到的都是窗口外/屏幕上的绝对坐标，表现出来就是
 * **视角被顶到最高或最低、鼠标一碰就狂转**（Mojira MCL-24083，至今未修）。
 *
 * 这里在启动后盯着进程窗口：一旦出现，且当时的**前台还是我们自己的启动器**，
 * 就把前台交过去。只在这种情况下动手 —— 你已经切到别的程序时绝不抢焦点。
 *
 * 一次 launch 只 arm 一次，目标达成或超时即停。
 */
void armGameWindowFocus(MinecraftInstance* instance, int timeoutMs = 60000);

}  // namespace maps
