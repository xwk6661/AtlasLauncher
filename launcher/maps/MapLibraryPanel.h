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

   private:
    void rebuildCategories();
    void rebuildTable();
    void applyCategory(const QString& file, const QString& categoryId);

    MapLibrary m_library;
    QListWidget* m_categories = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_importButton = nullptr;
    QPushButton* m_addCategoryBtn = nullptr;
    QPushButton* m_delCategoryBtn = nullptr;
    bool m_showAll = true;
    QString m_filterCategoryId;
};

}  // namespace maps
