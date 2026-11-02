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

#include <optional>

#include "MapTypes.h"

namespace maps {

/// 一条文本旁证：来源说明 + 正文
struct MapTextHint {
    QString source;
    QString text;
};

/// 判定输入。所有字段都可能缺失 —— 缺得越多，置信度越低。
struct MapDetectInput {
    LevelDatFacts level;
    std::optional<MapLayoutFacts> layout;
    QList<MapTextHint> textHints;
    /// 压缩包文件名（含扩展名），只用于旁证
    QString archiveName;
    /// 存档目录名（压缩包内那一级），只用于旁证
    QString worldFolderLabel;
    /// 压缩包内是否带 mods/*.jar
    bool hasBundledMods = false;
};

/**
 * 三级版本判定（对齐 AIO 的 `version/detector.ts`）。
 *
 *   L1 精确字段：Data.Version.Name / Version.Id / DataVersion 任一命中对照表
 *   L2 文本旁证：文件名 / 存档目录名 / README
 *   L3 结构兜底：Anvil/McRegion 容器标记 + 目录结构特征
 *
 * 冲突处理（顺序固定，不要改）：
 *   1. 外部旁证与某个候选一致 → 采信它
 *   2. 否则采信 Data.DataVersion（DFU 实际读取的格式标记）
 *   3. 仍无法裁决 → 取最低版本
 *   4. 跨主次版本分歧 → 降为 Medium，强制人工确认
 */
class MapVersionDetector {
   public:
    struct NormalizedVersion {
        QString value = QStringLiteral("unknown");
        /// release / snapshot / unknown
        QString kind = QStringLiteral("unknown");
        QString note;
    };

    /// 归一化任意来源的版本字符串；只有命中对照表才返回 release/snapshot
    static NormalizedVersion normalizeVersion(const QString& raw);

    static VersionGuess detect(const MapDetectInput& input);

    /// level.dat 损坏 / 缺失时的统一返回值：可展示、可拒绝启动，不是 nullptr
    static VersionGuess unknownVersion(const QString& reason);

    /// 是否可以直接用于自动部署（unknown 一律要求人工介入）
    static bool isLaunchableVersion(const QString& version);
};

}  // namespace maps
