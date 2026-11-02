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

#include <QDialog>

class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace maps {

class HallAPI;

/**
 * 地图启动器自己的几个开关（AIO 的「进图与退出」+「进图延迟」）。
 *
 * 为什么单独一个对话框而不是塞进全局设置页：
 * 这三项只影响「地图怎么进」，跟 Prism 的 Java/内存/主题无关；
 * 放进全局设置页要么改它的 .ui，要么新开一页，改动面都比这里大。
 */
class MapSettingsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit MapSettingsDialog(QWidget* parent = nullptr);

   private:
    void save();

    QCheckBox* m_autoEnter = nullptr;
    QCheckBox* m_safeExit = nullptr;
    QSpinBox* m_enterDelay = nullptr;
    QLineEdit* m_libraryDir = nullptr;
    QLineEdit* m_hallEndpoint = nullptr;
    /// 大厅传输方式：自动（直连被拦就改用本机浏览器）/ 只用网络 / 只用浏览器
    QComboBox* m_hallTransport = nullptr;
    QComboBox* m_hallProxyMode = nullptr;
    QLineEdit* m_hallProxyUrl = nullptr;
    QPushButton* m_probeButton = nullptr;
    QLabel* m_probeResult = nullptr;
    HallAPI* m_hallApi = nullptr;
    /// 控制台页签开关（默认收起 备注/世界/服务器/其他日志）
    QCheckBox* m_showNotes = nullptr;
    QCheckBox* m_showWorlds = nullptr;
    QCheckBox* m_showServers = nullptr;
    QCheckBox* m_showOtherLogs = nullptr;
    QLabel* m_hint = nullptr;
};

}  // namespace maps
