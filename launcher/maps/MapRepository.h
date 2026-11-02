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

#include <QList>
#include <QString>
#include <QStringList>

#include "MapMeta.h"
#include "MapNotes.h"

class InstanceList;
class MinecraftInstance;

namespace maps {

/// 画廊里的一张卡片
struct MapEntry {
    QString instanceId;
    QString instanceRoot;
    QString displayName;
    QString coverPath;  // 实例根目录下的 icon.png（可能为空）
    MapMeta meta;
    /// 玩家自己的评分/笔记/中文名（notes.json，与 map.json 分开）
    MapNotes notes;
    /// 版本徽章文本，例如 "1.12.2 · forge"
    QString versionBadge;
    /**
     * 实例**实际装的**加载器：Vanilla / Forge / Fabric / NeoForge / Quilt。
     *
     * 取 mmc-pack 的组件，而不是 map.json 里的 loader.type —— 后者是导入时的
     * "存档像什么环境"，玩家后来装没装 Forge 只有组件说了算。
     */
    QString loaderName;
    QString loaderVersion;
    /// 置信度徽章（High/Medium/Low）
    Confidence confidence = Confidence::Low;
};

/**
 * 把「实例列表」过滤成「地图列表」。
 *
 * 判据只有一条：实例根目录下有 map.json。这样普通实例永远不会被误当成地图，
 * 也不需要额外维护一份索引（索引会与磁盘不同步）。
 */
class MapRepository {
   public:
    /// 扫一遍实例列表；worlds 为空的 map.json 仍然算地图（导入失败时也需要展示与修复入口）
    static QList<MapEntry> collect(InstanceList* instances);
    static MapEntry describe(MinecraftInstance* instance);

    /// 地图库目录（设置项 MapLibraryDir，空则 <data>/maps）
    static QString libraryDir();

    /// 把压缩包复制进地图库（同名不同内容时加时间戳后缀）；失败返回空串
    static QString importIntoLibrary(const QString& archivePath);

    /**
     * 解析压缩包的实际位置：`libraryCopy` → 库目录同名文件 → 原始 `path`。
     *
     * 为什么要回退：数据根从 C 盘 AppData 搬到程序目录（便携化）之后，
     * 旧 `map.json` 里记录的还是老路径。位置换过一次就会有第二次，
     * 与其挨个改写 map.json，不如在使用点就地解析。
     */
    static QString resolveArchivePath(const class MapMeta& meta);
};

}  // namespace maps
