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
#include "ResourceStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>

#include "Application.h"
#include "FileSystem.h"

namespace maps {
namespace {

int countFiles(const QString& root)
{
    int count = 0;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

/// 把 src 里**目标没有的**文件复制到 dst（已存在的不覆盖：目标里可能是同一份资源）
int copyMissing(const QString& src, const QString& dst, int total, int* done, const ResourceStore::ProgressFn& progress,
                QStringList* errors)
{
    int copied = 0;
    QDirIterator it(src, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString rel = QDir(src).relativeFilePath(from);
        const QString to = FS::PathCombine(dst, rel);

        if (!QFileInfo::exists(to)) {
            if (!FS::ensureFolderPathExists(QFileInfo(to).absolutePath()) || !QFile::copy(from, to)) {
                if (errors != nullptr && errors->size() < 5)
                    errors->append(QObject::tr("复制失败：%1").arg(rel));
            } else {
                ++copied;
            }
        }

        if (progress && done != nullptr) {
            ++(*done);
            if ((*done % 64) == 0 || *done == total)
                progress(*done, total, rel);
        }
    }
    return copied;
}

/// 单个资源目录：把数据目录里的搬到目标位置
QString moveOne(const QString& name, const QString& dataRoot, const QString& sharedRoot, bool move,
                const ResourceStore::ProgressFn& progress, QStringList* errors)
{
    const QString local = FS::PathCombine(dataRoot, name);
    const QString target = FS::PathCombine(sharedRoot, name);

    if (QDir::cleanPath(local) == QDir::cleanPath(target))
        return {};

    if (!FS::ensureFolderPathExists(target)) {
        errors->append(QObject::tr("无法创建目录：%1").arg(target));
        return {};
    }

    if (!QFileInfo::exists(local))
        return QObject::tr("%1：目标目录已就绪（本地没有旧数据）").arg(name);

    if (!move)
        return QObject::tr("%1：已切换路径（旧数据仍留在数据目录，可自行删除）").arg(name);

    if (QFileInfo(local).isSymLink()) {
        errors->append(QObject::tr("%1 是目录联接，请先手动删除它再迁移").arg(local));
        return {};
    }

    // 同盘：整目录改名，瞬间完成
    if (!QFileInfo::exists(target) && QDir().rename(local, target))
        return QObject::tr("%1：已迁移").arg(name);

    // 跨盘（或目标里已有同名目录）：逐个复制，成功后再删源
    const int total = countFiles(local);
    int done = 0;
    if (progress)
        progress(0, total, name);
    QStringList copyErrors;
    const int copied = copyMissing(local, target, total, &done, progress, &copyErrors);
    if (!copyErrors.isEmpty()) {
        errors->append(QObject::tr("%1：复制未完成，旧数据保留在 %2").arg(name, local));
        errors->append(copyErrors);
        return {};
    }
    if (!QDir(local).removeRecursively()) {
        errors->append(QObject::tr("%1：新位置已复制好，但旧目录删不掉（可手动删除 %2）").arg(name, local));
        return {};
    }
    return QObject::tr("%1：已迁移 %2 个文件").arg(name).arg(copied);
}

}  // namespace

QStringList ResourceStore::managedNames()
{
    return { QStringLiteral("assets"), QStringLiteral("libraries"), QStringLiteral("java") };
}

QStringList ResourceStore::apply(const QString& sharedRoot, bool move, const ProgressFn& progress)
{
    QStringList errors;

    QString shared = QDir::fromNativeSeparators(sharedRoot.trimmed());
    if (shared.startsWith(QLatin1Char('"')) && shared.endsWith(QLatin1Char('"')))
        shared = shared.mid(1, shared.size() - 2);
    shared = QDir::cleanPath(shared);

    const QString data = QDir::cleanPath(APPLICATION->dataRoot());
    if (shared.isEmpty() || shared == data)
        return errors;  // 空 = 用数据目录，什么都不用做

    // 共享目录放在数据目录里面会把自己搬进自己
    if (shared.startsWith(data + QLatin1Char('/'))) {
        errors << QObject::tr("共享资源目录不能放在数据目录里");
        return errors;
    }

    if (!FS::ensureFolderPathExists(shared)) {
        errors << QObject::tr("无法创建共享目录：%1").arg(shared);
        return errors;
    }

    for (const QString& name : managedNames()) {
        const QString message = moveOne(name, data, shared, move, progress, &errors);
        if (!message.isEmpty())
            qInfo() << "ResourceStore:" << message;
    }
    return errors;
}

QList<ResourceStore::Candidate> ResourceStore::scanCandidates()
{
    QStringList roots;
    const auto add = [&roots](const QString& path) {
        if (!path.isEmpty() && QDir(path).exists())
            roots << QDir::cleanPath(QDir::fromNativeSeparators(path));
    };

    // 当前这个启动器的数据目录（便携时就是程序目录）
    add(APPLICATION->dataRoot());
    // 非便携模式的默认位置（升级前的旧数据、另一个 Prism 副本都可能在这儿）
    add(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!appData.isEmpty()) {
        add(FS::PathCombine(appData, ".minecraft"));     // 官方启动器
        add(FS::PathCombine(appData, "PrismLauncher"));  // 上游 Prism
        add(FS::PathCombine(appData, "MultiMC"));
        add(FS::PathCombine(appData, "PolyMC"));
    }
    if (!userProfile.isEmpty()) {
        add(FS::PathCombine(userProfile, ".minecraft"));  // 部分整合包/启动器
        add(FS::PathCombine(userProfile, ".hmcl"));
        add(FS::PathCombine(userProfile, ".minecraft", ".minecraft"));
    }
#else
    add(FS::PathCombine(QDir::homePath(), ".minecraft"));
    add(FS::PathCombine(QDir::homePath(), ".local/share/PrismLauncher"));
    add(FS::PathCombine(QDir::homePath(), ".local/share/multimc"));
#endif

    QList<Candidate> result;
    QStringList seen;
    for (const QString& root : roots) {
        if (seen.contains(root))
            continue;
        seen << root;

        Candidate candidate{ root, false, false, false };
        candidate.assets = QDir(FS::PathCombine(root, "assets")).exists();
        candidate.libraries = QDir(FS::PathCombine(root, "libraries")).exists();
        candidate.java = QDir(FS::PathCombine(root, "java")).exists();
        if (candidate.any())
            result << candidate;
    }
    return result;
}

}  // namespace maps
