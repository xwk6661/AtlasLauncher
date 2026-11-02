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
#include "MapArchiveScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

#include "DataVersionTable.h"
#include "LevelDatReader.h"
#include "MapTextUtils.h"
#include "archive/ArchiveReader.h"

namespace maps {
namespace {

struct Entry {
    QString path;
    qint64 size = 0;
    bool isDir = false;
};

QString baseName(const QString& path)
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? path : path.mid(index + 1);
}

QString dirName(const QString& path)
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? QString() : path.left(index);
}

int pathDepth(const QString& path)
{
    return path.count(QLatin1Char('/')) + 1;
}

bool isUnder(const QString& filePath, const QString& rootPath)
{
    if (rootPath.isEmpty())
        return true;
    return filePath == rootPath || filePath.startsWith(rootPath + QLatin1Char('/'));
}

/// 文件名/路径里出现这些词，基本可判定为材质包
bool hasResourceNameHint(const QString& path)
{
    static const QRegularExpression hint(
        QStringLiteral("resourcepack|resource[_-]?pack|texture|材质|资源包|贴图|rp[_-]"),
        QRegularExpression::CaseInsensitiveOption);
    return hint.match(path).hasMatch();
}

/// 编辑器残留目录，导入时应剔除
bool isJunkPath(const QString& path)
{
    static const QRegularExpression junk(
        QStringLiteral("(^|/)(##mcedit\\.temp##|\\.mcedit|##mcreator|\\.cache|__macosx)(/|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return junk.match(path).hasMatch();
}

bool isModJar(const QString& path)
{
    static const QRegularExpression mod(QStringLiteral("(^|/)mods/[^/]+\\.jar$"), QRegularExpression::CaseInsensitiveOption);
    return mod.match(path).hasMatch();
}

/// 结构特征索引：只保留相对世界根的路径，统一小写
struct StructureIndex {
    QSet<QString> dirs;
    QStringList files;
};

StructureIndex indexFor(const QList<Entry>& entries, const QString& rootPath)
{
    StructureIndex index;
    const QString prefix = rootPath.isEmpty() ? QString() : rootPath + QLatin1Char('/');

    const auto addDir = [&index](const QString& path) {
        QString current = path;
        while (!current.isEmpty() && !index.dirs.contains(current)) {
            index.dirs.insert(current);
            current = dirName(current);
        }
    };

    for (const auto& entry : entries) {
        if (!isUnder(entry.path, rootPath))
            continue;
        QString relative = entry.path.mid(prefix.size()).toLower();
        if (relative.isEmpty())
            continue;
        if (relative.endsWith(QLatin1Char('/')))
            relative.chop(1);
        if (relative.isEmpty())
            continue;

        if (entry.isDir) {
            addDir(relative);
        } else {
            index.files.append(relative);
            addDir(dirName(relative));
        }
    }
    return index;
}

MapLayoutFacts readStructure(const QList<Entry>& entries, const QString& rootPath)
{
    const auto index = indexFor(entries, rootPath);
    MapLayoutFacts facts;

    QSet<QString> dimensionNames;
    for (const auto& dir : index.dirs) {
        const auto parts = dir.split(QLatin1Char('/'));
        if (parts.size() >= 3 && parts[0] == QLatin1String("dimensions"))
            dimensionNames.insert(parts[2]);
    }

    const bool hasPlayersProfiles =
        index.dirs.contains(QStringLiteral("players/advancements")) || index.dirs.contains(QStringLiteral("players/data")) ||
        index.dirs.contains(QStringLiteral("players/stats"));
    facts.hasPlayerProfilesDir = hasPlayersProfiles;

    // 旧布局看 <world>/entities；新布局实体数据下沉到维度目录下
    bool hasEntities = index.dirs.contains(QStringLiteral("entities"));
    bool hasDimensionRegion = false;
    for (const auto& dimension : dimensionNames) {
        const QString dimensionPrefix = QStringLiteral("dimensions/minecraft/%1/").arg(dimension);
        if (index.dirs.contains(dimensionPrefix + QStringLiteral("entities")))
            hasEntities = true;
        if (index.dirs.contains(dimensionPrefix + QStringLiteral("region")))
            hasDimensionRegion = true;
        if (!hasDimensionRegion) {
            for (const auto& file : index.files) {
                if (file.startsWith(dimensionPrefix + QStringLiteral("region/")) && file.endsWith(QLatin1String(".mca"))) {
                    hasDimensionRegion = true;
                    break;
                }
            }
        }
    }

    facts.hasEntitiesDir = hasEntities;
    facts.hasAdvancementsDir =
        index.dirs.contains(QStringLiteral("advancements")) || index.dirs.contains(QStringLiteral("players/advancements"));
    facts.hasDatapacksDir = index.dirs.contains(QStringLiteral("datapacks"));

    // 旧布局的玩家数据是文件（1.13 起改到 playerdata/），新布局是子目录 —— 同名反义，必须区分
    if (!hasPlayersProfiles) {
        for (const auto& file : index.files) {
            if (file.startsWith(QLatin1String("players/")) && file.endsWith(QLatin1String(".dat")) &&
                !file.mid(8).contains(QLatin1Char('/'))) {
                facts.hasPlayersDir = true;
                break;
            }
        }
    }

    facts.hasDimensionsDir = hasDimensionRegion;
    return facts;
}

/// 嵌套 zip 是否为材质包：解出条目 -> 检查 pack.mcmeta
std::optional<bool> verifyResourcePack(const QString& archivePath, const QString& entryPath)
{
    MMCZip::ArchiveReader reader(archivePath);
    auto data = reader.readFile(entryPath);
    if (!data)
        return std::nullopt;

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return std::nullopt;

    const QString target = tempDir.filePath(baseName(entryPath));
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly))
        return std::nullopt;
    if (out.write(*data) != static_cast<qint64>(data->size()))
        return std::nullopt;
    out.close();

    MMCZip::ArchiveReader inner(target);
    QStringList names;
    const auto parsed = inner.parse([&names](MMCZip::ArchiveReader::File* file) -> Result<bool> {
        if (file->isFile()) {
            const QString name = file->filename();
            if (name.compare(QLatin1String("pack.mcmeta"), Qt::CaseInsensitive) == 0 ||
                name.endsWith(QLatin1String("/pack.mcmeta"), Qt::CaseInsensitive)) {
                names.append(name);
                return true;
            }
        }
        return false;
    });
    if (!parsed)
        return std::nullopt;
    return !names.isEmpty();
}

}  // namespace

QStringList MapScanResult::resourcePacks() const
{
    QStringList out;
    for (const auto& resource : resources) {
        if (resource.kind == ScannedResource::Kind::ResourcePack)
            out.append(resource.entryPath);
    }
    return out;
}

QStringList MapScanResult::bundledMods() const
{
    QStringList out;
    for (const auto& resource : resources) {
        if (resource.kind == ScannedResource::Kind::BundledMod)
            out.append(resource.entryPath);
    }
    return out;
}

QList<MapTextHint> MapScanResult::readmeHints() const
{
    QList<MapTextHint> out;
    for (const auto& resource : resources) {
        if (resource.kind == ScannedResource::Kind::Readme)
            out.append({ resource.entryPath, resource.readmeText });
    }
    return out;
}

Result<MapScanResult> MapArchiveScanner::scan(const QString& archivePath, const Options& options)
{
    QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists() || !archiveInfo.isFile())
        return std::unexpected{ QStringLiteral("压缩包不存在：%1").arg(archivePath) };

    MapScanResult result;
    result.archivePath = archiveInfo.absoluteFilePath();
    result.archiveBytes = archiveInfo.size();

    const QString archiveBase = archiveInfo.fileName();
    const QString archiveStem = archiveInfo.completeBaseName();

    // ── 单趟遍历：只记名字与大小，顺手读 level.dat 与浅层文本 ──────────
    QList<Entry> entries;
    QHash<QString, QByteArray> fileBytes;    // 小写路径 -> 已读入的字节
    QStringList readmeCandidates;            // 小写路径

    MMCZip::ArchiveReader reader(result.archivePath);
    const auto parsed = reader.parse([&](MMCZip::ArchiveReader::File* file) -> Result<bool> {
        Entry entry;
        entry.path = file->filename();
        // 有些打包工具会给每条加 "./" 前缀（zip -r . / 7-Zip 的某些设置）。
        // 不剥掉的话世界根会变成 "."，界面显示一坨点，解压前缀也对不上。
        while (entry.path.startsWith(QLatin1String("./")))
            entry.path.remove(0, 2);
        entry.isDir = !file->isFile();
        entry.size = file->size();
        if (entry.path.isEmpty())
            return false;

        const QString name = baseName(entry.path);
        const QString lower = name.toLower();
        const QString lowerPath = entry.path.toLower();

        if (!entry.isDir && lower == QLatin1String("level.dat")) {
            auto data = file->readAll();
            if (data)
                fileBytes.insert(lowerPath, *data);
            entries.append(entry);
            return false;
        }

        // 说明文件候选：浅层（<=2 段）+ 文本扩展名或无扩展名的小文件
        if (!entry.isDir && entry.size > 0 && entry.size <= options.readmeMaxBytes && pathDepth(entry.path) <= 2 &&
            readmeCandidates.size() < options.maxReadmeFiles) {
            static const QRegularExpression textExt(QStringLiteral("\\.(txt|md|html?|nfo|docx?)$"));
            const bool hasExtension = QRegularExpression(QStringLiteral("\\.[a-z0-9]{1,6}$"),
                                                         QRegularExpression::CaseInsensitiveOption)
                                          .match(lower)
                                          .hasMatch();
            if ((hasExtension && textExt.match(lower).hasMatch()) || (!hasExtension && entry.size > 64)) {
                auto data = file->readAll();
                if (data && data->size() <= options.readmeMaxBytes) {
                    // 先暂存，分类阶段再决定是否保留
                    readmeCandidates.append(lowerPath);
                    fileBytes.insert(lowerPath, *data);
                }
            }
        }

        entries.append(entry);
        return false;
    });

    if (!parsed)
        return std::unexpected{ QStringLiteral("无法打开压缩包 %1：%2").arg(archiveBase, parsed.error()) };

    // ── 定位世界候选 ──────────────────────────────────────────────
    QStringList roots;
    for (const auto& entry : entries) {
        if (entry.isDir)
            continue;
        if (baseName(entry.path).compare(QLatin1String("level.dat"), Qt::CaseInsensitive) != 0)
            continue;
        const QString root = dirName(entry.path);
        if (!roots.contains(root))
            roots.append(root);
    }

    if (roots.isEmpty()) {
        return std::unexpected{ QStringLiteral("压缩包内没有找到 level.dat，这不是一个 Minecraft 存档包（共扫描 %1 个条目）")
                                    .arg(entries.size()) };
    }

    // 嵌套世界（作者常把备份一起打包）
    for (const auto& a : roots) {
        for (const auto& b : roots) {
            if (a == b || a.isEmpty() || b.isEmpty())
                continue;
            if (b.startsWith(a + QLatin1Char('/'))) {
                result.warnings.append(
                    QStringLiteral("存档目录嵌套：%1 位于 %2 内部，可能含备份，请留意选择。").arg(b, a));
            }
        }
    }

    QStringList junkPaths;
    for (const auto& entry : entries) {
        if (entry.isDir && isJunkPath(entry.path))
            junkPaths.append(entry.path);
    }
    if (!junkPaths.isEmpty())
        result.ignoredPaths = junkPaths;

    bool hasBundledMods = false;
    for (const auto& entry : entries) {
        if (!entry.isDir && isModJar(entry.path)) {
            hasBundledMods = true;
            break;
        }
    }

    // ── 每个世界：结构 + level.dat + 版本判定 ─────────────────────
    for (const auto& root : roots) {
        const QString levelDatPath = root.isEmpty() ? QStringLiteral("level.dat") : root + QStringLiteral("/level.dat");

        MapWorldCandidate world;
        world.rootPath = root;
        world.levelDatPath = levelDatPath;
        world.folderLabel = root.isEmpty() ? archiveStem : baseName(root);

        for (const auto& entry : entries) {
            if (entry.isDir || !isUnder(entry.path, root))
                continue;
            // 属于更深层世界的文件不计入本世界
            bool ownedByDeeper = false;
            for (const auto& other : roots) {
                if (other != root && other.size() > root.size() && isUnder(entry.path, other)) {
                    ownedByDeeper = true;
                    break;
                }
            }
            if (ownedByDeeper)
                continue;
            world.sizeBytes += entry.size;
            ++world.fileCount;
        }

        const QString iconPath = root.isEmpty() ? QStringLiteral("icon.png") : root + QStringLiteral("/icon.png");
        for (const auto& entry : entries) {
            if (!entry.isDir && entry.path.compare(iconPath, Qt::CaseInsensitive) == 0) {
                world.iconPath = entry.path;
                break;
            }
        }

        world.layout = readStructure(entries, root);

        const auto bytes = fileBytes.value(levelDatPath.toLower());
        if (bytes.isEmpty()) {
            const QString reason = QStringLiteral("level.dat 读取失败");
            result.warnings.append(QStringLiteral("世界 %1 的 %2").arg(root.isEmpty() ? QStringLiteral("<压缩包根>") : root, reason));
            world.version = MapVersionDetector::unknownVersion(reason);
        } else {
            QString error;
            const auto facts = LevelDatReader::fromBytes(bytes, &error);
            if (!facts) {
                const QString reason = QStringLiteral("level.dat 解析失败：%1").arg(error);
                result.warnings.append(
                    QStringLiteral("世界 %1 的 %2").arg(root.isEmpty() ? QStringLiteral("<压缩包根>") : root, reason));
                world.version = MapVersionDetector::unknownVersion(reason);
            } else {
                world.level = *facts;
                MapDetectInput input;
                input.level = *facts;
                input.layout = world.layout;
                input.archiveName = archiveBase;
                input.worldFolderLabel = world.folderLabel;
                input.hasBundledMods = hasBundledMods;
                world.version = MapVersionDetector::detect(input);
            }
        }

        result.worlds.append(world);
    }

    // ── 附属资源嗅探 ──────────────────────────────────────────────
    QSet<QString> worldRoots;
    for (const auto& root : roots)
        worldRoots.insert(root);

    for (const auto& entry : entries) {
        if (entry.isDir || isJunkPath(entry.path))
            continue;

        const QString lower = entry.path.toLower();
        const QString name = baseName(lower);

        if (name == QLatin1String("resources.zip")) {
            result.resources.append({ ScannedResource::Kind::WorldResourcesZip, entry.path, entry.size,
                                      QStringLiteral("存档内置材质包（resources.zip）") });
            continue;
        }

        if (lower.endsWith(QLatin1String(".zip"))) {
            QString reason;
            if (hasResourceNameHint(lower)) {
                reason = QStringLiteral("文件名含材质包关键词");
            } else {
                const QString parent = dirName(entry.path);
                for (const auto& root : worldRoots) {
                    if (!root.isEmpty() && dirName(root) == parent) {
                        reason = QStringLiteral("与世界存档同级");
                        break;
                    }
                }
                if (reason.isEmpty() && parent.isEmpty())
                    reason = QStringLiteral("位于压缩包根目录");
            }
            if (!reason.isEmpty()) {
                result.resources.append({ ScannedResource::Kind::ResourcePack, entry.path, entry.size, reason });
                continue;
            }
        }

        if (lower.endsWith(QLatin1String(".jar")) && isModJar(entry.path)) {
            result.resources.append(
                { ScannedResource::Kind::BundledMod, entry.path, entry.size, QStringLiteral("位于 mods 目录") });
            continue;
        }

        if (readmeCandidates.contains(entry.path.toLower())) {
            const auto data = fileBytes.value(entry.path.toLower());
            if (!text::looksLikeText(data))
                continue;
            ScannedResource resource;
            resource.kind = ScannedResource::Kind::Readme;
            resource.entryPath = entry.path;
            resource.size = entry.size;
            resource.reason = QStringLiteral("浅层说明文件");
            resource.readmeText = text::decodeText(data);
            result.resources.append(resource);
        }
    }

    // ── 深度校验材质包（根目录普通命名的 zip 只能靠 pack.mcmeta 认）──
    if (options.deepResourceCheck) {
        int checked = 0;
        for (int i = 0; i < result.resources.size() && checked < options.maxDeepChecks; ++i) {
            auto& resource = result.resources[i];
            if (resource.kind != ScannedResource::Kind::ResourcePack || resource.size > options.maxDeepCheckSize)
                continue;
            ++checked;
            if (verifyResourcePack(result.archivePath, resource.entryPath) == false) {
                result.resources.removeAt(i);
                --i;
            }
        }
    }

    // ── L1 未命中的世界用旁证重跑一次 ────────────────────────────
    const auto hints = result.readmeHints();
    if (!hints.isEmpty()) {
        for (auto& world : result.worlds) {
            if (world.version.isKnown() || !world.level.valid)
                continue;
            MapDetectInput input;
            input.level = world.level;
            input.layout = world.layout;
            input.textHints = hints;
            input.archiveName = archiveBase;
            input.worldFolderLabel = world.folderLabel;
            input.hasBundledMods = hasBundledMods;
            world.version = MapVersionDetector::detect(input);
        }
    }

    // ── 模组环境提示（只识别，不处理依赖安装）────────────────────
    for (const auto& world : result.worlds) {
        if (!world.level.isModded)
            continue;
        const QString brandText =
            world.level.serverBrands.isEmpty() ? QStringLiteral("模组加载器") : world.level.serverBrands.join(QLatin1Char('/'));
        result.warnings.append(QStringLiteral("该存档由 %1 环境保存过，但未记录具体模组清单。若地图使用了模组内容，"
                                              "需要用相同的模组环境启动，否则可能丢失方块与实体。")
                                   .arg(brandText));
        break;
    }

    std::sort(result.worlds.begin(), result.worlds.end(),
              [](const MapWorldCandidate& a, const MapWorldCandidate& b) { return a.sizeBytes > b.sizeBytes; });

    return result;
}

}  // namespace maps
