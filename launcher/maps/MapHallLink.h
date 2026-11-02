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

#include <QDateTime>
#include <QString>

class MinecraftInstance;

namespace maps {

/**
 * 本地地图实例 ↔ CTMMaps 上那张图的「关联」。
 *
 * 为什么要单独存：`map.json` 是导入时判定的留痕，`notes.json` 是玩家自己的评价；
 * 而"这个实例对应大厅里哪张图"是**两者之间**的关系，可能事后才建立（导入时没联网、
 * 或者匹配错了要手动改）。所以单独一个 `hall-link.json`，删掉它就是解除关联。
 *
 * 内容很小，就是 slug / map_uuid / 标题，够用来查缓存里的简介和评论。
 */
struct MapHallLink {
    QString slug;
    QString mapUuid;
    QString title;
    QDateTime linkedAt;

    bool isValid() const { return !slug.isEmpty(); }

    static QString fileName();

    static MapHallLink load(const QString& instanceRoot);
    bool save(const QString& instanceRoot, QString* error = nullptr) const;
    /// 解除关联（删文件）
    static void clear(const QString& instanceRoot);

    /**
     * 按实例名猜一张图（只在没关联时用）。
     *
     * 规则刻意保守：只在**归一化后完全相等**或"一方包含另一方且长度接近"时才认，
     * 猜错比不猜更烦人。候选来自本地库，没有联网成本。
     */
    static QString suggestSlugFor(const QString& instanceName);
};

}  // namespace maps
