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
#include "HallMatcher.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

#include "HallCache.h"
#include "MapVariant.h"

namespace maps {

namespace {

QStringList tokens(const QString& normalized)
{
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

/// 站上的 `version` 字段可能是 "1.20.1 Forge"，只取前面的数字部分
QString leadingVersion(const QString& raw)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+(?:\\.\\d+)*)"));
    const auto match = re.match(raw);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString majorMinor(const QString& version)
{
    const auto parts = version.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return version;
    return QStringLiteral("%1.%2").arg(parts[0], parts[1]);
}

/// 罗马数字（只到 39，够系列编号用）
QString romanNumeral(int value)
{
    static const char* ones[] = { "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" };
    static const char* tens[] = { "", "x", "xx", "xxx" };
    if (value <= 0 || value >= 40)
        return {};
    return QString::fromLatin1(tens[value / 10]) + QString::fromLatin1(ones[value % 10]);
}

/// 归一化名里有没有这个编号：`7`、`07`、`#8`、`VII` 都算
bool hasNumberToken(const QString& normalized, int number)
{
    if (number <= 0)
        return false;

    const QString roman = romanNumeral(number);
    for (const auto& token : normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString digits;
        for (const QChar ch : token) {
            if (ch.isDigit())
                digits.append(ch);
        }
        if (!digits.isEmpty()) {
            bool ok = false;
            const int value = digits.toInt(&ok);
            if (ok && value == number)
                return true;
            continue;
        }
        if (!roman.isEmpty() && token.compare(roman, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/**
 * 系列证据：站上把「Untold Stories 09 - Castlemania」记成
 * `name = Castlemania` + `series = Untold Stories` + `series_num = 9`，
 * 本地文件名却带着系列名和编号。只看名字相似度只能拿到 0.88，加上系列与编号就很硬。
 *
 * 三种情况要分开：
 *   · 系列名都不在本地名里 → 与系列无关，**不能**因为站上有编号就扣分
 *     （`Divinity's End` 的系列编号是 1，本地名字里根本没写 1）；
 *   · 系列名在、编号也在 → 0.92；
 *   · 系列名在、本地写了**另一个**编号 → 大概率是相邻的那一作，扣分。
 */
struct SeriesEvidence {
    bool seriesSeen = false;
    bool numberSeen = false;
};

SeriesEvidence matchSeries(const QStringList& localNames, const QString& series, int seriesNumber)
{
    SeriesEvidence evidence;
    const QString seriesNormalized = variant::normalizeName(series);
    if (seriesNormalized.isEmpty())
        return evidence;

    for (const auto& local : localNames) {
        if (!local.contains(seriesNormalized))
            continue;
        evidence.seriesSeen = true;
        if (seriesNumber > 0 && hasNumberToken(local, seriesNumber)) {
            evidence.numberSeen = true;
            break;
        }
    }
    return evidence;
}

/// 本地名里有没有**任何**编号（阿拉伯数字或罗马数字），用来判断"写的是别的编号"
bool hasAnyNumberToken(const QStringList& localNames)
{
    static const QRegularExpression roman(QStringLiteral("^[ivx]+$"));
    for (const auto& local : localNames) {
        for (const auto& token : local.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            for (const QChar ch : token) {
                if (ch.isDigit())
                    return true;
            }
            if (token.size() <= 4 && roman.match(token).hasMatch())
                return true;
        }
    }
    return false;
}

/**
 * 名称相似度：[0, 1]。
 *
 * 三档，按可信度递减：
 *   完全一致 1.0 → 「名称一致」
 *   一方包含另一方（逐词）→ 0.75 + 0.2 × 短/长 → 「名称包含」
 *   词集 Jaccard → 原值 → 「名称相近」/「少量词重合」
 *
 * 不做编辑距离：地图名里作者署名、章节号、版本号常常多一段，
 * 编辑距离会把「Tower of Sinner」和「Tower of Prison」判得很近（同一系列的相邻两作）。
 */
double nameSimilarity(const QString& left, const QString& right, QString* reason)
{
    if (left.isEmpty() || right.isEmpty())
        return 0.0;
    if (left == right) {
        *reason = QStringLiteral("名称一致");
        return 1.0;
    }

    const auto leftTokens = tokens(left);
    const auto rightTokens = tokens(right);
    if (leftTokens.isEmpty() || rightTokens.isEmpty())
        return 0.0;

    const QSet<QString> leftSet(leftTokens.begin(), leftTokens.end());
    const QSet<QString> rightSet(rightTokens.begin(), rightTokens.end());

    QSet<QString> intersection = leftSet;
    intersection.intersect(rightSet);

    const bool leftInRight = intersection.size() == leftSet.size();
    const bool rightInLeft = intersection.size() == rightSet.size();
    if (leftInRight || rightInLeft) {
        const int shorter = qMin(leftSet.size(), rightSet.size());
        const int longer = qMax(leftSet.size(), rightSet.size());
        *reason = QStringLiteral("名称包含");
        return 0.75 + 0.2 * (static_cast<double>(shorter) / longer);
    }

    QSet<QString> unionSet = leftSet;
    unionSet.unite(rightSet);
    const double jaccard = static_cast<double>(intersection.size()) / unionSet.size();
    *reason = jaccard >= 0.5 ? QStringLiteral("名称相近") : QStringLiteral("少量词重合");
    return jaccard;
}

}  // namespace

QList<HallMatchCandidate> HallMatcher::suggest(const HallMatchQuery& query, int max)
{
    const auto snapshot = HallCache::load();
    if (!snapshot.isValid())
        return {};
    return rank(snapshot.listRows, query, max);
}

QList<HallMatchCandidate> HallMatcher::rank(const QJsonArray& rows, const HallMatchQuery& query, int max)
{
    // 本地名优先用索引里的英文名；没有就用中文名；再没有就用文件名（去掉版本/括号）
    QStringList localNames;
    if (!query.englishName.isEmpty())
        localNames << query.englishName;
    if (!query.chineseName.isEmpty())
        localNames << query.chineseName;
    if (!query.fileName.isEmpty())
        localNames << query.fileName;

    QStringList normalizedLocal;
    for (const auto& name : localNames) {
        const QString normalized = variant::normalizeName(name);
        if (!normalized.isEmpty() && !normalizedLocal.contains(normalized))
            normalizedLocal << normalized;
    }

    QList<HallMatchCandidate> candidates;
    if (normalizedLocal.isEmpty())
        return candidates;

    const QString localVersion = leadingVersion(query.mcVersion);

    for (const auto& value : rows) {
        const auto row = value.toObject();
        const QString slug = row.value("slug").toString();
        const QString hallName = row.value("name").toString();
        if (slug.isEmpty() || hallName.isEmpty())
            continue;

        const QString normalizedHall = variant::normalizeName(hallName);
        if (normalizedHall.isEmpty())
            continue;

        double best = 0.0;
        QString bestReason;
        for (const auto& normalized : normalizedLocal) {
            QString reason;
            const double score = nameSimilarity(normalized, normalizedHall, &reason);
            if (score > best) {
                best = score;
                bestReason = reason;
            }
        }

        // 系列 + 编号是比名字更硬的证据（站上把「Untold Stories 09 - Castlemania」
        // 存成 name=Castlemania、series=Untold Stories、series_num=9）
        const QString series = row.value("series").toString();
        const int seriesNumber = row.value("series_num").toInt(-1);
        const auto seriesEvidence = matchSeries(normalizedLocal, series, seriesNumber);
        if (seriesEvidence.seriesSeen) {
            if (seriesNumber <= 0) {
                // 只对上系列名（站上没填编号）：算个中等证据，理由写清楚是"同系列"
                if (best < 0.85) {
                    best = 0.85;
                    bestReason = QStringLiteral("《%1》同系列").arg(series);
                }
            } else if (seriesEvidence.numberSeen) {
                if (best < 0.92) {
                    best = 0.92;
                    bestReason = QStringLiteral("《%1》第 %2 作").arg(series).arg(seriesNumber);
                }
            } else if (hasAnyNumberToken(normalizedLocal)) {
                // 系列对得上、本地写的是**另一个**编号 → 大概率是相邻的另一作。
                // 这时不能拿"第 N 作"当理由（那会替玩家编一个不存在的对应关系）。
                best *= 0.85;
            }
        }

        // 版本只做加分/减分，不改名次的主体（站上的版本字段常带 Forge 后缀、也可能留空）
        QString versionNote;
        const QString hallVersion = leadingVersion(row.value("version").toString());
        if (!localVersion.isEmpty() && !hallVersion.isEmpty()) {
            if (localVersion == hallVersion) {
                best += 0.08;
                versionNote = QStringLiteral("，MC %1 一致").arg(hallVersion);
            } else if (majorMinor(localVersion) == majorMinor(hallVersion)) {
                best += 0.03;
                versionNote = QStringLiteral("，MC 同属 %1").arg(majorMinor(hallVersion));
            } else {
                best -= 0.35;
                versionNote = QStringLiteral("，但站上是 MC %1").arg(hallVersion);
            }
        }

        if (best < kMinScore)
            continue;

        HallMatchCandidate candidate;
        candidate.slug = slug;
        candidate.name = hallName;
        candidate.version = row.value("version").toString();
        candidate.series = row.value("series").toString();
        candidate.score = qBound(0.0, best, 1.0);
        candidate.reason = bestReason + versionNote;
        candidates << candidate;
    }

    std::sort(candidates.begin(), candidates.end(), [](const HallMatchCandidate& left, const HallMatchCandidate& right) {
        if (!qFuzzyCompare(left.score + 1.0, right.score + 1.0))
            return left.score > right.score;
        return left.name.localeAwareCompare(right.name) < 0;
    });

    if (max > 0 && candidates.size() > max)
        candidates = candidates.mid(0, max);
    return candidates;
}

}  // namespace maps
