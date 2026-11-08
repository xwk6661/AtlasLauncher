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
#include "MapImportTask.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThreadPool>
#include <QtConcurrent>

#include "FileSystem.h"
#include "MMCZip.h"
#include "DataVersionTable.h"
#include "MapMeta.h"
#include "MapRepository.h"
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

namespace maps {
namespace {

QString componentIdForLoader(const QString& loader)
{
    if (loader == QLatin1String("forge"))
        return QStringLiteral("net.minecraftforge");
    if (loader == QLatin1String("neoforge"))
        return QStringLiteral("net.neoforged");
    if (loader == QLatin1String("fabric"))
        return QStringLiteral("net.fabricmc.fabric-loader");
    if (loader == QLatin1String("quilt"))
        return QStringLiteral("org.quiltmc.quilt-loader");
    return {};
}

QString baseNameOf(const QString& path)
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? path : path.mid(index + 1);
}

}  // namespace

MapImportTask::MapImportTask(QString archivePath, MapScanResult scan, MapImportOptions options)
    : m_archivePath(std::move(archivePath)), m_scan(std::move(scan)), m_options(std::move(options))
{
    setName(m_options.displayName);
}

QString MapImportTask::instanceRoot() const
{
    return m_stagingPath;
}

QString MapImportTask::minecraftRoot() const
{
    return FS::PathCombine(m_stagingPath, ".minecraft");
}

QString MapImportTask::worldFolderFor(int index) const
{
    // 目录名固定英文：quickPlay 与 Agent 都吃目录名字符串，老版本对中文路径有坑
    return index == 0 ? QStringLiteral("MapWorld") : QStringLiteral("MapWorld_%1").arg(index + 1);
}

QList<int> MapImportTask::worldOrder() const
{
    const int primaryIndex =
        (m_options.worldIndex >= 0 && m_options.worldIndex < m_scan.worlds.size()) ? m_options.worldIndex : 0;

    // 主世界排第一，其余按原顺序跟在后面（一包多世界的 PvP 地图很常见）
    QList<int> order{ primaryIndex };
    for (int i = 0; i < m_scan.worlds.size(); ++i) {
        if (i != primaryIndex)
            order.append(i);
    }
    return order;
}

void MapImportTask::executeTask()
{
    if (m_scan.worlds.isEmpty()) {
        emitFailed(tr("这个压缩包里没有可用的世界。"));
        return;
    }

    const int index =
        (m_options.worldIndex >= 0 && m_options.worldIndex < m_scan.worlds.size()) ? m_options.worldIndex : 0;
    const auto& primary = m_scan.worlds.at(index);

    m_mcVersion = !m_options.forcedVersion.isEmpty() ? m_options.forcedVersion : primary.version.mcVersion;
    if (!MapVersionDetector::isLaunchableVersion(m_mcVersion)) {
        emitFailed(tr("无法确定游戏版本，请先手动指定版本再导入。"));
        return;
    }

    m_loader = !m_options.forcedLoader.isEmpty() ? m_options.forcedLoader : primary.version.recommendedLoader;
    m_loaderVersion = m_options.loaderVersion;
    if (m_loader != QLatin1String("vanilla") && m_loaderVersion.isEmpty()) {
        // 判出来是模组环境，但不知道具体加载器版本 —— 退回原版，并把这件事写进证据链
        m_loaderFallbackNote = tr("判定为 %1 存档，但没有指定加载器版本，已按原版导入"
                                  "（模组方块会被原版客户端删掉，请到「版本」页补装加载器）。")
                                   .arg(m_loader);
        m_loader = QStringLiteral("vanilla");
    }

    setAbortable(true);
    setAbortButtonText(tr("取消"));
    setProgress(0, 5);

    setStatus(tr("正在创建实例…"));
    if (const auto result = createInstance(); !result) {
        emitFailed(result.error());
        return;
    }

    m_extractWatcher.disconnect();
    connect(&m_extractWatcher, &QFutureWatcher<Result<>>::finished, this, &MapImportTask::onExtractionFinished);

    // 将重度解压与文件写入流程放入全局线程池，解放 UI 线程，彻底消除界面卡顿并支持随时取消
    auto future = QtConcurrent::run(QThreadPool::globalInstance(), [this]() -> Result<> {
        if (m_abortRequested.load())
            return std::unexpected{ tr("导入已被取消") };

        QMetaObject::invokeMethod(this, [this]() {
            setProgress(1, 5);
            setStatus(tr("正在解压存档…"));
        }, Qt::QueuedConnection);

        if (const auto result = extractWorlds(); !result)
            return result;

        if (m_abortRequested.load())
            return std::unexpected{ tr("导入已被取消") };

        QMetaObject::invokeMethod(this, [this]() {
            setProgress(2, 5);
            setStatus(tr("正在部署随包资源…"));
        }, Qt::QueuedConnection);

        if (const auto result = deployResources(); !result)
            return result;

        if (m_abortRequested.load())
            return std::unexpected{ tr("导入已被取消") };

        QMetaObject::invokeMethod(this, [this]() {
            setProgress(3, 5);
            setStatus(tr("正在写入地图元数据…"));
        }, Qt::QueuedConnection);

        if (const auto result = writeMetadata(); !result)
            return result;

        return {};
    });

    m_extractWatcher.setFuture(future);
}

bool MapImportTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;

    if (m_gameFilesTask) {
        return InstanceTask::abort();
    }

    m_abortRequested.store(true);
    return true;
}

void MapImportTask::onExtractionFinished()
{
    if (m_abortRequested.load()) {
        for (const auto& dir : m_extractedWorldDirs)
            FS::deletePath(dir);
        emitAborted();
        return;
    }

    const auto result = m_extractWatcher.result();
    if (!result) {
        for (const auto& dir : m_extractedWorldDirs)
            FS::deletePath(dir);
        emitFailed(result.error());
        return;
    }

    setProgress(4, 5);
    // downloadFiles 内部会发 emitSucceeded（不需要下载时立刻发，需要时下载完再发），
    // 这里不能再补一次，否则 InstanceStaging 会收到两遍成功信号。
    setStatus(tr("正在下载游戏文件…"));
    downloadFiles(m_instance.get());
}

Result<> MapImportTask::createInstance()
{
    if (m_stagingPath.isEmpty())
        return std::unexpected{ tr("没有可用的 staging 目录，实例任务未正确初始化。") };

    if (!FS::ensureFolderPathExists(m_stagingPath))
        return std::unexpected{ tr("无法创建实例目录：%1").arg(m_stagingPath) };

    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")),
        m_stagingPath);

    const SettingsObject::Lock lock(m_instance->settings());

    auto* components = m_instance->getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", m_mcVersion, true);

    const QString componentId = componentIdForLoader(m_loader);
    if (!componentId.isEmpty() && !m_loaderVersion.isEmpty())
        components->setComponentVersion(componentId, m_loaderVersion);

    m_instance->setName(name());
    if (!m_instIcon.isEmpty())
        m_instance->setIconKey(m_instIcon);

    // 1.20+ 把「启动即进图」写进实例设置。注意这只是给 Prism 的设置页回填 +
    // 「创建快捷方式」用的：真正启动时由 MainWindow 传 MinecraftTarget（见 MapLaunchPrep）。
    if (DataVersionTable::supportsQuickPlay(m_mcVersion))
        m_instance->settings()->set("JoinWorldOnLaunch", worldFolderFor(0));

    components->saveNow();
    return {};
}

Result<> MapImportTask::extractWorlds()
{
    const QString savesDir = FS::PathCombine(minecraftRoot(), "saves");
    if (!FS::ensureFolderPathExists(savesDir))
        return std::unexpected{ tr("无法创建 saves 目录：%1").arg(savesDir) };

    MMCZip::ArchiveReader reader(m_archivePath);
    int folderIndex = 0;
    for (const int worldIndex : worldOrder()) {
        const auto& world = m_scan.worlds.at(worldIndex);
        const QString folder = worldFolderFor(folderIndex);
        const QString finalDir = FS::PathCombine(savesDir, folder);
        const QString tempDir = FS::PathCombine(savesDir, folder + QStringLiteral(".tmp-world"));

        // 先解到 .tmp-world，校验通过再改名：中途失败不会留下半个存档
        FS::deletePath(tempDir);
        if (!FS::ensureFolderPathExists(tempDir))
            return std::unexpected{ tr("无法创建临时目录：%1").arg(tempDir) };

        const QString subDir = world.rootPath.isEmpty() ? QString() : world.rootPath + QLatin1Char('/');
        // 解压在后台线程池执行；progress 回调检测取消并向主线程发送进度信号
        const auto progress = [this](qint64 done, qint64 total) {
            if (m_abortRequested.load()) {
                return false;
            }
            QMetaObject::invokeMethod(this, [this, done, total]() {
                setStatus(tr("正在解压存档… %1 / %2").arg(done).arg(total));
                setProgress(done, total);
            }, Qt::QueuedConnection);
            return true;
        };

        if (const auto extracted = MMCZip::extractSubDir(&reader, subDir, tempDir, progress); !extracted) {
            FS::deletePath(tempDir);
            if (m_abortRequested.load()) {
                return std::unexpected{ tr("解压已被取消") };
            }
            return std::unexpected{ tr("解压世界 %1 失败：%2").arg(world.folderLabel, extracted.error()) };
        }

        if (!QFile::exists(FS::PathCombine(tempDir, "level.dat"))) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("解压后没有找到 level.dat，压缩包可能已损坏：%1").arg(world.folderLabel) };
        }

        FS::deletePath(finalDir);
        if (!FS::move(tempDir, finalDir)) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("无法把解压结果移动到 %1").arg(finalDir) };
        }

        m_extractedWorldDirs.append(finalDir);
        if (folderIndex == 0)
            m_primaryWorldFolder = folder;
        ++folderIndex;
    }

    return {};
}

Result<> MapImportTask::extractEntry(const QString& entryPath, const QString& targetPath)
{
    MMCZip::ArchiveReader reader(m_archivePath);
    const auto data = reader.readFile(entryPath);
    if (!data)
        return std::unexpected{ tr("无法读取压缩包内的 %1：%2").arg(entryPath, data.error()) };

    if (!FS::ensureFolderPathExists(QFileInfo(targetPath).absolutePath()))
        return std::unexpected{ tr("无法创建目录：%1").arg(QFileInfo(targetPath).absolutePath()) };

    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly))
        return std::unexpected{ tr("无法写入 %1：%2").arg(targetPath, out.errorString()) };
    if (out.write(*data) != data->size())
        return std::unexpected{ tr("写入 %1 时中断").arg(targetPath) };
    return {};
}

Result<> MapImportTask::deployResources()
{
    if (!m_options.deployBundledResources)
        return {};

    for (const auto& resource : m_scan.resources) {
        if (resource.kind != ScannedResource::Kind::ResourcePack)
            continue;
        const QString target =
            FS::PathCombine(minecraftRoot(), "resourcepacks", baseNameOf(resource.entryPath));
        if (const auto result = extractEntry(resource.entryPath, target); !result)
            return result;
    }

    for (const auto& resource : m_scan.resources) {
        if (resource.kind != ScannedResource::Kind::BundledMod)
            continue;
        const QString target = FS::PathCombine(minecraftRoot(), "mods", baseNameOf(resource.entryPath));
        if (const auto result = extractEntry(resource.entryPath, target); !result)
            return result;
    }

    // 世界内置的 resources.zip 已经随世界一起解开，不需要额外处理
    return mergeOptionsTxt();
}

Result<> MapImportTask::mergeOptionsTxt()
{
    const auto packs = m_scan.resourcePacks();
    if (packs.isEmpty())
        return {};

    const QString path = FS::PathCombine(minecraftRoot(), "options.txt");
    QStringList lines;
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(existing.readAll());
            lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
    }

    // 只改 resourcePacks 这一行，玩家的按键/灵敏度/语言等设置原样保留
    QStringList wanted{ QStringLiteral("vanilla") };
    for (const auto& pack : packs)
        wanted.append(QStringLiteral("file/%1").arg(baseNameOf(pack)));
    const QString value = QStringLiteral("resourcePacks:[\"%1\"]").arg(wanted.join(QStringLiteral("\",\"")));

    bool replaced = false;
    for (auto& line : lines) {
        if (line.startsWith(QLatin1String("resourcePacks:"))) {
            line = value;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        lines.append(value);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return std::unexpected{ tr("无法写入 options.txt：%1").arg(out.errorString()) };
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    return {};
}

Result<> MapImportTask::writeMetadata()
{
    const int primaryIndex =
        (m_options.worldIndex >= 0 && m_options.worldIndex < m_scan.worlds.size()) ? m_options.worldIndex : 0;
    const auto& primary = m_scan.worlds.at(primaryIndex);

    MapMeta meta;
    meta.title = name();
    meta.archive.path = m_scan.archivePath;
    meta.archive.size = m_scan.archiveBytes;
    meta.version.mcVersion = m_mcVersion;
    meta.version.confidence = primary.version.confidence;
    meta.version.decidedBy = m_options.forcedVersion.isEmpty() ? QStringLiteral("detector") : QStringLiteral("user");

    for (const auto& item : primary.version.evidence)
        meta.version.evidence.append(QStringLiteral("%1：%2").arg(item.kind, item.detail));
    meta.version.evidence.append(primary.version.warnings);
    if (!m_loaderFallbackNote.isEmpty())
        meta.version.evidence.append(m_loaderFallbackNote);

    int folderIndex = 0;
    for (const int worldIndex : worldOrder()) {
        MapWorldRecord record;
        record.folder = worldFolderFor(folderIndex);
        record.sourceRoot = m_scan.worlds.at(worldIndex).rootPath;
        meta.worlds.append(record);
        ++folderIndex;
    }

    meta.loader.type = m_loader;
    meta.loader.version = m_loaderVersion;
    meta.packs = m_scan.resourcePacks();
    meta.mods = m_scan.bundledMods();
    meta.experimentalFeatures = primary.level.enabledFeatures;
    meta.requiredMods = primary.level.requiredMods;
    // 只有 FML LoadingModList（= requiredMods）才算"这张图依赖模组"。
    // ServerBrands / WasModded 只说明作者用自己的整合包打开过，原版照样能开。
    meta.requiresExternalModpack = !primary.level.requiredMods.isEmpty() && m_scan.bundledMods().isEmpty();
    meta.importedAt = QDateTime::currentDateTimeUtc();

    // 把压缩包归入地图库：原路径可能是下载目录 / U 盘，删掉就再也装不回来了
    const QString libraryCopy = MapRepository::importIntoLibrary(m_scan.archivePath);
    if (!libraryCopy.isEmpty())
        meta.archive.libraryCopy = libraryCopy;

    QString error;
    if (!meta.save(instanceRoot(), &error))
        return std::unexpected{ error };

    // 封面：世界自带的 icon.png 直接提升为实例图标
    if (!primary.iconPath.isEmpty()) {
        const QString target = FS::PathCombine(instanceRoot(), "icon.png");
        if (const auto result = extractEntry(primary.iconPath, target); !result) {
            qWarning() << "Map import: cover extraction failed:" << result.error();
        }
    }

    return {};
}

Result<> MapImportTask::reinstallWorlds(const QString& instanceRoot, const MapMeta& meta,
                                        std::function<bool(qint64, qint64)> progress)
{
    if (meta.worlds.isEmpty())
        return std::unexpected{ tr("map.json 里没有记录世界目录，无法重装。") };

    const QString archivePath = MapRepository::resolveArchivePath(meta);
    if (archivePath.isEmpty())
        return std::unexpected{ tr("找不到来源压缩包（库目录和 map.json 记录的位置都没有）。请重新导入这张地图。") };

    const QString savesDir = FS::PathCombine(instanceRoot, ".minecraft", "saves");
    if (!FS::ensureFolderPathExists(savesDir))
        return std::unexpected{ tr("无法创建 saves 目录：%1").arg(savesDir) };

    // 旧存档整体挪走，不删：玩家可能在图里建过东西
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupRoot = FS::PathCombine(savesDir, QStringLiteral(".mcml-backup"), stamp);

    MMCZip::ArchiveReader reader(archivePath);

    for (const auto& world : meta.worlds) {
        if (world.folder.isEmpty())
            continue;

        const QString finalDir = FS::PathCombine(savesDir, world.folder);
        const QString tempDir = FS::PathCombine(savesDir, world.folder + QStringLiteral(".tmp-world"));

        if (QFile::exists(FS::PathCombine(finalDir, "level.dat"))) {
            if (!FS::ensureFolderPathExists(backupRoot))
                return std::unexpected{ tr("无法创建备份目录：%1").arg(backupRoot) };
            const QString backupDir = FS::PathCombine(backupRoot, world.folder);
            FS::deletePath(backupDir);
            if (!FS::move(finalDir, backupDir))
                return std::unexpected{ tr("无法把旧存档移动到备份目录：%1").arg(backupDir) };
        } else {
            FS::deletePath(finalDir);
        }

        FS::deletePath(tempDir);
        if (!FS::ensureFolderPathExists(tempDir))
            return std::unexpected{ tr("无法创建临时目录：%1").arg(tempDir) };

        const QString subDir = world.sourceRoot.isEmpty() ? QString() : world.sourceRoot + QLatin1Char('/');
        const auto keepAlive = [&progress](qint64 done, qint64 total) {
            if (progress) {
                return progress(done, total);
            }
            return true;
        };
        if (const auto extracted = MMCZip::extractSubDir(&reader, subDir, tempDir, keepAlive); !extracted) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("重装时解压失败：%1").arg(extracted.error()) };
        }
        if (!QFile::exists(FS::PathCombine(tempDir, "level.dat"))) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("重装后没有找到 level.dat，压缩包可能已损坏：%1").arg(world.folder) };
        }
        if (!FS::move(tempDir, finalDir)) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("无法把解压结果移动到 %1").arg(finalDir) };
        }
    }

    return {};
}

// ----------------------------------------------------------------------------
// MapReinstallTask
// ----------------------------------------------------------------------------

MapReinstallTask::MapReinstallTask(QString instanceRoot, MapMeta meta)
    : m_instanceRoot(std::move(instanceRoot)), m_meta(std::move(meta))
{
}

void MapReinstallTask::executeTask()
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    setStatus(tr("正在重装地图存档…"));

    connect(&m_watcher, &QFutureWatcher<Result<>>::finished, this, &MapReinstallTask::onReinstallFinished);

    auto future = QtConcurrent::run(QThreadPool::globalInstance(), [this]() -> Result<> {
        return MapImportTask::reinstallWorlds(m_instanceRoot, m_meta, [this](qint64 done, qint64 total) {
            if (m_abortRequested.load()) {
                return false;
            }
            QMetaObject::invokeMethod(this, [this, done, total]() {
                setStatus(tr("正在解压存档… %1 / %2").arg(done).arg(total));
                setProgress(done, total);
            }, Qt::QueuedConnection);
            return true;
        });
    });

    m_watcher.setFuture(future);
}

bool MapReinstallTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_abortRequested.store(true);
    return true;
}

void MapReinstallTask::onReinstallFinished()
{
    if (m_abortRequested.load()) {
        emitAborted();
        return;
    }
    const auto result = m_watcher.result();
    if (!result) {
        emitFailed(result.error());
    } else {
        emitSucceeded();
    }
}

}  // namespace maps
