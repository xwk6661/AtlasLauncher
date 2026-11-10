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
#include "MapRepository.h"
#include "MapMeta.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "MapTextUtils.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

namespace maps {
namespace {

QString sha1OfFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

}  // namespace

QString MapRepository::libraryDir()
{
    if (!APPLICATION || !APPLICATION->settings())
        return {};
    const QString configured = APPLICATION->settings()->get("MapLibraryDir").toString();
    if (!configured.isEmpty())
        return configured;
    return FS::PathCombine(APPLICATION->dataRoot(), "maps");
}

MapEntry MapRepository::describe(MinecraftInstance* instance)
{
    MapEntry entry;
    if (instance == nullptr)
        return entry;

    entry.instanceId = instance->id();
    entry.instanceRoot = instance->instanceRoot();
    // 老实例名里可能已经写进了 § 格式化码，显示时剥掉
    entry.displayName = text::stripFormattingCodes(instance->name());
    entry.meta = MapMeta::load(entry.instanceRoot);
    entry.notes = MapNotes::load(entry.instanceRoot);

    const QString cover = FS::PathCombine(entry.instanceRoot, "icon.png");
    if (QFile::exists(cover))
        entry.coverPath = cover;

    if (auto profile = instance->getPackProfile()) {
        // 顺序有讲究：Quilt 继承 Fabric、NeoForge 继承 Forge，反着的组件也常同时存在
        struct Loader {
            const char* uid;
            const char* name;
        };
        static constexpr Loader kLoaders[] = {
            { "org.quiltmc.quilt-loader", "Quilt" },
            { "net.neoforged", "NeoForge" },
            { "net.minecraftforge", "Forge" },
            { "net.fabricmc.fabric-loader", "Fabric" },
        };
        for (const auto& loader : kLoaders) {
            const QString version = profile->getComponentVersion(QString::fromLatin1(loader.uid));
            if (!version.isEmpty()) {
                entry.loaderName = QString::fromLatin1(loader.name);
                entry.loaderVersion = version;
                break;
            }
        }
    }
    if (entry.loaderName.isEmpty())
        entry.loaderName = QStringLiteral("Vanilla");

    entry.confidence = entry.meta.version.confidence;
    QString badge = entry.meta.version.isKnown()
                        ? entry.meta.version.mcVersion
                        : QCoreApplication::translate("MapRepository", "版本未知");
    if (entry.meta.loader.type != QLatin1String("vanilla") && !entry.meta.loader.type.isEmpty())
        badge += QStringLiteral(" · %1").arg(entry.meta.loader.type);
    entry.versionBadge = badge;

    if (entry.displayName.isEmpty())
        entry.displayName = entry.meta.title;
    // 中文名优先（AIO 的 UserNote.alias 语义）：卡片、右侧标题、窗口标题都认它
    entry.displayName = entry.notes.displayName(entry.displayName);

    return entry;
}

QList<MapEntry> MapRepository::collect(InstanceList* instances)
{
    QList<MapEntry> out;
    if (instances == nullptr)
        return out;

    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (instance == nullptr)
            continue;
        // 唯一的判据：实例根目录下有 map.json
        if (!QFile::exists(FS::PathCombine(instance->instanceRoot(), MapMeta::fileName())))
            continue;
        out.append(describe(instance));
    }

    // 最近玩过的排前面；没玩过的按名字
    std::sort(out.begin(), out.end(), [](const MapEntry& a, const MapEntry& b) {
        const bool aPlayed = a.meta.stats.lastPlayed.isValid();
        const bool bPlayed = b.meta.stats.lastPlayed.isValid();
        if (aPlayed != bPlayed)
            return aPlayed;
        if (aPlayed && bPlayed && a.meta.stats.lastPlayed != b.meta.stats.lastPlayed)
            return a.meta.stats.lastPlayed > b.meta.stats.lastPlayed;
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });
    return out;
}

QString MapRepository::resolveArchivePath(const MapMeta& meta)
{
    if (!meta.archive.libraryCopy.isEmpty() && QFileInfo::exists(meta.archive.libraryCopy))
        return meta.archive.libraryCopy;

    if (!meta.archive.libraryCopy.isEmpty()) {
        const QString sameName = FS::PathCombine(libraryDir(), QFileInfo(meta.archive.libraryCopy).fileName());
        if (QFileInfo::exists(sameName))
            return sameName;
    }

    if (!meta.archive.path.isEmpty() && QFileInfo::exists(meta.archive.path))
        return meta.archive.path;
    return {};
}

QString MapRepository::importIntoLibrary(const QString& archivePath)
{
    QFileInfo source(archivePath);
    if (!source.exists() || !source.isFile())
        return {};

    const QString dir = libraryDir();
    if (!FS::ensureFolderPathExists(dir))
        return {};

    QString target = FS::PathCombine(dir, source.fileName());
    if (QFile::exists(target)) {
        // 先比大小：几百 MB 的地图做两次全量 SHA1 会明显卡住导入流程，
        // 大小不同就直接当成另一个包，大小相同才值得算哈希。
        const QFileInfo existing(target);
        if (existing.size() == source.size() && sha1OfFile(target) == sha1OfFile(source.absoluteFilePath())) {
            // 同一个包已经在库里了：直接复用库里的那份，并把外面的原件清掉 ——
            // 留两份只会让人分不清该删哪个（用户明确要求"不要库副本，直接移动"）
            if (source.absoluteFilePath() != target)
                QFile::remove(source.absoluteFilePath());
            return target;
        }
        const QString suffix = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        target = FS::PathCombine(dir, QStringLiteral("%1-%2.%3").arg(source.completeBaseName(), suffix, source.suffix()));
    }

    /**
     * **移动**而不是复制：库里的这份就是重装用的唯一权威副本，
     * 原位置再留一份纯属占空间。跨盘移动 FS::move 会自动退化成复制+删除。
     * 万一移动失败（文件被占用等），退回复制 —— 导入不能因为这一步黄了。
     */
    if (FS::move(source.absoluteFilePath(), target)) {
        qInfo() << "MapRepository: moved archive into library:" << target;
        return target;
    }
    qWarning() << "MapRepository: move failed, falling back to copy:" << source.absoluteFilePath();
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        qWarning() << "MapRepository: failed to copy" << source.absoluteFilePath() << "to" << target;
        return {};
    }
    return target;
}

}  // namespace maps
