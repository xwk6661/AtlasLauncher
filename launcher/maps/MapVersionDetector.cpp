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
#include "MapVersionDetector.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include "DataVersionTable.h"
#include "MapTextUtils.h"

namespace maps {
namespace {

constexpr int WEIGHT_VERSION_NAME = 100;
constexpr int WEIGHT_VERSION_ID = 95;
constexpr int WEIGHT_DATA_VERSION = 90;
constexpr int WEIGHT_TEXT_HINT = 40;
constexpr int WEIGHT_ANVIL = 20;
constexpr int WEIGHT_BORDER = 10;
constexpr int WEIGHT_DATA_PACKS = 15;
constexpr int WEIGHT_FML = 10;
constexpr int WEIGHT_ENTITIES_DIR = 8;
constexpr int WEIGHT_ADVANCEMENTS_DIR = 6;
constexpr int WEIGHT_PLAYERS_DIR = 6;
constexpr int WEIGHT_DIMENSIONS_DIR = 6;
constexpr int WEIGHT_PLAYER_PROFILES_DIR = 4;

constexpr const char* SOURCE_VERSION_NAME = "Data.Version.Name";
constexpr const char* SOURCE_VERSION_ID = "Data.Version.Id";
constexpr const char* SOURCE_DATA_VERSION = "Data.DataVersion";
constexpr const char* SOURCE_ARCHIVE_NAME = "压缩包文件名";
constexpr const char* SOURCE_FOLDER_LABEL = "存档目录名";
constexpr const char* UNKNOWN = "unknown";

struct Candidate {
    QString source;
    QString value;
    int weight = 0;
    QString detail;
};

struct Mention {
    QString source;
    QString version;
};

Confidence scoreToConfidence(int score)
{
    if (score >= 100)
        return Confidence::High;
    if (score >= 40)
        return Confidence::Medium;
    return Confidence::Low;
}

QString majorMinor(const QString& version)
{
    const auto key = DataVersionTable::parseReleaseKey(version);
    if (!key)
        return version;
    return QStringLiteral("%1.%2").arg((*key)[0]).arg((*key)[1]);
}

struct VersionRange {
    QString min = QStringLiteral("1.0.0");
    QString max = QStringLiteral("99.99.99");
};

/// 由目录结构推出可行的版本区间，用来过滤 README 里的无关数字噪声
VersionRange impliedRange(const std::optional<MapLayoutFacts>& layout)
{
    VersionRange range;
    if (!layout)
        return range;

    if (layout->hasEntitiesDir)
        range.min = QStringLiteral("1.17");
    else if (layout->hasDatapacksDir)
        range.min = QStringLiteral("1.13");
    else if (layout->hasAdvancementsDir)
        range.min = QStringLiteral("1.12");

    // 26.x 的两条新特征比 entities/ 更晚出现，最后覆盖 min
    if (layout->hasDimensionsDir || layout->hasPlayerProfilesDir)
        range.min = QStringLiteral("26.1");

    // 旧布局的 players/<uuid>.dat 在 1.13 被移除，只有它能当上界
    if (layout->hasPlayersDir)
        range.max = QStringLiteral("1.12.2");

    return range;
}

/// 是否已经是「世界数据按维度分目录」的那一代（26.x）
bool isModernLayout(const std::optional<MapLayoutFacts>& layout)
{
    return layout && (layout->hasDimensionsDir || layout->hasPlayerProfilesDir);
}

QString loaderOf(const LevelDatFacts& level, bool hasBundledMods)
{
    const auto hasBrand = [&level](const char* needle) {
        for (const auto& brand : level.serverBrands) {
            if (brand.contains(QLatin1String(needle)))
                return true;
        }
        return false;
    };

    if (hasBrand("neoforge"))
        return QStringLiteral("neoforge");
    if (hasBrand("quilt"))
        return QStringLiteral("quilt");
    if (hasBrand("fabric"))
        return QStringLiteral("fabric");
    if (hasBrand("forge"))
        return QStringLiteral("forge");
    if (level.isModded)
        return QStringLiteral("forge");
    return hasBundledMods ? QStringLiteral("forge") : QStringLiteral("vanilla");
}

QList<Mention> externalMentions(const MapDetectInput& input)
{
    QList<Mention> out;
    const auto push = [&out](const QString& source, const QString& text) {
        if (text.isEmpty())
            return;
        for (const auto& raw : text::extractVersionMentions(text)) {
            const auto normalized = MapVersionDetector::normalizeVersion(raw);
            if (normalized.kind != QLatin1String("unknown"))
                out.append({ source, normalized.value });
        }
    };

    push(QLatin1String(SOURCE_ARCHIVE_NAME), input.archiveName);
    push(QLatin1String(SOURCE_FOLDER_LABEL), input.worldFolderLabel);
    for (const auto& hint : input.textHints)
        push(hint.source, hint.text);
    return out;
}

/// 收集结构性特征证据，返回累计权重
int addStructuralEvidence(const LevelDatFacts& level, const std::optional<MapLayoutFacts>& layout,
                          QList<VersionEvidence>& evidence)
{
    int score = 0;

    if (level.storageVersion && *level.storageVersion == ANVIL_VERSION) {
        score += WEIGHT_ANVIL;
        evidence.append({ QStringLiteral("Data.version"), WEIGHT_ANVIL, QStringLiteral("Anvil 容器格式（19133）") });
    } else if (level.storageVersion && *level.storageVersion == MCREGION_VERSION) {
        score += WEIGHT_ANVIL;
        evidence.append({ QStringLiteral("Data.version"), WEIGHT_ANVIL, QStringLiteral("McRegion 容器格式（19132）") });
    }

    if (level.hasBorderSize) {
        score += WEIGHT_BORDER;
        evidence.append({ QStringLiteral("Data.BorderSize"), WEIGHT_BORDER, QStringLiteral("存在世界边界字段（1.8+）") });
    }
    if (level.hasDataPacks) {
        score += WEIGHT_DATA_PACKS;
        evidence.append({ QStringLiteral("Data.DataPacks"), WEIGHT_DATA_PACKS, QStringLiteral("存在数据包字段（1.13+）") });
    }
    if (level.isModded) {
        score += WEIGHT_FML;
        const QString detail = !level.serverBrands.isEmpty()
                                   ? QStringLiteral("ServerBrands=[%1]").arg(level.serverBrands.join(QLatin1Char(',')))
                                   : QStringLiteral("存在 fml/FML/forge 根标签或 WasModded 标记");
        evidence.append({ QStringLiteral("模组标记"), WEIGHT_FML, detail });
    }

    if (!layout)
        return score;

    if (layout->hasEntitiesDir) {
        score += WEIGHT_ENTITIES_DIR;
        evidence.append({ QStringLiteral("entities/"), WEIGHT_ENTITIES_DIR, QStringLiteral("实体数据已独立成目录（1.17+）") });
    }
    if (layout->hasAdvancementsDir) {
        score += WEIGHT_ADVANCEMENTS_DIR;
        evidence.append(
            { QStringLiteral("advancements/"), WEIGHT_ADVANCEMENTS_DIR, QStringLiteral("存在 advancements 目录（1.12+）") });
    }
    if (layout->hasPlayersDir) {
        score += WEIGHT_PLAYERS_DIR;
        evidence.append({ QStringLiteral("players/"), WEIGHT_PLAYERS_DIR,
                          QStringLiteral("存在 players/<uuid>.dat（旧布局，1.13 起已移除）") });
    }
    if (layout->hasDimensionsDir) {
        score += WEIGHT_DIMENSIONS_DIR;
        evidence.append({ QStringLiteral("dimensions/"), WEIGHT_DIMENSIONS_DIR,
                          QStringLiteral("世界数据按维度分目录（26.1+ 布局）") });
    }
    if (layout->hasPlayerProfilesDir) {
        score += WEIGHT_PLAYER_PROFILES_DIR;
        evidence.append({ QStringLiteral("players/{advancements,data,stats}"), WEIGHT_PLAYER_PROFILES_DIR,
                          QStringLiteral("存在玩家档案目录（26.x 布局）") });
    }
    return score;
}

QString joinCandidates(const QList<Candidate>& candidates)
{
    QStringList parts;
    parts.reserve(candidates.size());
    for (const auto& candidate : candidates)
        parts.append(QStringLiteral("%1=%2").arg(candidate.source, candidate.value));
    return parts.join(QStringLiteral(" / "));
}

}  // namespace

MapVersionDetector::NormalizedVersion MapVersionDetector::normalizeVersion(const QString& raw)
{
    static const QRegularExpression snapshotPattern(QStringLiteral("^\\d{2}w\\d{2}[a-z]$"));
    static const QRegularExpression releaseFragment(QStringLiteral("\\b\\d{1,2}\\.\\d{1,2}(?:\\.\\d{1,2})?\\b"));

    NormalizedVersion result;
    const auto& table = DataVersionTable::instance();

    const QString cleaned = text::cleanDisplayName(raw);
    if (cleaned.isEmpty())
        return result;

    // 1) 完全命中
    if (table.isKnownVersion(cleaned)) {
        result.value = cleaned;
        result.kind = table.entry(cleaned)->type == QLatin1String("release") ? QStringLiteral("release")
                                                                            : QStringLiteral("snapshot");
        return result;
    }

    // 2) 快照名（23w14a）
    if (snapshotPattern.match(cleaned).hasMatch()) {
        result.value = cleaned;
        result.kind = QStringLiteral("unknown");
        result.note = QStringLiteral("快照 %1 不在对照表中").arg(cleaned);
        return result;
    }

    // 3) 从长字符串里抠版本片段，优先选择对照表中存在的
    QStringList fragments;
    auto it = releaseFragment.globalMatch(cleaned);
    while (it.hasNext())
        fragments.append(it.next().captured(0));

    for (const auto& fragment : fragments) {
        if (!table.isKnownVersion(fragment))
            continue;
        result.value = fragment;
        result.kind = table.entry(fragment)->type == QLatin1String("release") ? QStringLiteral("release")
                                                                             : QStringLiteral("snapshot");
        result.note = QStringLiteral("从 \"%1\" 中提取到 %2").arg(cleaned, fragment);
        return result;
    }

    // 4) 1.20.0 这种不存在的写法 -> 1.20
    for (const auto& fragment : fragments) {
        const auto key = DataVersionTable::parseReleaseKey(fragment);
        if (!key || (*key)[2] != 0)
            continue;
        const QString collapsed = QStringLiteral("%1.%2").arg((*key)[0]).arg((*key)[1]);
        if (table.isKnownVersion(collapsed)) {
            result.value = collapsed;
            result.kind = QStringLiteral("release");
            result.note = QStringLiteral("%1 归一化为 %2").arg(fragment, collapsed);
            return result;
        }
    }

    // 5) 形状像版本号但表里没有 —— 保留原值并标记 unknown，交给上层决策
    if (!fragments.isEmpty()) {
        result.value = fragments.first();
        result.kind = QStringLiteral("unknown");
        result.note = QStringLiteral("\"%1\" 不在对照表中，可能是不存在的版本号或新版本").arg(fragments.first());
        return result;
    }

    result.value = cleaned;
    result.kind = QStringLiteral("unknown");
    result.note = QStringLiteral("无法解析版本字符串 \"%1\"").arg(cleaned);
    return result;
}

VersionGuess MapVersionDetector::detect(const MapDetectInput& input)
{
    const auto& level = input.level;
    const auto& table = DataVersionTable::instance();

    VersionGuess guess;
    QStringList warnings;
    QList<Candidate> candidates;
    const auto mentions = externalMentions(input);

    // ── L1：显式字段 ─────────────────────────────────────────────
    if (!level.versionName.isEmpty()) {
        const auto normalized = normalizeVersion(level.versionName);
        if (!normalized.note.isEmpty())
            warnings.append(QStringLiteral("Data.Version.Name：%1").arg(normalized.note));
        if (normalized.kind != QLatin1String("unknown")) {
            candidates.append({ QLatin1String(SOURCE_VERSION_NAME), normalized.value, WEIGHT_VERSION_NAME,
                                QStringLiteral("原始值 \"%1\" → %2").arg(level.versionName, normalized.value) });
        }
    }

    if (level.versionId) {
        const QString mapped = table.versionForDataVersion(*level.versionId);
        if (!mapped.isEmpty()) {
            candidates.append({ QLatin1String(SOURCE_VERSION_ID), mapped, WEIGHT_VERSION_ID,
                                QStringLiteral("Version.Id=%1 → %2").arg(*level.versionId).arg(mapped) });
        } else {
            warnings.append(QStringLiteral("Data.Version.Id=%1 不在对照表中").arg(*level.versionId));
        }
    }

    if (level.dataVersion && (!level.versionId || *level.dataVersion != *level.versionId)) {
        const QString mapped = table.versionForDataVersion(*level.dataVersion);
        if (!mapped.isEmpty()) {
            candidates.append({ QLatin1String(SOURCE_DATA_VERSION), mapped, WEIGHT_DATA_VERSION,
                                QStringLiteral("DataVersion=%1 → %2").arg(*level.dataVersion).arg(mapped) });
        } else {
            warnings.append(QStringLiteral("Data.DataVersion=%1 不在对照表中").arg(*level.dataVersion));
        }
    }

    // ── 结果收敛 ─────────────────────────────────────────────────
    if (!candidates.isEmpty()) {
        QStringList distinct;
        for (const auto& candidate : candidates) {
            if (!distinct.contains(candidate.value))
                distinct.append(candidate.value);
        }

        QString chosen;
        Confidence confidence = Confidence::High;

        if (distinct.size() == 1) {
            chosen = distinct.first();
        } else {
            QStringList corroborated;
            for (const auto& value : distinct) {
                for (const auto& mention : mentions) {
                    if (mention.version == value) {
                        corroborated.append(value);
                        break;
                    }
                }
            }

            QString dataVersionCandidate;
            for (const auto& candidate : candidates) {
                if (candidate.source == QLatin1String(SOURCE_DATA_VERSION)) {
                    dataVersionCandidate = candidate.value;
                    break;
                }
            }

            if (corroborated.size() == 1) {
                chosen = corroborated.first();
                QString sourceName;
                for (const auto& mention : mentions) {
                    if (mention.version == chosen) {
                        sourceName = mention.source;
                        break;
                    }
                }
                warnings.append(QStringLiteral("存档内嵌版本字段互相冲突（%1），已依据外部旁证（%2）判定为 %3。")
                                    .arg(joinCandidates(candidates), sourceName, chosen));
            } else if (!dataVersionCandidate.isEmpty()) {
                chosen = dataVersionCandidate;
                warnings.append(QStringLiteral("存档内嵌版本字段互相冲突（%1），已按 Data.DataVersion"
                                               "（DFU 实际读取的格式标记）判定为 %2。")
                                    .arg(joinCandidates(candidates), chosen));
            } else {
                chosen = distinct.first();
                for (const auto& value : distinct) {
                    if (DataVersionTable::compareRelease(value, chosen) < 0)
                        chosen = value;
                }
                warnings.append(QStringLiteral("存档内嵌版本字段互相冲突（%1），无法裁决，已按保守策略取最低值 %2。")
                                    .arg(joinCandidates(candidates), chosen));
            }

            // 跨主次版本分歧必须人工确认
            bool crossesMinor = false;
            for (const auto& value : distinct) {
                if (majorMinor(value) != majorMinor(chosen)) {
                    crossesMinor = true;
                    break;
                }
            }
            if (crossesMinor) {
                confidence = Confidence::Medium;
                warnings.append(QStringLiteral("版本分歧跨越了主次版本号，已降低置信度，请人工确认后再启动。"));
            }
        }

        int chosenWeight = 0;
        for (const auto& candidate : candidates) {
            if (candidate.value == chosen) {
                guess.evidence.append({ candidate.source, candidate.weight, candidate.detail });
                if (chosenWeight == 0)
                    chosenWeight = candidate.weight;
            } else {
                guess.evidence.append({ candidate.source, 0, QStringLiteral("%1（未被采信）").arg(candidate.detail) });
            }
        }

        const auto entry = table.entry(chosen);
        if (entry && entry->type == QLatin1String("snapshot"))
            warnings.append(QStringLiteral("%1 是快照版本，启动器可能无法自动下载该版本核心").arg(chosen));
        if (level.snapshot && *level.snapshot)
            warnings.append(QStringLiteral("存档标记为 Snapshot，可能来自开发版"));

        guess.mcVersion = chosen;
        guess.confidence = confidence;
        guess.score = chosenWeight;
        guess.recommendedLoader = loaderOf(level, input.hasBundledMods);
        guess.warnings = warnings;
        guess.javaMajor = DataVersionTable::javaMajorFor(chosen);
        return guess;
    }

    // ── L2：旁证文本 ─────────────────────────────────────────────
    if (!mentions.isEmpty()) {
        const VersionRange range = impliedRange(input.layout);
        QList<Mention> plausible;
        for (const auto& mention : mentions) {
            if (DataVersionTable::compareRelease(mention.version, range.min) >= 0 &&
                DataVersionTable::compareRelease(mention.version, range.max) <= 0) {
                plausible.append(mention);
            }
        }
        const QList<Mention> pool = plausible.isEmpty() ? mentions : plausible;

        // 按「来源权重 × 出现次数」计分，而不是简单取最低值：
        // README 里常有大量无关数字（地图版本、人数、坐标），取最低会严重误判
        QStringList distinct;
        QHash<QString, int> scores;
        for (const auto& mention : pool) {
            const int weight = mention.source == QLatin1String(SOURCE_ARCHIVE_NAME) ? 3 : 1;
            scores[mention.version] = scores.value(mention.version) + weight;
            if (!distinct.contains(mention.version))
                distinct.append(mention.version);
        }

        QStringList ranked = distinct;
        std::sort(ranked.begin(), ranked.end(), [&scores](const QString& a, const QString& b) {
            const int delta = scores.value(b) - scores.value(a);
            if (delta != 0)
                return delta < 0;
            return DataVersionTable::compareRelease(a, b) < 0;
        });
        const QString chosen = ranked.first();

        if (distinct.size() > 1) {
            QStringList detail;
            for (const auto& value : distinct)
                detail.append(QStringLiteral("%1×%2").arg(value).arg(scores.value(value)));
            warnings.append(QStringLiteral("外部旁证给出多个版本（%1），已选择权重最高的 %2")
                                .arg(detail.join(QStringLiteral(" / ")), chosen));
        }

        for (const auto& mention : mentions) {
            const bool trusted = mention.version == chosen;
            guess.evidence.append({ mention.source, trusted ? WEIGHT_TEXT_HINT : 0,
                                    QStringLiteral("文本中出现 %1%2")
                                        .arg(mention.version, trusted ? QStringLiteral("（采信）") : QString()) });
        }

        int score = WEIGHT_TEXT_HINT + addStructuralEvidence(level, input.layout, guess.evidence);
        if (level.isModded || input.hasBundledMods)
            score += WEIGHT_FML;

        guess.mcVersion = chosen;
        guess.confidence = scoreToConfidence(std::min(score, 99));
        guess.score = score;
        guess.recommendedLoader = loaderOf(level, input.hasBundledMods);
        guess.warnings = warnings;
        guess.warnings.append(QStringLiteral("版本来自文本旁证而非存档内嵌版本号，请务必确认后再启动"));
        guess.javaMajor = DataVersionTable::javaMajorFor(chosen);
        return guess;
    }

    // ── L3：容器格式 + 时代特征 ───────────────────────────────────
    const int structuralScore = addStructuralEvidence(level, input.layout, guess.evidence);
    const QString loader = loaderOf(level, input.hasBundledMods);

    // 26.x 的新布局没有版本字段时必须拒绝猜测：1.8.9 打开会按旧格式回写，属不可逆降级
    if (isModernLayout(input.layout)) {
        guess.mcVersion = QLatin1String(UNKNOWN);
        guess.confidence = Confidence::Low;
        guess.score = structuralScore;
        guess.recommendedLoader = loader;
        guess.warnings = warnings;
        guess.warnings.append(
            QStringLiteral("存档使用「维度分目录」的新版布局（dimensions/<命名空间>/<维度>/region）且没有内嵌版本号，"
                           "无法判定具体版本。该布局自 26.1 起使用；按旧版布局猜测（1.8.9 / 1.7.10）会导致游戏以旧格式"
                           "回写世界，属于不可逆降级，因此拒绝猜测。请手动指定游戏版本。"));
        guess.javaMajor = -1;
        return guess;
    }

    const bool isAnvil = level.storageVersion && *level.storageVersion == ANVIL_VERSION;
    const bool isMcRegion = level.storageVersion && *level.storageVersion == MCREGION_VERSION;

    if (isAnvil) {
        const QString guessVersion = level.hasBorderSize ? QStringLiteral("1.8.9") : QStringLiteral("1.7.10");
        guess.mcVersion = guessVersion;
        guess.confidence = scoreToConfidence(std::min(structuralScore, 99));
        guess.score = structuralScore;
        guess.recommendedLoader = loader;
        guess.warnings = warnings;
        guess.warnings.append(level.hasBorderSize
                                  ? QStringLiteral("存档无内嵌版本号（1.8.9 及以前不写该字段），依据 Anvil 容器 + 世界边界字段"
                                                   "推断为 1.8.x。该区间（1.2.1 ~ 1.8.9）存档格式高度相似，必须人工确认。")
                                  : QStringLiteral("存档无内嵌版本号且无世界边界字段，推断为 1.7.10 或更早。"
                                                   "该区间可能包含 1.6.4 / 1.5.2 等更老版本，必须人工确认。"));
        guess.javaMajor = DataVersionTable::javaMajorFor(guessVersion);
        return guess;
    }

    guess.mcVersion = QLatin1String(UNKNOWN);
    guess.confidence = Confidence::Low;
    guess.score = structuralScore;
    guess.recommendedLoader = loader;
    guess.warnings = warnings;
    if (isMcRegion) {
        guess.warnings.append(QStringLiteral("存档为 McRegion 格式（Beta 1.3 ~ 1.1），远早于本项目支持范围。"));
        guess.warnings.append(QStringLiteral("请用对应时代的启动器打开，或手动指定版本。"));
    } else {
        guess.warnings.append(QStringLiteral("无法从 level.dat 判定版本，且无可用旁证。请手动指定游戏版本。"));
    }
    guess.javaMajor = -1;
    return guess;
}

VersionGuess MapVersionDetector::unknownVersion(const QString& reason)
{
    VersionGuess guess;
    guess.warnings.append(reason);
    return guess;
}

bool MapVersionDetector::isLaunchableVersion(const QString& version)
{
    return version != QLatin1String(UNKNOWN) && DataVersionTable::parseReleaseKey(version).has_value();
}

}  // namespace maps
