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
#include <QList>

#include "ModPresets.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace maps {

/**
 * 「搜索并添加」：从 Modrinth / CurseForge 搜模组，勾选后加进当前预设。
 *
 * 只做一件事：把"这个平台上这个东西叫 xxx"变成一条预设条目。
 * 版本不在这里选 —— 版本永远是安装时按实例现查（见 ModPresetResolveTask）。
 */
class ModSearchDialog : public QDialog {
    Q_OBJECT

   public:
    ModSearchDialog(const QString& gameVersion, const QString& loader, QWidget* parent = nullptr);

    /// 用户勾选的条目（已填好 provider / project / name / note）
    QList<ModPresetEntry> selected() const { return m_selected; }

   private:
    void buildUi();
    void startSearch();
    void fillTable();
    void accept() override;

    QString m_gameVersion;
    QString m_loader;

    QLineEdit* m_query = nullptr;
    QComboBox* m_provider = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_add = nullptr;

    QList<ModSearchHit> m_hits;
    QList<ModPresetEntry> m_selected;
    ModSearchTask* m_task = nullptr;
    int m_generation = 0;
};

}  // namespace maps
