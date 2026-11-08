// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas - Minecraft Launcher
 *  Copyright (c) 2026 Atlas contributors
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
#include "MapDiskCleanup.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QThreadPool>

#include "Application.h"
#include "FileSystem.h"
#include "ResourcePaths.h"
#include "settings/SettingsObject.h"

namespace maps {
namespace {

qint64 calculateDirectorySize(const QString& dirPath)
{
    qint64 size = 0;
    QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        size += it.fileInfo().size();
    }
    return size;
}

}  // namespace

CleanupStats MapDiskCleanup::cleanDirectoryTree(const QString& rootDir, int maxAgeDays)
{
    CleanupStats stats;
    if (rootDir.isEmpty() || !QDir(rootDir).exists())
        return stats;

    const QDateTime now = QDateTime::currentDateTime();

    QDirIterator it(rootDir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString fileName = it.fileName();
        const QString filePath = it.filePath();

        // 1. *.part.parallel（分片临时空洞文件）或 *.part-*（私有 Sink 临时占用文件）
        if (fileName.endsWith(QStringLiteral(".part.parallel")) || fileName.contains(QStringLiteral(".part-"))) {
            const qint64 size = it.fileInfo().size();
            if (QFile::remove(filePath)) {
                stats.removedParallelPartFiles++;
                stats.freedBytes += size;
            }
            continue;
        }

        // 2. 超过 maxAgeDays 天未修改的 *.part 悬挂断点文件
        if (fileName.endsWith(QStringLiteral(".part"))) {
            const QDateTime mtime = it.fileInfo().lastModified();
            if (mtime.isValid() && mtime.daysTo(now) >= maxAgeDays) {
                const qint64 size = it.fileInfo().size();
                if (QFile::remove(filePath)) {
                    stats.removedStalePartFiles++;
                    stats.freedBytes += size;
                }
            }
        }
    }

    return stats;
}

CleanupStats MapDiskCleanup::cleanStaleResidues(int maxAgeDays)
{
    CleanupStats totalStats;

    // 1. 清理 instances/.tmp 孤儿临时目录（上次意外崩溃/强杀留下的半截导入）
    QString instancesDir;
    if (APPLICATION_DYN && APPLICATION_DYN->settings()) {
        instancesDir = APPLICATION_DYN->settings()->get("InstanceDir").toString();
    }
    if (instancesDir.isEmpty()) {
        instancesDir = FS::PathCombine(QDir::currentPath(), "instances");
    }

    if (!instancesDir.isEmpty()) {
        const QString tmpDir = FS::PathCombine(instancesDir, ".tmp");
        if (QDir(tmpDir).exists()) {
            QDir dir(tmpDir);
            const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto& info : entries) {
                const qint64 size = calculateDirectorySize(info.absoluteFilePath());
                if (FS::deletePath(info.absoluteFilePath())) {
                    totalStats.removedTmpInstanceDirs++;
                    totalStats.freedBytes += size;
                }
            }
        }
    }

    // 2. 清理 assets, libraries, java, meta, instances 里的悬挂/过期 part 文件
    QStringList targetDirs;
    targetDirs << resource::assetsDir()
               << resource::librariesDir()
               << resource::javaDir();

    if (APPLICATION_DYN) {
        targetDirs << FS::PathCombine(APPLICATION_DYN->dataRoot(), "meta");
    } else {
        targetDirs << FS::PathCombine(QDir::currentPath(), "meta");
    }
    if (!instancesDir.isEmpty()) {
        targetDirs << instancesDir;
    }

    targetDirs.removeDuplicates();

    for (const auto& path : targetDirs) {
        const auto stats = cleanDirectoryTree(path, maxAgeDays);
        totalStats.removedParallelPartFiles += stats.removedParallelPartFiles;
        totalStats.removedStalePartFiles += stats.removedStalePartFiles;
        totalStats.freedBytes += stats.freedBytes;
    }

    if (totalStats.removedParallelPartFiles > 0 || totalStats.removedStalePartFiles > 0 || totalStats.removedTmpInstanceDirs > 0) {
        qInfo() << "MapDiskCleanup: freed" << totalStats.freedBytes << "bytes ("
                << totalStats.removedParallelPartFiles << "parallel part files,"
                << totalStats.removedStalePartFiles << "stale part files,"
                << totalStats.removedTmpInstanceDirs << "orphaned tmp instance dirs)";
    }

    return totalStats;
}

void MapDiskCleanup::cleanAsync(int maxAgeDays)
{
    QThreadPool::globalInstance()->start([maxAgeDays]() {
        cleanStaleResidues(maxAgeDays);
    });
}

}  // namespace maps
