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

#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class MinecraftInstance;
class SettingsObject;

namespace maps {

class HallAPI;

/**
 * 「地图」设置的内容体，全局设置页与实例设置页签共用。
 *
 * 两种作用域：
 *   · **全局**（`instance == nullptr`）：进图与退出 / 地图库 / 地图大厅 / 控制台页签，
 *     也就是原先「文件 → 地图设置」那个对话框的全部内容；
 *   · **实例**（传了实例）：只有「地图启动」三项，外面套一个可勾选的分组框
 *     （`OverrideMapBehaviors`），勾上才用实例自己的值，否则跟随全局 ——
 *     与「游戏窗口」「控制台窗口」等既有分组的覆盖语义一致。
 *
 * 为什么要挪进设置页：地图设置里一半是"启动器级"的（大厅地址、代理、地图库目录），
 * 继续放在独立对话框里，用户永远不知道它跟设置页是同一层东西。
 */
class MapSettingsWidget : public QWidget {
    Q_OBJECT

   public:
    explicit MapSettingsWidget(MinecraftInstance* instance = nullptr, QWidget* parent = nullptr);
    ~MapSettingsWidget() override = default;

    /// 从对应的设置对象回填（实例作用域读实例设置，未覆盖时自动回落到全局）
    void loadSettings();
    /// 写回；实例作用域未勾选覆盖时重置为"跟随全局"
    void saveSettings();

   private:
    void buildBehaviorGroup(QWidget* parent, QVBoxLayout* layout);
    void buildGlobalGroups(QWidget* parent, QVBoxLayout* layout);

    MinecraftInstance* m_instance = nullptr;
    SettingsObject* m_settings = nullptr;

    QGroupBox* m_behaviorBox = nullptr;
    QCheckBox* m_autoEnter = nullptr;
    QCheckBox* m_safeExit = nullptr;
    QSpinBox* m_enterDelay = nullptr;

    // 以下仅全局作用域可见
    QLineEdit* m_libraryDir = nullptr;
    QLineEdit* m_hallEndpoint = nullptr;
    QComboBox* m_hallTransport = nullptr;
    QComboBox* m_hallProxyMode = nullptr;
    QLineEdit* m_hallProxyUrl = nullptr;
    QPushButton* m_probeButton = nullptr;
    QLabel* m_probeResult = nullptr;
    HallAPI* m_hallApi = nullptr;

    QCheckBox* m_showNotes = nullptr;
    QCheckBox* m_showWorlds = nullptr;
    QCheckBox* m_showServers = nullptr;
    QCheckBox* m_showOtherLogs = nullptr;
};

}  // namespace maps
