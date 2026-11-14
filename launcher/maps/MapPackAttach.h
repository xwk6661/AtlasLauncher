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

#include <QString>
#include <QStringList>

#include "Result.h"

namespace maps {

/// 一个压缩包"是不是纯资源包"的结论
struct ResourcePackProbe {
    /// true = 有 `pack.mcmeta`、且没有 `level.dat`
    bool valid = false;
    /// 建议的包 id：根目录包用 zip 文件名，套了一层文件夹的用那个文件夹名
    QString packId;
    /// `pack.mcmeta` 所在的那一级（根目录为空串）
    QString rootFolder;
    /// 无效原因（给人看的一句话）
    QString reason;
};

/**
 * 把"汉化资源包"挂到已装实例上。
 *
 * 为什么必须单独一条路：资源包**没有世界**。它不能走地图导入（那边建实例、解存档），
 * 否则玩家会得到一个空世界。正确做法只有一条：
 *
 *     拷进 `<实例>/.minecraft/resourcepacks/` → 把 `file/<包>` 追加进 `options.txt`
 *
 * 不动存档、不动 `map.json`、不建实例。撤销就是删掉那个文件并去掉那一项。
 *
 * 只认文件结构，不认文件名：`pack.mcmeta` 在根或在唯一的一级子目录里才算资源包；
 * 有 `level.dat` 一律当地图包（那种包该走导入）。
 */
class MapPackAttach {
   public:
    /// 读归档的目录（不解压），判断它是不是资源包
    static ResourcePackProbe inspect(const QString& archivePath);

    /**
     * 挂到实例：拷贝（或解出子目录）到 `resourcepacks/`，并把它启用。
     *
     * 目标位置已有同名文件时**不覆盖**，直接报错让人先处理 —— 悄悄覆盖别人的包更糟。
     *
     * @return 成功时返回写进 options.txt 的条目（`file/xxx.zip` 或 `file/xxx`）
     */
    static Result<QString> attach(const QString& archivePath, const QString& instanceRoot);

    /// 纯函数：把条目追加进 `resourcePacks:[...]`（幂等；没有这一行就新建，`vanilla` 必留）
    static bool addToOptionsLines(QStringList& lines, const QString& packEntry);
};

}  // namespace maps
