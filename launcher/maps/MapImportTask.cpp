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
#include "Application.h"
#include "DataVersionTable.h"
#include "HallCache.h"
#include "JavaRecommend.h"
#include "MapHallLink.h"
#include "MapLibrary.h"
#include "MapMeta.h"
#include "MapOptionsTxt.h"
#include "MapRepository.h"
#include "MapTextUtils.h"
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"

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

    // 构造在 UI 线程：设置对象的读取放在这里，别带到线程池里去（见头文件里的说明）
    if (m_options.hasGameOptions) {
        m_gameOptions = m_options.gameOptions;
    } else if (const auto* app = APPLICATION) {
        m_gameOptions = options::defaultsFromSettings(app->settings());
    }
}

/**
 * 线程池里的那个 lambda 捕获的是**裸 this**（`QtConcurrent::run(..., [this]{...})`），
 * 而 `abort()` 只置一个原子标志、不会等人。异步路径上如果任务先被销毁，
 * 工作线程接下来碰的每一个成员都是悬空的 —— 这里必须先置取消位、再等它收工，
 * 才能让成员按正常顺序析构。
 *
 * 等待发生在析构体里（早于成员析构、早于 ~QObject），所以工作线程投回来的
 * `invokeMethod(... Qt::QueuedConnection)` 也只会在对象还活着时被投递/丢弃，不会打到半死的对象上。
 */
MapImportTask::~MapImportTask()
{
    m_abortRequested.store(true);
    if (m_extractWatcher.isRunning())
        m_extractWatcher.waitForFinished();
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
    // 共享状态（库索引 / 大厅关联）回 UI 线程再写：工作线程只算了结果，
    // 直接写会和本地库面板、实例卡片抢同一份文件（审查 P0-3）
    registerInLibrary();
    writeHallLink();

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

    // 导入时就把 Java 定下来：这一步只依赖「MC 版本 → 需要哪个 Java」与本地已装的运行时，
    // 不碰网络。玩家没手选过（新实例本来就没选过），所以切换是安全的；
    // 关掉 MapAutoJava 时这里什么都不做，留给 Prism 既有的自动 Java 流程。
    if (const auto java = autoSelectForInstance(m_instance.get(), m_mcVersion); java.applied)
        qInfo() << "Map java: 导入时为新实例选用" << java.path << "—" << java.reason;
    else if (!java.problem.isEmpty())
        qDebug() << "Map java: 导入时未自动选择 —" << java.problem;

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
        // 关闭对话框时析构会在 UI 线程等这个任务收工：每个文件都查一次取消位，
        // 免得"玩家点了取消还要等一整个资源包"（审查 P1-6）
        if (m_abortRequested.load())
            return std::unexpected{ tr("导入已被取消") };
        const QString target =
            FS::PathCombine(minecraftRoot(), "resourcepacks", baseNameOf(resource.entryPath));
        if (const auto result = extractEntry(resource.entryPath, target); !result)
            return result;
    }

    for (const auto& resource : m_scan.resources) {
        if (resource.kind != ScannedResource::Kind::BundledMod)
            continue;
        if (m_abortRequested.load())
            return std::unexpected{ tr("导入已被取消") };
        const QString target = FS::PathCombine(minecraftRoot(), "mods", baseNameOf(resource.entryPath));
        if (const auto result = extractEntry(resource.entryPath, target); !result)
            return result;
    }

    // 世界内置的 resources.zip 已经随世界一起解开，不需要额外处理
    return applyOptionsDefaults();
}

Result<> MapImportTask::applyOptionsDefaults()
{
    const auto packs = m_scan.resourcePacks();

    options::Defaults defaults = m_gameOptions;
    // 首屏无障碍引导会把 quickPlay 挡在外面（2026-11-12 复现），地图实例一律关掉
    defaults.skipAccessibilityOnboarding = true;
    // 全屏：地图本来就是"一张图一个世界"，全屏是它的正常形态。
    // （2026-11-12 曾用它绕过本机「窗口模式一片白」，该问题已于 11-13 解决，
    //   这里不再作为规避手段，只是玩家可关的默认偏好。）
    defaults.fullscreen = true;

    if (packs.isEmpty() && defaults.isEmpty())
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

    bool touched = false;

    // ── 1. 随包材质包：除了这一行，玩家的其余设置原样保留 ──
    if (!packs.isEmpty()) {
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
        touched = true;
    }

    // ── 2. 语言与键位 ──
    // 版本先判一代（≤1.12 的数字键码 / ≥1.13 的 key.keyboard.*）；判不出来就退回
    // 看现有 options.txt 的写法，两者都没有就**一条都不写** —— 写错一代会让
    // 蹲/跑静默失效，比不写更糟。
    if (!defaults.isEmpty()) {
        auto format = options::formatForVersion(m_mcVersion);
        if (!format)
            format = options::detectFormat(lines);
        if (format) {
            // `version:` 必须一起写：没有它，游戏把整份文件当 1.12 之前的老格式去修复，
            // 我们刚写进去的 `key.keyboard.*` 会让 Integer.parseInt 抛异常，整份设置作废
            // （含刚设好的中文），首屏还退回无障碍引导 —— 图就进不去了。
            if (options::putVersionStamp(lines, *format, DataVersionTable::instance().dataVersionFor(m_mcVersion)))
                touched = true;
            if (options::applyDefaults(lines, *format, m_mcVersion, defaults) > 0)
                touched = true;
        }
    }

    if (!touched)
        return {};

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
    meta.variant = m_options.variant;  // 这个实例是原版还是哪个语言的翻译

    // 封面：世界自带的 icon.png 直接提升为实例图标。
    // **必须在归档被移进地图库之前读**：importIntoLibrary() 是"移动"（跨盘才退化成复制 + 删除），
    // 之后再用原路径打开必然失败。这里曾经放在移动之后，于是每张新导入的地图都丢图标，
    // 日志只留一行 `cover extraction failed: … Failed to open archive file <原路径>`（06 §109）。
    if (!primary.iconPath.isEmpty()) {
        const QString target = FS::PathCombine(instanceRoot(), "icon.png");
        if (const auto result = extractEntry(primary.iconPath, target); !result) {
            qWarning() << "Map import: cover extraction failed:" << result.error();
        }
    }

    // 把压缩包归入地图库：原路径可能是下载目录 / U 盘，删掉就再也装不回来了
    const QString libraryCopy = MapRepository::importIntoLibrary(m_scan.archivePath);
    if (!libraryCopy.isEmpty()) {
        meta.archive.libraryCopy = libraryCopy;
        // 归档已经不在原路径上了：之后任何"按 m_archivePath 再打开一次"的读取都改走库副本。
        // 现在这条路径上已经没有别的读取点，留着是为了下次有人加代码时不会再踩同一个坑。
        m_archivePath = libraryCopy;
        // 顺手把这次识别到的版本/名字攒起来，回 UI 线程再写库索引。
        // 索引是进程内共享的（本地库面板也在读），工作线程直接读改写会和它撞车。
        const QString worldName = text::stripFormattingCodes(primary.level.levelName);
        bool hasChinese = false;
        for (const auto ch : worldName) {
            if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) {
                hasChinese = true;
                break;
            }
        }
        m_libraryRegistration.valid = true;
        m_libraryRegistration.archiveCopy = libraryCopy;
        m_libraryRegistration.mcVersion = primary.version.mcVersion;
        m_libraryRegistration.englishName = hasChinese ? QString() : worldName;
        m_libraryRegistration.chineseName = hasChinese ? worldName : QString();
        m_libraryRegistration.hallSlug = m_options.hallSlug;
        m_libraryRegistration.variant = m_options.variant;
    }

    // 大厅关联：标题/UUID 要从 HallCache 查（共享缓存），同样留到 UI 线程写
    m_pendingHallSlug = m_options.hallSlug;

    QString error;
    if (!meta.save(instanceRoot(), &error))
        return std::unexpected{ error };

    return {};
}

/**
 * 把这次导入登记进库索引（**必须在 UI 线程调用**）。
 *
 * `MapLibrary::rememberImported()` 是"读整份索引 → 改一条 → 写回"，而 UI 侧的
 * 本地库面板持有同一份文件的内存副本、改名/分类时也会整份写回。两边并发时后写的
 * 会覆盖前写的 —— 表现是"新导入的包在库里消失"或"刚改的分类被还原"（审查 P0-3）。
 */
void MapImportTask::registerInLibrary()
{
    if (!m_libraryRegistration.valid)
        return;
    m_libraryRegistration.valid = false;
    MapLibrary::rememberImported(m_libraryRegistration.archiveCopy, m_libraryRegistration.mcVersion,
                                 m_libraryRegistration.englishName, m_libraryRegistration.chineseName,
                                 m_libraryRegistration.hallSlug, m_libraryRegistration.variant);
}

/// 写实例自己的 `hall-link.json`（UI 线程）：标题/UUID 来自共享的大厅缓存
void MapImportTask::writeHallLink()
{
    if (m_pendingHallSlug.isEmpty())
        return;

    MapHallLink link;
    link.slug = m_pendingHallSlug;
    const auto row = HallCache::findListRow(m_pendingHallSlug);
    if (!row.isEmpty()) {
        link.mapUuid = row.value(QStringLiteral("map_uuid")).toString();
        link.title = row.value(QStringLiteral("name")).toString();
    }
    link.linkedAt = QDateTime::currentDateTimeUtc();
    QString linkError;
    if (!link.save(instanceRoot(), &linkError))
        qWarning() << "Map import: hall-link save failed:" << linkError;
    m_pendingHallSlug.clear();
}

Result<> MapImportTask::reinstallWorlds(const QString& instanceRoot, const MapMeta& meta,
                                        bool backupExisting,
                                        std::function<bool(qint64, qint64)> progress,
                                        const QString& archiveOverride)
{
    if (meta.worlds.isEmpty())
        return std::unexpected{ tr("map.json 里没有记录世界目录，无法重装。") };

    const QString archivePath = archiveOverride.isEmpty() ? MapRepository::resolveArchivePath(meta) : archiveOverride;
    if (archivePath.isEmpty())
        return std::unexpected{ tr("找不到来源压缩包（库目录和 map.json 记录的位置都没有）。请重新导入这张地图。") };

    const QString savesDir = FS::PathCombine(instanceRoot, ".minecraft", "saves");
    if (!FS::ensureFolderPathExists(savesDir))
        return std::unexpected{ tr("无法创建 saves 目录：%1").arg(savesDir) };

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupRoot = FS::PathCombine(savesDir, QStringLiteral(".mcml-backup"), stamp);

    MMCZip::ArchiveReader reader(archivePath);

    for (const auto& world : meta.worlds) {
        if (world.folder.isEmpty())
            continue;

        const QString finalDir = FS::PathCombine(savesDir, world.folder);
        const QString tempDir = FS::PathCombine(savesDir, world.folder + QStringLiteral(".tmp-world"));

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

        // 解压确认成功后再处置旧存档，保证解压失败时不损坏现有存档
        if (QFile::exists(FS::PathCombine(finalDir, "level.dat"))) {
            if (backupExisting) {
                if (!FS::ensureFolderPathExists(backupRoot)) {
                    FS::deletePath(tempDir);
                    return std::unexpected{ tr("无法创建备份目录：%1").arg(backupRoot) };
                }
                const QString backupDir = FS::PathCombine(backupRoot, world.folder);
                FS::deletePath(backupDir);
                if (!FS::move(finalDir, backupDir)) {
                    FS::deletePath(tempDir);
                    return std::unexpected{ tr("无法把旧存档移动到备份目录：%1").arg(backupDir) };
                }
            } else {
                FS::deletePath(finalDir);
            }
        } else {
            FS::deletePath(finalDir);
        }

        if (!FS::move(tempDir, finalDir)) {
            FS::deletePath(tempDir);
            return std::unexpected{ tr("无法把解压结果移动到 %1").arg(finalDir) };
        }
    }

    // 图标：06 §109 之前装出来的实例都没有 icon.png（导入时归档已被移走），而"重装"是玩家手上
    // 唯一会重新解一次包的动作，所以顺手补一次。**只在缺图标时补** —— 玩家自己换过的图标不覆盖。
    const QString iconTarget = FS::PathCombine(instanceRoot, "icon.png");
    if (!QFile::exists(iconTarget)) {
        for (const auto& world : meta.worlds) {
            // 与 MapArchiveScanner 一致：世界根为空时图标就在归档根上
            const QString entry =
                world.sourceRoot.isEmpty() ? QStringLiteral("icon.png") : world.sourceRoot + QStringLiteral("/icon.png");
            const auto data = reader.readFile(entry);
            if (!data)
                continue;
            QFile out(iconTarget);
            if (out.open(QIODevice::WriteOnly) && out.write(*data) == data->size())
                break;
            FS::deletePath(iconTarget);
        }
    }

    return {};
}

// ----------------------------------------------------------------------------
// MapReinstallTask
// ----------------------------------------------------------------------------

MapReinstallTask::MapReinstallTask(QString instanceRoot, MapMeta meta, bool backupExisting, QString archivePath,
                                   variant::Info variant)
    : m_instanceRoot(std::move(instanceRoot))
    , m_meta(std::move(meta))
    , m_backupExisting(backupExisting)
    , m_archivePath(std::move(archivePath))
    , m_variant(std::move(variant))
{
}

MapReinstallTask::~MapReinstallTask()
{
    // 同上：worker 捕获裸 this，析构必须等它把 abort 位看到并退出来
    m_abortRequested.store(true);
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
}

void MapReinstallTask::executeTask()
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    setStatus(tr("正在重装地图存档…"));

    connect(&m_watcher, &QFutureWatcher<Result<>>::finished, this, &MapReinstallTask::onReinstallFinished);

    auto future = QtConcurrent::run(QThreadPool::globalInstance(), [this]() -> Result<> {
        return MapImportTask::reinstallWorlds(m_instanceRoot, m_meta, m_backupExisting, [this](qint64 done, qint64 total) {
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
    if (result && !m_archivePath.isEmpty()) {
        // 换版本重装：实例现在依附的是另一个库条目，map.json 必须跟着改，
        // 否则下次重装又会回到旧版本（库里的"已安装"也会指错）
        MapMeta updated = MapMeta::load(m_instanceRoot);
        if (updated.worlds.isEmpty())
            updated = m_meta;
        updated.archive.path = m_archivePath;
        updated.archive.libraryCopy = m_archivePath;
        updated.archive.size = QFileInfo(m_archivePath).size();
        if (!m_variant.isEmpty())
            updated.variant = m_variant;
        QString error;
        if (!updated.save(m_instanceRoot, &error))
            qWarning() << "Map reinstall: map.json save failed:" << error;
    }

    if (!result) {
        emitFailed(result.error());
    } else {
        emitSucceeded();
    }
}

}  // namespace maps
