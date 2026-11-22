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
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrent>

#include "Application.h"
#include "FileSystem.h"

namespace maps {
namespace {
constexpr int CACHE_SCHEMA = 1;
constexpr const char* CACHE_FILE = "hall-cache.json";

/**
 * slug 索引的进程内缓存。
 *
 * 谁会同时碰它：导入任务在**工作线程**里查站上标题（`MapImportTask`），
 * 本地库面板与实例卡片在 **UI 线程**里查同一张表。原来这张表是函数内静态
 * `QHash`，没有锁 —— 一边 `clear()+insert()` 重哈希、另一边 `value()` 就是
 * 未定义行为（审查 P0-1）。现在：锁只保护"命中判断 / 整体换入"，解析在锁外，
 * 所以 UI 不会被 3.6 MB 的解析按住。
 */
QMutex g_indexMutex;
QHash<QString, QJsonObject> g_index;
QDateTime g_indexStamp;

QHash<QString, QJsonObject> buildIndex(const HallSnapshot& snapshot)
{
    QHash<QString, QJsonObject> built;
    built.reserve(snapshot.listRows.size());
    for (const auto& value : snapshot.listRows) {
        const QJsonObject row = value.toObject();
        const QString key = row.value(QStringLiteral("slug")).toString();
        if (!key.isEmpty())
            built.insert(key, row);
    }
    return built;
}
}  // namespace

QString HallCache::path()
{
    auto* app = APPLICATION_DYN;
    const QString root = app != nullptr ? app->dataRoot() : QDir::currentPath();
    return FS::PathCombine(root, "maps", QString::fromLatin1(CACHE_FILE));
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

QJsonObject HallCache::findListRow(const QString& slug)
{
    if (slug.isEmpty())
        return {};

    const QFileInfo info(path());
    if (!info.exists()) {
        QMutexLocker lock(&g_indexMutex);
        g_index.clear();
        g_indexStamp = QDateTime();
        return {};
    }

    // 文件被同步流程整体换掉时 mtime 会变，此时重建成索引
    const QDateTime stamp = info.lastModified();
    {
        QMutexLocker lock(&g_indexMutex);
        if (!g_index.isEmpty() && stamp == g_indexStamp)
            return g_index.value(slug);
    }

    const HallSnapshot snapshot = load();
    QMutexLocker lock(&g_indexMutex);
    g_index = buildIndex(snapshot);
    g_indexStamp = stamp;
    return g_index.value(slug);
}

void HallCache::warm()
{
    const QFileInfo info(path());
    if (!info.exists())
        return;

    const QDateTime stamp = info.lastModified();
    {
        QMutexLocker lock(&g_indexMutex);
        if (!g_index.isEmpty() && stamp == g_indexStamp)
            return;
    }

    const HallSnapshot snapshot = load();
    QMutexLocker lock(&g_indexMutex);
    g_index = buildIndex(snapshot);
    g_indexStamp = stamp;
}

void HallCache::warmAsync()
{
    // 启动时就把 3.6 MB 解析掉：这份表第一次被查往往发生在"打开本地库"这种
    // 该立刻出画面的动作里（审查 P1-3）。
    QtConcurrent::run(QThreadPool::globalInstance(), [] { HallCache::warm(); });
}

void HallCache::clear()
{
    QFile::remove(path());
    QMutexLocker lock(&g_indexMutex);
    g_index.clear();
    g_indexStamp = QDateTime();
}

qint64 HallCache::sizeBytes()
{
    QFileInfo info(path());
    return info.exists() ? info.size() : 0;
}

}  // namespace maps
