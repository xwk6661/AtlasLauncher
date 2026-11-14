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
#include "MapPackAttach.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "FileSystem.h"
#include "MMCZip.h"
#include "archive/ArchiveReader.h"

namespace maps {

namespace {

/// 打包工具常加 "./" 或前导斜杠，先剥掉再比
QString normalizedEntry(const QString& raw)
{
    QString path = raw;
    // Windows 上某些打包路径用反斜杠；统一成 '/' 再按层数判断
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1String("./")))
        path.remove(0, 2);
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    return path;
}

QString baseNameOf(const QString& path)
{
    return path.section(QLatin1Char('/'), -1);
}

}  // namespace

ResourcePackProbe MapPackAttach::inspect(const QString& archivePath)
{
    ResourcePackProbe probe;
    if (!QFileInfo::exists(archivePath)) {
        probe.reason = QStringLiteral("文件不存在：%1").arg(archivePath);
        return probe;
    }

    QStringList packMetas;
    bool hasLevelDat = false;
    bool hasPackPng = false;

    MMCZip::ArchiveReader reader(archivePath);
    // 注意：这个回调返回 **true 表示停止**（见 ArchiveReader::parse 的 shouldStop）。
    // 写成 true 的话只会看第一个条目 —— 一个「包内有 pack.mcmeta、也有 level.dat」的地图包
    // 就会被误判成资源包，然后在导入那一步建出个空世界。
    const auto parsed = reader.parse([&](MMCZip::ArchiveReader::File* file) -> Result<bool> {
        if (!file->isFile())
            return false;
        const QString path = normalizedEntry(file->filename());
        if (path.isEmpty())
            return false;

        const QString name = baseNameOf(path).toLower();
        if (name == QLatin1String("level.dat"))
            hasLevelDat = true;
        else if (name == QLatin1String("pack.mcmeta"))
            packMetas << path;
        else if (name == QLatin1String("pack.png"))
            hasPackPng = true;
        return false;
    });

    if (!parsed) {
        probe.reason = QStringLiteral("读不了这个压缩包：%1").arg(parsed.error());
        return probe;
    }

    // 有 level.dat 就是地图包 —— 那种包该走导入，不是挂载
    if (hasLevelDat) {
        probe.reason = QStringLiteral("这是地图包（含 level.dat），不是资源包");
        return probe;
    }
    if (packMetas.isEmpty()) {
        probe.reason = hasPackPng ? QStringLiteral("有 pack.png 但没有 pack.mcmeta，不像资源包")
                                  : QStringLiteral("包里没有 pack.mcmeta，不像资源包");
        return probe;
    }

    // 只认根目录或唯一的一级子目录；埋得更深的认不出包根，宁可报错也不猜
    QString best;
    for (const auto& path : packMetas) {
        if (path.count(QLatin1Char('/')) > 1)
            continue;
        if (best.isEmpty() || path.count(QLatin1Char('/')) < best.count(QLatin1Char('/')))
            best = path;
    }
    if (best.isEmpty()) {
        probe.reason = QStringLiteral("pack.mcmeta 埋得太深（两层以上），认不出包根");
        return probe;
    }

    const int slash = best.lastIndexOf(QLatin1Char('/'));
    probe.rootFolder = slash < 0 ? QString() : best.left(slash);
    probe.packId = probe.rootFolder.isEmpty() ? QFileInfo(archivePath).completeBaseName()
                                              : baseNameOf(probe.rootFolder);
    probe.valid = true;
    return probe;
}

Result<QString> MapPackAttach::attach(const QString& archivePath, const QString& instanceRoot)
{
    const auto probe = inspect(archivePath);
    if (!probe.valid)
        return std::unexpected{ probe.reason };

    const QString optionsPath = FS::PathCombine(instanceRoot, QStringLiteral(".minecraft/options.txt"));
    // 没有 options.txt 说明这个实例还没被游戏读过一次；这时写入的 resourcePacks 会在
    // 第一次启动时被当成"没有 version 的老文件"整份重写（见 MapOptionsTxt 的说明）。
    if (!QFile::exists(optionsPath))
        return std::unexpected{ QStringLiteral("这个实例还没有 options.txt —— 先启动一次游戏，再回来挂资源包") };

    // 先把 options 读进来并算出该写什么，**确认能写**再去动文件；
    // 否则会出现"包放进去了、设置没改"，玩家看到的是游戏里没生效。
    QStringList lines;
    {
        QFile existing(optionsPath);
        if (!existing.open(QIODevice::ReadOnly))
            return std::unexpected{ QStringLiteral("读不了 options.txt：%1").arg(QDir::toNativeSeparators(optionsPath)) };
        lines = QString::fromUtf8(existing.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    const QString packEntry = probe.rootFolder.isEmpty()
                                  ? QStringLiteral("file/%1").arg(QFileInfo(archivePath).fileName())
                                  : QStringLiteral("file/%1").arg(probe.packId);

    if (!addToOptionsLines(lines, packEntry)) {
        // false 有两种：已经启用过（好事）与写法认不出（不能动）。用是否已包含来区分。
        bool alreadyEnabled = false;
        for (const auto& line : lines) {
            if (line.startsWith(QLatin1String("resourcePacks:")) && line.contains(packEntry)) {
                alreadyEnabled = true;
                break;
            }
        }
        if (!alreadyEnabled)
            return std::unexpected{
                QStringLiteral("options.txt 里的 resourcePacks 写法认不出，没有改动。\n原行：%1")
                    .arg(QDir::toNativeSeparators(optionsPath))
            };
    }

    const QString packsDir = FS::PathCombine(instanceRoot, QStringLiteral(".minecraft/resourcepacks"));
    if (!FS::ensureFolderPathExists(packsDir))
        return std::unexpected{ QStringLiteral("建不了资源包目录：%1").arg(packsDir) };

    if (probe.rootFolder.isEmpty()) {
        const QString target = FS::PathCombine(packsDir, QFileInfo(archivePath).fileName());
        if (!QFileInfo::exists(target)) {
            if (!QFile::copy(archivePath, target))
                return std::unexpected{ QStringLiteral("复制资源包失败：%1").arg(QDir::toNativeSeparators(target)) };
        }
    } else {
        const QString target = FS::PathCombine(packsDir, probe.packId);
        if (!QFileInfo::exists(target)) {
            if (!FS::ensureFolderPathExists(target))
                return std::unexpected{ QStringLiteral("建不了目录：%1").arg(target) };
            const auto extracted = MMCZip::extractDir(archivePath, probe.rootFolder, target);
            if (!extracted)
                return std::unexpected{ QStringLiteral("解出资源包失败：%1").arg(extracted.error()) };
        }
    }

    QSaveFile out(optionsPath);
    if (!out.open(QIODevice::WriteOnly))
        return std::unexpected{ QStringLiteral("写不了 options.txt：%1").arg(QDir::toNativeSeparators(optionsPath)) };
    out.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (!out.commit())
        return std::unexpected{ QStringLiteral("保存 options.txt 失败：%1").arg(QDir::toNativeSeparators(optionsPath)) };

    return packEntry;
}

bool MapPackAttach::addToOptionsLines(QStringList& lines, const QString& packEntry)
{
    if (packEntry.isEmpty())
        return false;

    const QString prefix = QStringLiteral("resourcePacks:");
    for (auto& line : lines) {
        if (!line.startsWith(prefix))
            continue;

        // 认不出的写法一个字节都不动 —— 这是玩家/游戏写的数组，写坏会让整份设置失效
        const auto document = QJsonDocument::fromJson(line.mid(prefix.size()).toUtf8());
        if (!document.isArray())
            return false;

        auto array = document.array();
        for (const auto& value : array) {
            if (value.toString() == packEntry)
                return false;
        }
        array.append(packEntry);
        line = prefix + QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
        return true;
    }

    QJsonArray array;
    array.append(QStringLiteral("vanilla"));  // 原版包必须在，否则资源包链断了
    array.append(packEntry);
    lines.append(prefix + QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    return true;
}

}  // namespace maps
