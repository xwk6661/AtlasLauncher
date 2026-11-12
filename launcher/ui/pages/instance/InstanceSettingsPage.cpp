// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
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
#include "InstanceSettingsPage.h"

InstanceSettingsPage::InstanceSettingsPage(MinecraftInstance* instance, QWidget* parent)
    : MinecraftSettingsWidget(instance, parent)
{
    /**
     * 这里曾经插过一个「地图」页签（maps::MapSettingsWidget 的实例作用域）。
     *
     * 2026-11-12 按用户要求挪到实例窗口的「地图 → 启动」页：打开实例先看到这张图，
     * 顺手就能改它怎么进游戏，不用先进设置再找页签。保存责任也一起搬走
     * （MapInstancePage::apply），所以这里不再持有那个 widget。
     */
}
