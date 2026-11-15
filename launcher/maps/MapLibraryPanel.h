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

#include <QSet>
#include <QWidget>

#include "MapLibrary.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace maps {

/**
 * 「本地地图库」面板：左边分类、右边库里的压缩包。
 *
 * 交互能力：
 *   · 把行拖到左边某个分类上 → 改分类（写 map-library.json）；
 *   · 双击行 / 点「导入选中的地图」→ 把路径丢给 ImportPage，走原来那条识别→建实例的链路；
 *   · 刷新 → 重新扫库目录（新丢进去的包会以「Other」出现）；
 *   · 方案 B 自定义分类：支持 + / - 按钮以及右键菜单的新建、重命名、删除分类操作。
 *
 * 「已安装」是每次刷新算出来的，不是存出来的：实例重装/删除后不会残留错误标记。
 */
class MapLibraryPanel : public QWidget {
    Q_OBJECT

   public:
    explicit MapLibraryPanel(QWidget* parent = nullptr);
    ~MapLibraryPanel() override;

    /// 重新读索引与目录（页面打开时调一次即可，代价只有一次目录遍历）
    void refresh();

   signals:
    /// 想导入某张库里的图（绝对路径）
    void importRequested(const QString& archivePath);

   private slots:
    void onAddCategoryClicked();
    void onRenameCategoryClicked();
    void onDeleteCategoryClicked();
    void showCategoryContextMenu(const QPoint& pos);
    /// 列表行右键：组标题 / 组内版本 / 独立成图，三种菜单互不越权
    void showItemContextMenu(const QPoint& pos);

   private:
    void rebuildCategories();
    void rebuildTable();
    /// 折叠/展开某一组（组键同 `MapLibrary::groupKey()`）
    void toggleGroup(const QString& key);
    void applyCategory(const QString& file, const QString& categoryId);
    /// 整组改分类（组标题右键）
    void applyCategoryToGroup(const QString& groupKey, const QString& categoryId);
    /// 把一份资源包挂到玩家选中的实例（不建实例、不动存档）
    void attachResourcePack(const QString& file, const QString& path);
    /// 改语言标签（原版/简中/繁中/官中 + 来源）
    void editVariant(const QString& file);
    /// 改「地图版本」（v1.0 这种，界面默认 1.0）
    void editMapVersion(const QString& file);
    /// 组标题菜单：信息 + 展开/收起 + 导入特定版本 + 整组类别/关联
    void showGroupContextMenu(const QString& groupKey, const QPoint& globalPos);
    /// 组内版本菜单：**只能**导入和改它自己的字段，碰不到组标题
    void showVersionContextMenu(const QString& file, const QPoint& globalPos);
    /// 独立成图（没有组）的菜单：它就是自己那一组，所以组级操作也归它
    void showStandaloneMenu(const QString& file, const QPoint& globalPos);
    /// 从大厅缓存里挑一张图当"所属主"（files 可以是一组）
    void pickHallMap(const QStringList& files);

    /// 列宽持久化（设置项 `MapLibraryColumnState`）
    void restoreColumnState();
    void saveColumnState();

    MapLibrary m_library;
    QListWidget* m_categories = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_importButton = nullptr;
    QPushButton* m_addCategoryBtn = nullptr;
    QPushButton* m_delCategoryBtn = nullptr;
    bool m_showAll = true;
    QString m_filterCategoryId;
    /// 展开的组键。**默认收起**：库里大半是同一个图的几个语言版本，全摊开时一屏看不到几张图
    QSet<QString> m_expandedGroups;
    /// 列宽变化后延迟落盘：拖动时 sectionResized 会连发几十次
    class QTimer* m_columnSaveTimer = nullptr;
};

}  // namespace maps
