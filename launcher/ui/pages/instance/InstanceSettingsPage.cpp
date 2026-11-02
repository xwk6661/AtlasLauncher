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

#include <QTabWidget>

#include "Application.h"

InstanceSettingsPage::InstanceSettingsPage(MinecraftInstance* instance, QWidget* parent)
    : MinecraftSettingsWidget(instance, parent)
{
    /**
     * 「地图」页签插在「常规」与「Java」之间。
     *
     * 不写进 .ui：这一页只在实例窗口里出现（全局那份是独立的 maps::MapPage），
     * 而 .ui 是两边共用的 —— 写进去还得在全局模式下再删一次。
     */
    m_mapSettings = new maps::MapSettingsWidget(m_instance, this);
    // 走 objectName 拿页签容器：直接包含 ui_ 头会触发 AUTOUIC 按 .cpp 所在目录找 .ui（这里找不到）
    if (auto* tabs = findChild<QTabWidget*>(QStringLiteral("settingsTabs")))
        tabs->insertTab(1, m_mapSettings, QIcon::fromTheme("worlds"), tr("地图"));

    connect(APPLICATION, &Application::globalSettingsAboutToOpen, this, &InstanceSettingsPage::apply);
    connect(APPLICATION, &Application::globalSettingsApplied, this, &InstanceSettingsPage::loadSettings);
    connect(APPLICATION, &Application::globalSettingsApplied, this, [this] { m_mapSettings->loadSettings(); });
}
