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

namespace maps {

/**
 * 判定置信度。
 *
 * 映射关系（与 AIO 的 VersionDetectResult.confidence 一致）：
 *   High   —— 至少有一个权重 >= 100 的精确来源（Data.Version.Name）
 *   Medium —— 只有旁证 / 跨主次版本分歧
 *   Low    —— 兜底推断，必须人工确认
 */
enum class Confidence { High, Medium, Low };

inline QString confidenceToString(Confidence confidence)
{
    switch (confidence) {
        case Confidence::High:
            return QStringLiteral("HIGH");
        case Confidence::Medium:
            return QStringLiteral("MEDIUM");
        case Confidence::Low:
            return QStringLiteral("LOW");
    }
    return QStringLiteral("LOW");
}

inline Confidence confidenceFromString(const QString& value)
{
    if (value.compare(QLatin1String("HIGH"), Qt::CaseInsensitive) == 0)
        return Confidence::High;
    if (value.compare(QLatin1String("MEDIUM"), Qt::CaseInsensitive) == 0)
        return Confidence::Medium;
    return Confidence::Low;
}


/// 存档容器格式标记（Data.version）
constexpr int ANVIL_VERSION = 19133;
constexpr int MCREGION_VERSION = 19132;

/// DataVersion 对照表里的一条记录
struct DataVersionEntry {
    int dataVersion = -1;
    int packFormat = -1;
    QString type;    // "release" / "snapshot"
    QString time;    // ISO8601
    QString source;  // "mcmeta" / "curated"
};

/**
 * 从 level.dat 抽出来的版本相关事实。
 *
 * 这是「MapVersionDetector 的唯一输入」。刻意不复用 World：World 的字段都被 UI 语义绑死了，
 * 往里塞判定用的证据会让两边都难改。
 */
struct LevelDatFacts {
    bool valid = false;

    QString levelName;
    /// Data.Version.Name 原文（可能含 § 格式化码，未加工）
    QString versionName;
    /// Data.Version.Id
    std::optional<int> versionId;
    /// Data.DataVersion
    std::optional<int> dataVersion;
    /// Data.version：19133 = Anvil，19132 = McRegion
    std::optional<int> storageVersion;
    /// Data.Version.Snapshot
    std::optional<bool> snapshot;
    /// Data.Version.Series（1.19+）
    QString series;

    bool hasBorderSize = false;
    bool hasPlayerTag = false;
    /// Data.DataPacks / Data.DataPacksVersion（1.13+）
    bool hasDataPacks = false;
    /// 根标签 fml/FML/forge/Forge 任一存在，或 WasModded==1，或 ServerBrands 命中
    bool isModded = false;
    /// Data.ServerBrands（已小写）
    QStringList serverBrands;
    /// Data.enabled_features（除 minecraft:vanilla 外的实验性开关）
    QStringList enabledFeatures;
    /// 存档声明的模组依赖，形如 "modid@1.2.3"（来自 Forge 的 fml.LoadingModList）
    QStringList requiredMods;
    int dataKeyCount = 0;
};

/**
 * 世界目录的结构特征。
 *
 * 只回答「有没有」，不回答「是什么版本」——版本结论由 detector 综合得出。
 * 注意 players/ 在旧布局（1.12 及以前，存 <uuid>.dat 文件）与新布局（26.x，存子目录）里
 * 同名反义，必须分开两个字段。
 */
struct MapLayoutFacts {
    bool hasEntitiesDir = false;
    bool hasAdvancementsDir = false;
    bool hasDatapacksDir = false;
    bool hasPlayersDir = false;
    bool hasDimensionsDir = false;
    bool hasPlayerProfilesDir = false;
};

/// 一条可展示的判定证据
struct VersionEvidence {
    QString kind;
    int weight = 0;
    QString detail;
};

/// 判定结果
struct VersionGuess {
    QString mcVersion = QStringLiteral("unknown");
    Confidence confidence = Confidence::Low;
    int score = 0;
    QString recommendedLoader = QStringLiteral("vanilla");
    QList<VersionEvidence> evidence;
    QStringList warnings;
    /// 需要的 Java 大版本；-1 表示无法确定
    int javaMajor = -1;

    bool isKnown() const { return mcVersion != QLatin1String("unknown"); }
};

}  // namespace maps
