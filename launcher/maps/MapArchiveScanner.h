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

#include "MapTypes.h"
#include "MapVersionDetector.h"
#include "Result.h"

namespace maps {

/// 压缩包里除世界之外、导入时需要落位的东西
struct ScannedResource {
    enum class Kind {
        WorldResourcesZip,  ///< 存档内的 resources.zip（客户端自动挂载）
        ResourcePack,       ///< 材质包
        BundledMod,         ///< mods/*.jar
        Readme,             ///< 说明文件（版本旁证来源）
    };

    Kind kind = Kind::ResourcePack;
    QString entryPath;
    qint64 size = 0;
    QString reason;
    /// 仅 Readme 用：解码后的正文（供版本旁证）
    QString readmeText;
};

/// 一个世界候选
struct MapWorldCandidate {
    /// 世界根在压缩包内的路径；空串表示就是压缩包根
    QString rootPath;
    QString levelDatPath;
    qint64 sizeBytes = 0;
    int fileCount = 0;
    /// 展示用标签：根目录时取压缩包名，否则取最后一级目录名
    QString folderLabel;

    LevelDatFacts level;
    MapLayoutFacts layout;
    VersionGuess version;

    /// 存档自带的 icon.png（如果存在）
    QString iconPath;
};

struct MapScanResult {
    QString archivePath;
    qint64 archiveBytes = 0;
    QList<MapWorldCandidate> worlds;
    QList<ScannedResource> resources;
    QStringList ignoredPaths;
    QStringList warnings;

    QStringList resourcePacks() const;
    QStringList bundledMods() const;
    QList<MapTextHint> readmeHints() const;
};

/**
 * 免解压归档嗅探：只看目录，不落盘。
 *
 * 作者打包习惯千差万别，实测四种典型形态都要认：
 *   `<map>/level.dat`（单层）、`PACK/<worldA|worldB>/level.dat`（一包多世界）、
 *   `a/b/c/d/<map>/level.dat`（套娃）、`level.dat`（直接摊平）。
 */
class MapArchiveScanner {
   public:
    struct Options {
        /// 深度校验根目录 zip 是否为材质包（解出 pack.mcmeta 检查）
        bool deepResourceCheck = true;
        qint64 readmeMaxBytes = 256 * 1024;
        int maxReadmeFiles = 4;
        int maxDeepChecks = 3;
        qint64 maxDeepCheckSize = 300LL * 1024 * 1024;
    };

    static Result<MapScanResult> scan(const QString& archivePath, const Options& options = {});
};

}  // namespace maps
