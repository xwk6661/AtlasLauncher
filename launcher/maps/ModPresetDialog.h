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
#include <QSet>

#include "ModPresets.h"

class MinecraftInstance;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace maps {

/**
 * 「一键装模组预设」。
 *
 * 交互只有三步：选预设 → 看每一行解析到哪个版本（可以取消勾选）→ 安装。
 * 版本不是预设里写死的，而是按**当前实例的 MC 版本 + 加载器**现查 Modrinth；
 * 解析不到的条目会标出来并自动取消勾选，绝不猜一个文件装进去。
 */
class ModPresetDialog : public QDialog {
    Q_OBJECT

   public:
    /**
     * 入口：先确保实例有模组加载器（没有就问一句、顺手打开安装向导），
     * 再打开预设窗口。地图实例几乎都是 Vanilla，这一步不能省。
     */
    static void run(MinecraftInstance* instance, QWidget* parent);

   private:
    explicit ModPresetDialog(MinecraftInstance* instance, QWidget* parent = nullptr);

    void buildUi();
    void reloadPresets();
    void startResolve();
    void rebuildTable(bool resolving);
    void refreshRow(int index);
    /// 主条目勾选状态变化时，连带它的依赖（递归）
    void setDependentsChecked(const QString& key, bool checked, QSet<QString>& visited);
    /// 读实例现有的 mods，按 (provider, projectId/slug) 建索引
    void collectInstalled();
    bool isInstalled(const ModPresetEntry& entry, const ModPresetResolution& result) const;
    void updateSummary();
    void install();
    void editPresets();
    /// 预设不合口味时的出口：打开原生模组浏览器自己搜
    void openModBrowser();

    QList<ModPreset> m_presets;
    ModPreset m_current;
    QList<ModPresetResolution> m_results;

    MinecraftInstance* m_instance = nullptr;
    QString m_mcVersion;
    QString m_loader;

    QComboBox* m_presetCombo = nullptr;
    QLabel* m_target = nullptr;
    QLabel* m_description = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_browse = nullptr;
    QPushButton* m_selectAll = nullptr;
    QPushButton* m_selectNone = nullptr;

    ModPresetResolveTask* m_task = nullptr;
    /// 已安装模组的键：`provider:projectId` 与 `provider:slug`
    QSet<QString> m_installed;
    /// 已安装模组的显示名（小写）：同一个模组在 Modrinth / CurseForge 两边用不同 id 发布时按名字认
    QSet<QString> m_installedNames;
    /// 批量改动勾选时挡住 itemChanged，避免自己触发自己
    bool m_syncingChecks = false;
    /// 切换预设时递增，旧任务迟到的信号直接丢掉
    int m_generation = 0;
};

/**
 * 预设编辑器。
 *
 * 故意做成"一行一个模组"的文本：玩家从浏览器复制 Modrinth 地址里最后那段
 * （slug）就能加进来，不需要我们先做一套模组搜索。格式 `slug | 显示名 | 备注`，
 * 后两段可省。
 */
class ModPresetEditorDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ModPresetEditorDialog(const QString& gameVersion, const QString& loader, QWidget* parent = nullptr);

   private:
    void reloadPresets(const QString& selectId = {});
    void loadIntoEditors(const QString& id);
    void saveCurrent();
    void removeCurrent();
    void addPreset();

    QList<ModPreset> m_presets;
    QString m_currentId;
    /// 打开搜索时用的实例版本/加载器（不是下面那个下拉框 m_loader）
    QString m_gameVersion;
    QString m_instanceLoader;

    QComboBox* m_presetCombo = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_loader = nullptr;
    QLineEdit* m_description = nullptr;
    QPlainTextEdit* m_mods = nullptr;
};

}  // namespace maps
