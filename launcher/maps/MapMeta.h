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
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "MapTypes.h"

namespace maps {

/// 压缩包来源
struct MapArchiveSource {
    QString path;
    qint64 size = 0;
    QString sha1;
    /// 归入地图库后的副本路径（没入库则为空）
    QString libraryCopy;
};

/// 版本判定留痕：凭什么认为它是这个版本
struct MapVersionRecord {
    QString mcVersion = QStringLiteral("unknown");
    Confidence confidence = Confidence::Low;
    QStringList evidence;
    /// detector（自动判定） / user（人工指定） / archive（存档内嵌字段唯一来源）
    QString decidedBy = QStringLiteral("detector");

    bool isKnown() const { return !mcVersion.isEmpty() && mcVersion != QLatin1String("unknown"); }
};

/// 一个存档目录
struct MapWorldRecord {
    /// 实例内 saves/ 下的目录名，固定英文
    QString folder;
    /// 压缩包内的世界根路径（空串 = 压缩包根）
    QString sourceRoot;
};

struct MapLoaderRecord {
    QString type = QStringLiteral("vanilla");
    QString version;
};

struct MapStats {
    qint64 playSeconds = 0;
    QDateTime lastPlayed;
    /// safe / crash / unknown
    QString lastExit = QStringLiteral("unknown");
};

/**
 * `map.json` —— 地图特有元数据的唯一读写点（设计见 docs/atlas/02 §2.2）。
 *
 * 为什么单独一个文件而不是塞进 instance.cfg：
 *   · INI 表达不了数组与证据链；
 *   · instance.cfg 是 Prism 的领域（INISettingsObject 有类型与默认值语义），别污染。
 * 也不重复 Prism 已有的字段：版本在 mmc-pack.json、显示名在 instance.cfg 的 name。
 */
class MapMeta {
   public:
    static constexpr int SCHEMA_VERSION = 1;

    /// 实例根目录下的文件名
    static QString fileName() { return QStringLiteral("map.json"); }

    /// 读取；文件不存在或损坏时返回空对象（带 error 说明）
    static MapMeta load(const QString& instanceRoot, QString* error = nullptr);

    bool save(const QString& instanceRoot, QString* error = nullptr) const;

    QJsonObject toJson() const;
    static MapMeta fromJson(const QJsonObject& object);

    QString title;
    QString alias;
    MapArchiveSource archive;
    MapVersionRecord version;
    QList<MapWorldRecord> worlds;
    MapLoaderRecord loader;
    /// 随包材质包（实例内相对路径）
    QStringList packs;
    /// 随包模组（实例内相对路径）
    QStringList mods;
    /// 存档 enabled_features 里除 minecraft:vanilla 之外的项
    QStringList experimentalFeatures;
    /// 存档声明的模组依赖，形如 "modid@1.2.3"
    QStringList requiredMods;
    /// 存档是模组环境、但压缩包没带 mods —— 需要玩家自备整合包
    bool requiresExternalModpack = false;
    MapStats stats;
    QDateTime importedAt;
};

}  // namespace maps
