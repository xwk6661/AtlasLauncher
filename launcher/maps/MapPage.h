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

#include "MapSettingsWidget.h"
#include "ui/pages/BasePage.h"

namespace maps {

/// 全局设置的「地图」页（排在「常规」右边，与「游戏」相邻）
class MapPage : public MapSettingsWidget, public BasePage {
    Q_OBJECT

   public:
    explicit MapPage(QWidget* parent = nullptr) : MapSettingsWidget(nullptr, parent) {}
    ~MapPage() override = default;

    QString displayName() const override { return tr("地图"); }
    QIcon icon() const override { return QIcon::fromTheme("worlds"); }
    QString id() const override { return QStringLiteral("map-settings"); }

    bool apply() override
    {
        saveSettings();
        return true;
    }
};

}  // namespace maps
