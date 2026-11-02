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
#include "HallCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include "Application.h"
#include "FileSystem.h"

namespace maps {
namespace {
constexpr int CACHE_SCHEMA = 1;
constexpr const char* CACHE_FILE = "hall-cache.json";
}  // namespace

QString HallCache::path()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "maps", QString::fromLatin1(CACHE_FILE));
}

HallSnapshot HallCache::load()
{
    HallSnapshot snapshot;
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return snapshot;

    const auto document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject())
        return snapshot;

    const auto root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != CACHE_SCHEMA)
        return snapshot;  // 版本不对就当没有，下一次同步会重写

    snapshot.fetchedAt = QDateTime::fromString(root.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    snapshot.listRows = root.value(QStringLiteral("list")).toArray();
    snapshot.detailRows = root.value(QStringLiteral("details")).toArray();
    snapshot.userRows = root.value(QStringLiteral("users")).toArray();
    snapshot.reviewRows = root.value(QStringLiteral("reviews")).toArray();
    return snapshot;
}

bool HallCache::detailsCoverList(const HallSnapshot& snapshot)
{
    if (!snapshot.isValid())
        return false;

    QSet<QString> detailSlugs;
    detailSlugs.reserve(snapshot.detailRows.size());
    for (const auto& value : snapshot.detailRows)
        detailSlugs.insert(value.toObject().value(QStringLiteral("slug")).toString());

    for (const auto& value : snapshot.listRows) {
        const QString slug = value.toObject().value(QStringLiteral("slug")).toString();
        if (!slug.isEmpty() && !detailSlugs.contains(slug))
            return false;
    }
    return true;
}

bool HallCache::save(const HallSnapshot& snapshot, QString* error)
{
    const QString target = path();
    QDir().mkpath(QFileInfo(target).absolutePath());

    QJsonObject root;
    root.insert(QStringLiteral("schema"), CACHE_SCHEMA);
    root.insert(QStringLiteral("fetchedAt"), snapshot.fetchedAt.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("list"), snapshot.listRows);
    root.insert(QStringLiteral("details"), snapshot.detailRows);
    root.insert(QStringLiteral("users"), snapshot.userRows);
    root.insert(QStringLiteral("reviews"), snapshot.reviewRows);

    // QSaveFile：写失败时原文件不动，不会留下"半个缓存"
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    return true;
}

void HallCache::clear()
{
    QFile::remove(path());
}

qint64 HallCache::sizeBytes()
{
    QFileInfo info(path());
    return info.exists() ? info.size() : 0;
}

}  // namespace maps
