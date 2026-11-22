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

#include "ui/pages/BasePage.h"

class NewInstanceDialog;

namespace maps {

class MapLibraryPanel;

/**
 * 「新建实例 → 本地库」页：把地图库从导入页里独立出来，排在 CTM 地图下面。
 *
 * 为什么独立：导入页回答的是"这个文件怎么进来"，本地库回答的是"我手上有哪些图"。
 * 两者混在一页时，导入页既不是上游的样子，库也挤在半屏里 —— 现在各归各位：
 *   · 导入页回到"选一个包 → 导入"的原始形态；
 *   · 本页整屏给库（分类栏 + 列表 + 右键），右下角留一个「从外部导入…」当旁门。
 *
 * 本页与库面板的分工：面板只管展示与索引写入，**建实例**这件事仍要经过
 * `MapImportDialog` 确认（版本 / 加载器 / 关联 / 语言），然后交给新建实例对话框。
 */
class LocalLibraryPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit LocalLibraryPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);

    QString id() const override { return QStringLiteral("maplibrary"); }
    QString displayName() const override { return tr("本地库"); }
    QIcon icon() const override;
    QString helpPage() const override { return {}; }
    void openedImpl() override;

   private:
    /// 选一个不在库里的包 → 扫描 → 导入对话框 → 交给新建实例对话框
    void importFromPath(const QString& path);

    NewInstanceDialog* m_dialog = nullptr;
    MapLibraryPanel* m_library = nullptr;
    /// 自动关联每次运行只做一遍（结果已落进索引，重复扫没有意义）
    bool m_autoLinkedOnce = false;
};

}  // namespace maps
