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
#include "MapHallLink.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "FileSystem.h"
#include "HallCache.h"

namespace maps {
namespace {

/// 归一化：小写、去排版字符、压空白。中文原样保留。
QString normalize(const QString& raw)
{
    QString text = raw.toLower();
    text.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
    return text;
}

}  // namespace

QString MapHallLink::fileName()
{
    return QStringLiteral("hall-link.json");
}

MapHallLink MapHallLink::load(const QString& instanceRoot)
{
    MapHallLink link;
    QFile file(FS::PathCombine(instanceRoot, fileName()));
    if (!file.open(QIODevice::ReadOnly))
        return link;

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return link;

    const auto root = document.object();
    link.slug = root.value(QStringLiteral("slug")).toString();
    link.mapUuid = root.value(QStringLiteral("mapUuid")).toString();
    link.title = root.value(QStringLiteral("title")).toString();
    link.linkedAt = QDateTime::fromString(root.value(QStringLiteral("linkedAt")).toString(), Qt::ISODate);
    return link;
}

bool MapHallLink::save(const QString& instanceRoot, QString* error) const
{
    QJsonObject root;
    root.insert(QStringLiteral("slug"), slug);
    root.insert(QStringLiteral("mapUuid"), mapUuid);
    root.insert(QStringLiteral("title"), title);
    root.insert(QStringLiteral("linkedAt"), (linkedAt.isValid() ? linkedAt : QDateTime::currentDateTimeUtc())
                                                .toUTC()
                                                .toString(Qt::ISODate));

    QSaveFile file(FS::PathCombine(instanceRoot, fileName()));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    return true;
}

void MapHallLink::clear(const QString& instanceRoot)
{
    QFile::remove(FS::PathCombine(instanceRoot, fileName()));
}

QString MapHallLink::suggestSlugFor(const QString& instanceName)
{
    const QString wanted = normalize(instanceName);
    if (wanted.isEmpty())
        return {};

    const HallSnapshot snapshot = HallCache::load();
    if (!snapshot.isValid())
        return {};

    QString best;
    int bestScore = 0;
    for (const auto& value : snapshot.listRows) {
        const auto row = value.toObject();
        const QString slug = row.value(QStringLiteral("slug")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        if (slug.isEmpty() || name.isEmpty())
            continue;

        const QString candidate = normalize(name);
        if (candidate.isEmpty())
            continue;

        int score = 0;
        if (candidate == wanted)
            score = 1000;
        else if (candidate.contains(wanted) || wanted.contains(candidate)) {
            // 长短差太多就是"名字里恰好含这几个字"，不算
            const int shorter = qMin(candidate.size(), wanted.size());
            const int longer = qMax(candidate.size(), wanted.size());
            if (shorter * 100 >= longer * 70)
                score = shorter * 100 / longer;
        }
        if (score > bestScore) {
            bestScore = score;
            best = slug;
        }
    }
    // 阈值：完全相等，或者"包含且长度差在 30% 以内"
    return bestScore >= 70 ? best : QString();
}

}  // namespace maps
