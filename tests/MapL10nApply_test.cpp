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
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>

#include <archive/ArchiveReader.h>
#include <archive/ArchiveWriter.h>
#include <maps/MapL10nApply.h>
#include <maps/MapL10nProbe.h>

#include <FileSystem.h>

using namespace maps::l10n;

namespace {

QByteArray fileHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return hash.result();
}

QString fixtureWorld()
{
    return QFINDTESTDATA("testdata/MapL10n/world");
}

/// 把夹具世界复制到 dest（`FS::copy` 自己会建目录）
bool copyFixtureTo(const QString& dest)
{
    FS::copy copier(fixtureWorld(), dest);
    return copier();
}

bool writeFile(const QString& path, const QByteArray& data)
{
    FS::ensureFilePathExists(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(data) == data.size();
}

/// 列出 zip 里的文件条目（回调返回 false = 继续，不是失败）
QStringList listZip(const QString& path)
{
    QStringList names;
    MMCZip::ArchiveReader reader(path);
    reader.parse([&names](MMCZip::ArchiveReader::File* file) -> Result<bool> {
        if (file->isFile())
            names.append(file->filename());
        return false;
    });
    return names;
}

}  // namespace

/**
 * C 档（注入式汉化）的写回。
 *
 * 这一组测试盯的是**不能写坏**，而不是"翻得对不对"：
 *   · 源存档必须一个字节不变；
 *   · 改过的区块结构必须与原区块一致（文本字段清空后逐字节比）；
 *   · 没改的区块必须原样保留。
 */
class MapL10nApplyTest : public QObject {
    Q_OBJECT

   private slots:
    void refusesEmptyMapping()
    {
        QTemporaryDir dir;
        const auto stats = applyToCopy(fixtureWorld(), dir.filePath("out"), {});
        QVERIFY(!stats.error.isEmpty());
        QVERIFY(!stats.ok());
        QVERIFY(!QFileInfo::exists(dir.filePath("out")));
    }

    // ── 备份 + 替换（C 档收尾：不删实例，旧存档打包后顶替）──

    /**
     * 正常替换：旧存档进 zip，新存档就位，源目录不残留。
     *
     * 这一条盯的是"不可逆操作必须有退路"：只要 zip 里的东西跟原来一致，
     * 玩家随时能解压回去。
     */
    void replaceBacksUpOldWorldAndSwapsInNew()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString saves = dir.filePath(QStringLiteral("saves"));
        const QString oldWorld = QDir(saves).filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(saves));
        QVERIFY(copyFixtureTo(oldWorld));

        // 新存档：同样的夹具，外加一个标记文件，用来确认"确实换成了新的"
        const QString newWorld = dir.filePath(QStringLiteral("staged/MapWorld"));
        QVERIFY(copyFixtureTo(newWorld));
        QVERIFY(writeFile(QDir(newWorld).filePath(QStringLiteral("l10n-marker.txt")), "patched"));

        const QString backupDir = dir.filePath(QStringLiteral("backups"));
        const auto stats = replaceWorldWithBackup(oldWorld, newWorld, backupDir);

        QVERIFY2(stats.ok(), qPrintable(stats.error));
        QCOMPARE(stats.worldFolder, QStringLiteral("MapWorld"));
        QVERIFY(stats.files >= 6);  // 夹具里的 6 个文件
        QVERIFY(stats.backupBytes > 0);
        QVERIFY(QFileInfo::exists(stats.backupPath));

        // 新存档就位、旧内容（标记文件不该有）
        QVERIFY(QFileInfo::exists(QDir(oldWorld).filePath(QStringLiteral("l10n-marker.txt"))));
        QVERIFY(QFileInfo::exists(QDir(oldWorld).filePath(QStringLiteral("level.dat"))));

        // 暂存目录已经被移走，没有留下第二份
        QVERIFY(!QFileInfo::exists(newWorld));

        // 备份里必须有旧存档的全部文件
        const auto entries = listZip(stats.backupPath);
        QVERIFY2(entries.contains(QStringLiteral("level.dat")), qPrintable(entries.join(QLatin1Char(','))));
        QVERIFY(entries.contains(QStringLiteral("region/r.0.0.mca")));
        QVERIFY(entries.contains(QStringLiteral("entities/r.0.0.mca")));
        QVERIFY(entries.contains(QStringLiteral("data/scoreboard.dat")));
        QVERIFY(entries.contains(QStringLiteral("datapacks/demo/pack.mcmeta")));
        QVERIFY(!entries.contains(QStringLiteral("l10n-marker.txt")));

        // 临时挪开的旧目录要清掉，不然每次替换都留一份 67 MB
        const auto leftovers = QDir(saves).entryList(QStringList{ QStringLiteral("MapWorld.old-*") }, QDir::Dirs);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));
    }

    /**
     * 恢复原图：备份 zip → 解回世界目录。
     *
     * 这条链路是「汉化写坏了怎么办」的唯一退路，必须验证：
     *   · 备份里的文件原样回来（汉化版被顶掉）；
     *   · 当前那份先挪开、成功后才删（不留 `MapWorld.old-*` 垃圾）；
     *   · 备份里没有 level.dat 时拒绝动手。
     */
    void restorePutsBackupBackAndKeepsNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString saves = dir.filePath(QStringLiteral("saves"));
        const QString world = QDir(saves).filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(saves));
        QVERIFY(copyFixtureTo(world));

        const QString backupDir = dir.filePath(QStringLiteral("backups"));

        // 先用"汉化版"顶替一次，正好得到一份真备份
        const QString l10nWorld = dir.filePath(QStringLiteral("staged/MapWorld"));
        QVERIFY(copyFixtureTo(l10nWorld));
        QVERIFY(writeFile(QDir(l10nWorld).filePath(QStringLiteral("zh-marker.txt")), "translated"));
        QVERIFY(writeFile(QDir(l10nWorld).filePath(QStringLiteral("level.dat")), "patched-level"));
        const auto replaced = replaceWorldWithBackup(world, l10nWorld, backupDir);
        QVERIFY2(replaced.ok(), qPrintable(replaced.error));
        QVERIFY(QFileInfo::exists(QDir(world).filePath(QStringLiteral("zh-marker.txt"))));

        const auto backups = listBackups(backupDir);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(backups.first().path, replaced.backupPath);
        QVERIFY(backups.first().bytes > 0);

        // 还原：汉化版标记要消失，原始 level.dat 要回来
        const auto restored = restoreWorldFromBackup(world, backups.first().path);
        QVERIFY2(restored.ok(), qPrintable(restored.error));
        QCOMPARE(restored.worldFolder, QStringLiteral("MapWorld"));
        QVERIFY(restored.files >= 6);
        QVERIFY(!QFileInfo::exists(QDir(world).filePath(QStringLiteral("zh-marker.txt"))));
        QVERIFY(QFileInfo::exists(QDir(world).filePath(QStringLiteral("level.dat"))));
        QVERIFY(QFileInfo::exists(QDir(world).filePath(QStringLiteral("region/r.0.0.mca"))));
        QVERIFY(!QFileInfo::exists(QDir(world).filePath(QStringLiteral("level.dat.restore-marker"))));

        // 临时目录与挪开的旧目录都不该留下
        const auto leftovers = QDir(saves).entryList(QStringList{ QStringLiteral("MapWorld.*") }, QDir::Dirs | QDir::Files);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));
    }

    void restoreRefusesZipWithoutLevelDat()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString world = dir.filePath(QStringLiteral("saves/MapWorld"));
        QVERIFY(copyFixtureTo(world));
        const QByteArray before = fileHash(QDir(world).filePath(QStringLiteral("level.dat")));

        // 一份"不像世界"的 zip
        const QString zip = dir.filePath(QStringLiteral("backups/broken.zip"));
        QVERIFY(FS::ensureFilePathExists(zip));
        {
            MMCZip::ArchiveWriter writer(zip);
            QVERIFY(writer.open().has_value());
            const QString junk = dir.filePath(QStringLiteral("junk.txt"));
            QVERIFY(writeFile(junk, "not a world"));
            QVERIFY(writer.addFile(junk, QStringLiteral("junk.txt")).has_value());
            QVERIFY(writer.close().has_value());
        }

        const auto restored = restoreWorldFromBackup(world, zip);
        QVERIFY(!restored.ok());
        QVERIFY(restored.error.contains(QStringLiteral("level.dat")));
        // 原世界一个字节没动
        QCOMPARE(fileHash(QDir(world).filePath(QStringLiteral("level.dat"))), before);
        const auto leftovers =
            QDir(dir.filePath(QStringLiteral("saves"))).entryList(QStringList{ QStringLiteral("MapWorld.*") }, QDir::Dirs);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));
    }

    /// 缺 level.dat 的目录不能顶替（否则游戏会把它当成新世界重建，玩家的存档就"消失"了）
    void refusesIncompleteNewWorld()
    {
        QTemporaryDir dir;
        const QString oldWorld = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(copyFixtureTo(oldWorld));
        const QByteArray before = fileHash(QDir(oldWorld).filePath(QStringLiteral("level.dat")));

        const QString newWorld = dir.filePath(QStringLiteral("staged"));
        QVERIFY(QDir().mkpath(newWorld));
        QVERIFY(writeFile(QDir(newWorld).filePath(QStringLiteral("region/r.0.0.mca")), "not a real region"));

        const auto stats = replaceWorldWithBackup(oldWorld, newWorld, dir.filePath(QStringLiteral("backups")));

        QVERIFY(!stats.ok());
        QVERIFY(stats.error.contains(QStringLiteral("level.dat")));
        // 旧存档一个字节都没动
        QCOMPARE(fileHash(QDir(oldWorld).filePath(QStringLiteral("level.dat"))), before);
        QVERIFY(QFileInfo::exists(newWorld));
        QVERIFY(!QDir(dir.filePath(QStringLiteral("backups"))).exists());
    }

    /// 打包阶段取消：旧存档原样、不留半成品 zip
    void cancelDuringBackupLeavesEverythingAlone()
    {
        QTemporaryDir dir;
        const QString oldWorld = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(copyFixtureTo(oldWorld));
        const QByteArray before = fileHash(QDir(oldWorld).filePath(QStringLiteral("level.dat")));

        const QString newWorld = dir.filePath(QStringLiteral("staged"));
        QVERIFY(copyFixtureTo(newWorld));

        const auto stats = replaceWorldWithBackup(oldWorld, newWorld, dir.filePath(QStringLiteral("backups")),
                                                  [](int done, int) { return done < 2; });

        QVERIFY(!stats.ok());
        QVERIFY(stats.error.contains(QStringLiteral("已取消")));
        QCOMPARE(fileHash(QDir(oldWorld).filePath(QStringLiteral("level.dat"))), before);
        QVERIFY(QFileInfo::exists(newWorld));  // 暂存目录还在（没被移走）

        const QDir backups(dir.filePath(QStringLiteral("backups")));
        QVERIFY(backups.entryList(QDir::Files).isEmpty());
    }

    void refusesExistingDestination()
    {
        QTemporaryDir dir;
        const QString out = dir.filePath("out");
        QVERIFY(QDir().mkpath(out));
        const auto stats = applyToCopy(fixtureWorld(), out, { { QStringLiteral("Eat Me"), QStringLiteral("吃我") } });
        QVERIFY(!stats.ok());
        QVERIFY(stats.error.contains(QStringLiteral("已存在")));
    }

    void patchesCopyAndLeavesSourceUntouched()
    {
        const QString world = fixtureWorld();
        QVERIFY2(!world.isEmpty(), "找不到夹具世界");

        const QString regionPath = QDir(world).filePath(QStringLiteral("region/r.0.0.mca"));
        const QByteArray beforeHash = fileHash(regionPath);
        QVERIFY(!beforeHash.isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString out = dir.filePath("MapWorld");

        const QHash<QString, QString> mapping{
            { QStringLiteral("Eat Me"), QStringLiteral("吃我") },
            { QStringLiteral("Storage"), QStringLiteral("储物桶") },
            { QStringLiteral("Welcome"), QStringLiteral("欢迎") },
            { QStringLiteral("you've had"), QStringLiteral("你已经吃") },
            { QStringLiteral("A core of solid iron"), QStringLiteral("实心铁芯") },
        };

        const auto stats = applyToCopy(world, out, mapping);
        QVERIFY2(stats.error.isEmpty(), qPrintable(stats.error));
        QVERIFY(stats.ok());
        QCOMPARE(stats.structureDiffs, 0);
        QVERIFY(stats.patchedChunks >= 1);
        QVERIFY(stats.replaced >= 5);
        QCOMPARE(stats.files, 2);  // region + entities 两个文件都会被扫到

        // 源存档必须一个字节不变
        QCOMPARE(fileHash(regionPath), beforeHash);

        // 副本存在，而且内容确实变了
        const QString outRegion = QDir(out).filePath(QStringLiteral("region/r.0.0.mca"));
        QVERIFY(QFile::exists(outRegion));
        QVERIFY(fileHash(outRegion) != beforeHash);

        // 再体检一遍副本：翻掉的原文不该再出现在待译表里
        auto reprobe = scanFast(out, QStringLiteral("1.21.1"));
        QVERIFY(scanRegions(reprobe, [](int, int) { return true; }));
        QVERIFY(!reprobe.regions.texts.contains(QStringLiteral("Eat Me")));
        QVERIFY(!reprobe.regions.texts.contains(QStringLiteral("Storage")));
        // 没在 mapping 里的原文仍然在（"没翻的就用原文"）
        QVERIFY(reprobe.regions.texts.contains(QStringLiteral("of the Old World")));
    }

    void keepsUnmappedTextVerbatim()
    {
        QTemporaryDir dir;
        const auto stats = applyToCopy(fixtureWorld(), dir.filePath("MapWorld"),
                                       { { QStringLiteral("Eat Me"), QStringLiteral("吃我") } });
        QVERIFY2(stats.error.isEmpty(), qPrintable(stats.error));
        QCOMPARE(stats.structureDiffs, 0);

        // 只改了一处，其余照旧
        auto reprobe = scanFast(dir.filePath("MapWorld"), QStringLiteral("1.21.1"));
        QVERIFY(scanRegions(reprobe, [](int, int) { return true; }));
        QVERIFY(!reprobe.regions.texts.contains(QStringLiteral("Eat Me")));
        QVERIFY(reprobe.regions.texts.contains(QStringLiteral("Storage")));
        QVERIFY(reprobe.regions.texts.contains(QStringLiteral("Welcome")));
    }

    void loadsTranslationsFromJson()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("strings.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schema":1,"items":[
            {"kind":"literal","en":"Hello","zh":"你好"},
            {"kind":"literal","en":"Empty","zh":""},
            {"kind":"key","key":"a.b","en":"Item","zh":"物品"}]})");
        file.close();

        int available = 0;
        QString error;
        const auto mapping = loadTranslations(path, &available, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(available, 2);
        QCOMPARE(mapping.value(QStringLiteral("Hello")), QStringLiteral("你好"));
        QCOMPARE(mapping.value(QStringLiteral("Item")), QStringLiteral("物品"));
        QVERIFY(!mapping.contains(QStringLiteral("Empty")));
    }

    /**
     * 真实地图端到端：写回 → 打包备份 → 替换，全链路走一遍。
     *
     * 不碰真实实例 —— 把存档复制到临时目录再操作。设 `ATLAS_L10N_WORLDS` 才会跑。
     */
    void replacesRealWorldWithBackup()
    {
        const QString base = qEnvironmentVariable("ATLAS_L10N_WORLDS");
        const QString instance = qEnvironmentVariable("ATLAS_L10N_APPLY_ONE");
        if (base.isEmpty() || instance.isEmpty())
            QSKIP("未设置 ATLAS_L10N_WORLDS / ATLAS_L10N_APPLY_ONE，跳过真实地图替换");

        const QString source = QDir(base).filePath(instance + QStringLiteral("/.minecraft/saves/MapWorld"));
        if (!QFileInfo(source).isDir())
            QSKIP("找不到指定实例的存档");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString oldWorld = dir.filePath(QStringLiteral("saves/MapWorld"));
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("saves"))));
        {
            FS::copy copier(source, oldWorld);
            QVERIFY(copier());
        }

        // 汉化版就用真实写回产出，而不是随便复制一份 —— 这样才验证得了
        // "applyToCopy 的产物能不能被 replace 接手"（缺 level.dat 会被拒绝）
        auto probe = scanFast(source, QString());
        QVERIFY(scanRegions(probe, [](int, int) { return true; }));
        QVERIFY(!probe.regions.texts.isEmpty());
        QHash<QString, QString> mapping;
        for (auto it = probe.regions.texts.begin(); it != probe.regions.texts.end(); ++it)
            mapping.insert(it.key(), QStringLiteral("【译】") + it.key());

        const QString newWorld = dir.filePath(QStringLiteral("staged/MapWorld"));
        const auto applyStats = applyToCopy(source, newWorld, mapping);
        QVERIFY2(applyStats.error.isEmpty(), qPrintable(applyStats.error));
        QVERIFY(applyStats.replaced > 0);
        QVERIFY(QFileInfo::exists(QDir(newWorld).filePath(QStringLiteral("level.dat"))));

        QElapsedTimer timer;
        timer.start();
        const auto stats = replaceWorldWithBackup(oldWorld, newWorld, dir.filePath(QStringLiteral("backups")));
        const qint64 elapsed = timer.elapsed();

        qInfo().noquote() << QStringLiteral("%1 备份 + 替换：%2 个文件 / 备份 %3 / 耗时 %4 秒")
                                 .arg(instance)
                                 .arg(stats.files)
                                 .arg(QLocale().formattedDataSize(stats.backupBytes))
                                 .arg(elapsed / 1000.0, 0, 'f', 1);

        QVERIFY2(stats.ok(), qPrintable(stats.error));
        QVERIFY(stats.files > 20);        // 真实地图远不止夹具那几个文件
        QVERIFY(stats.backupBytes > 1024 * 1024);
        QVERIFY(!QFileInfo::exists(newWorld));

        // 备份里的条目数应与打进的文件数一致（目录条目不算）
        const auto entries = listZip(stats.backupPath);
        QCOMPARE(entries.size(), stats.files);
        QVERIFY(entries.contains(QStringLiteral("level.dat")));

        // 替换后的世界能被正常体检，且原文确实被译文顶掉了
        auto after = scanFast(oldWorld, QString());
        QVERIFY(scanRegions(after, [](int, int) { return true; }));
        QCOMPARE(after.chunks, probe.chunks);  // 区块一个不少
        QVERIFY2(after.regions.texts.size() < probe.regions.texts.size(),
                 qPrintable(QStringLiteral("待译条数没下降：%1 → %2")
                                .arg(probe.regions.texts.size())
                                .arg(after.regions.texts.size())));
    }

    /**
     * 真实地图：把 Torre de Piedra 的待译表全部"翻"成 `【译】原文`，走一遍写回。
     *
     * 不是验证翻译质量，是验证**在真实存档上不写坏**：67 MB、15731 个区块、
     * 六个维度的数据都要过一遍结构比对。设 `ATLAS_L10N_WORLDS` 才会跑。
     */
    void appliesToRealWorld()
    {
        const QString base = qEnvironmentVariable("ATLAS_L10N_WORLDS");
        const QString instance = qEnvironmentVariable("ATLAS_L10N_APPLY_ONE");
        if (base.isEmpty() || instance.isEmpty())
            QSKIP("未设置 ATLAS_L10N_WORLDS / ATLAS_L10N_APPLY_ONE，跳过真实地图写回");

        const QString world = QDir(base).filePath(instance + QStringLiteral("/.minecraft/saves/MapWorld"));
        if (!QFileInfo(world).isDir())
            QSKIP("找不到指定实例的存档");

        auto probe = scanFast(world, QString());
        QVERIFY(scanRegions(probe, [](int, int) { return true; }));
        QVERIFY(!probe.regions.texts.isEmpty());

        QHash<QString, QString> mapping;
        for (auto it = probe.regions.texts.begin(); it != probe.regions.texts.end(); ++it)
            mapping.insert(it.key(), QStringLiteral("【译】") + it.key());
        for (auto it = probe.datapacks.texts.begin(); it != probe.datapacks.texts.end(); ++it)
            mapping.insert(it.key(), QStringLiteral("【译】") + it.key());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QElapsedTimer timer;
        timer.start();
        const auto stats = applyToCopy(world, dir.filePath("MapWorld"), mapping);
        const qint64 elapsed = timer.elapsed();

        qInfo().noquote() << QStringLiteral("%1：%2 个文件 / %3 个区块 / 改写 %4 块 / 替换 %5 处 / "
                                            "结构差异 %6 / 丢弃 %7 / 耗时 %8 秒")
                                 .arg(instance)
                                 .arg(stats.files)
                                 .arg(stats.chunks)
                                 .arg(stats.patchedChunks)
                                 .arg(stats.replaced)
                                 .arg(stats.structureDiffs)
                                 .arg(stats.discarded)
                                 .arg(elapsed / 1000.0, 0, 'f', 1);

        QVERIFY2(stats.error.isEmpty(), qPrintable(stats.error));
        QCOMPARE(stats.structureDiffs, 0);
        QCOMPARE(stats.discarded, 0);
        QVERIFY(stats.patchedChunks > 0);
        QVERIFY(stats.replaced > 0);

        // 副本能被正常体检（说明写出来的区块结构没坏）
        auto reprobe = scanFast(dir.filePath("MapWorld"), QString());
        QVERIFY(scanRegions(reprobe, [](int, int) { return true; }));
        QCOMPARE(reprobe.chunks, probe.chunks);  // 区块一个不少
        QVERIFY(reprobe.regions.texts.size() < probe.regions.texts.size());  // 英文确实少了
    }

    /**
     * A 档：只改资源包（写 zh_cn.json），存档数据一个字节都不碰。
     *
     * 两种形态都要覆盖 —— 世界里既有 `resources.zip` 也有 `resources/` 目录，
     * zip 还得整包重写（`zh_cn.json` 以外的条目必须原样搬过去）。
     */
    void applyToPackWritesZhCnIntoDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString world = dir.filePath(QStringLiteral("MapWorld"));
        const QString langDir = world + QStringLiteral("/resources/assets/test/lang");
        QVERIFY(QDir().mkpath(langDir));
        QVERIFY(writeFile(langDir + "/en_us.json", R"({"a":"Hello there","b":"Open the door"})"));

        QHash<QString, QString> zh;
        zh.insert(QStringLiteral("a"), QStringLiteral("你好"));
        zh.insert(QStringLiteral("nope"), QStringLiteral("拼错的键"));  // 不在表里，应被忽略

        const auto stats = applyToPack(world, zh, dir.filePath(QStringLiteral("backups")));
        QVERIFY2(stats.ok(), qPrintable(stats.error));
        QCOMPARE(stats.localeEntry, QStringLiteral("assets/test/lang/zh_cn.json"));
        QCOMPARE(stats.translated, 1);  // 只有 a 命中
        QCOMPARE(stats.total, 2);

        QFile file(langDir + "/zh_cn.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(doc.value(QStringLiteral("a")).toString(), QStringLiteral("你好"));
        // 没翻到的键保持英文，而不是显示成键名
        QCOMPARE(doc.value(QStringLiteral("b")).toString(), QStringLiteral("Open the door"));
        QVERIFY(!doc.contains(QStringLiteral("nope")));
    }

    void applyToPackRewritesZipAndKeepsOtherEntries()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString world = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(world));

        const QString zip = world + QStringLiteral("/resources.zip");
        const QByteArray texture("PNGDATA-1234567890");
        {
            MMCZip::ArchiveWriter writer(zip);
            QVERIFY(writer.open().has_value());
            QVERIFY(writer.addFile(QStringLiteral("pack.mcmeta"), QByteArray(R"({"pack":{"pack_format":15}})")).has_value());
            QVERIFY(writer
                        .addFile(QStringLiteral("assets/test/lang/en_us.json"),
                                 QByteArray(R"({"title":"The Stone Fear","sub":"Enter the cave"})"))
                        .has_value());
            QVERIFY(writer.addFile(QStringLiteral("assets/test/textures/font/glyph.png"), texture).has_value());
            QVERIFY(writer.close().has_value());
        }

        QHash<QString, QString> zh;
        zh.insert(QStringLiteral("title"), QStringLiteral("石之恐惧"));

        const QString backups = dir.filePath(QStringLiteral("backups"));
        const auto stats = applyToPack(world, zh, backups);
        QVERIFY2(stats.ok(), qPrintable(stats.error));
        QCOMPARE(stats.translated, 1);
        QVERIFY(QFileInfo::exists(stats.backupPath));  // 原包先备份

        // 新 zip：zh_cn.json 有了，其它条目一个不少、字节一致
        MMCZip::ArchiveReader reader(zip);
        QVERIFY(reader.collectFiles(true).has_value());  // getFiles() 要先收集
        const auto names = reader.getFiles();
        QVERIFY2(names.contains(QStringLiteral("assets/test/lang/zh_cn.json")), qPrintable(names.join(QLatin1Char(','))));
        QVERIFY(names.contains(QStringLiteral("assets/test/textures/font/glyph.png")));
        QVERIFY(names.contains(QStringLiteral("pack.mcmeta")));
        const auto kept = reader.readFile(QStringLiteral("assets/test/textures/font/glyph.png"));
        QVERIFY(kept.has_value());
        QCOMPARE(*kept, texture);

        const auto table = reader.readFile(QStringLiteral("assets/test/lang/zh_cn.json"));
        QVERIFY(table.has_value());
        const auto doc = QJsonDocument::fromJson(*table).object();
        QCOMPARE(doc.value(QStringLiteral("title")).toString(), QStringLiteral("石之恐惧"));
        QCOMPARE(doc.value(QStringLiteral("sub")).toString(), QStringLiteral("Enter the cave"));

        // 临时文件不许留下
        QVERIFY(!QFileInfo::exists(zip + QStringLiteral(".new")));
    }

    /// 没有资源包的图，A 档要干净地拒绝（而不是写坏什么）
    void applyToPackWithoutPackFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString world = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(world));
        QHash<QString, QString> zh;
        zh.insert(QStringLiteral("a"), QStringLiteral("甲"));
        const auto stats = applyToPack(world, zh, dir.filePath(QStringLiteral("backups")));
        QVERIFY(!stats.ok());
        QVERIFY(stats.error.contains(QStringLiteral("资源包")));
    }

    /**
     * 真实资源包上的 A 档（`ATLAS_L10N_WORLDS` 门控）。
     *
     * 合成夹具只有三个条目，测不出"上千条目的真实包里 addFile 逐个搬运"会不会漏；
     * 这里把真实 `resources.zip` 复制到临时目录再改，**不动真实存档**。
     */
    void applyToPackOnRealPacks()
    {
        const QString base = qEnvironmentVariable("ATLAS_L10N_WORLDS");
        if (base.isEmpty())
            QSKIP("未设置 ATLAS_L10N_WORLDS，跳过真实资源包写回");

        const QDir root(base);
        if (!root.exists())
            QSKIP("ATLAS_L10N_WORLDS 指向的目录不存在");

        int tested = 0;
        for (const auto& instance : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            for (const auto& worldName : QDir(root.filePath(instance + QStringLiteral("/.minecraft/saves")))
                                             .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const QString sourceZip =
                    root.filePath(instance + QStringLiteral("/.minecraft/saves/") + worldName + QStringLiteral("/resources.zip"));
                if (!QFileInfo::exists(sourceZip))
                    continue;

                QTemporaryDir tmp;
                QVERIFY(tmp.isValid());
                const QString world = tmp.filePath(QStringLiteral("MapWorld"));
                QVERIFY(QDir().mkpath(world));
                const QString zip = world + QStringLiteral("/resources.zip");
                QVERIFY(QFile::copy(sourceZip, zip));

                /**
                 * 找一个真实键来翻。必须取**键最多的那个命名空间** ——
                 * `applyToPack` 就是这么选目标的（与探针一致），
                 * 随便取第一个 en_us 会挑到小命名空间，然后"一条都对不上"。
                 */
                QString ns;
                QString key;
                int bestSize = -1;
                {
                    MMCZip::ArchiveReader reader(zip);
                    reader.parse([&](MMCZip::ArchiveReader::File* file) -> Result<bool> {
                        const auto parts = file->filename().split(QLatin1Char('/'));
                        if (!file->isFile() || parts.size() != 4 || parts.at(0) != QLatin1String("assets")
                            || parts.at(2) != QLatin1String("lang") || parts.at(3) != QLatin1String("en_us.json"))
                            return false;
                        const auto data = file->readAll();
                        if (!data)
                            return false;
                        const auto object = QJsonDocument::fromJson(*data).object();
                        if (object.isEmpty() || object.size() <= bestSize)
                            return false;
                        bestSize = int(object.size());
                        ns = parts.at(1);
                        key = object.begin().key();
                        return false;  // 继续找更大的
                    });
                }
                if (ns.isEmpty() || key.isEmpty())
                    continue;

                // 挑一个非语言表条目，改前改后必须字节一致
                QByteArray sampleName;
                QByteArray sampleData;
                {
                    MMCZip::ArchiveReader reader(zip);
                    reader.parse([&](MMCZip::ArchiveReader::File* file) -> Result<bool> {
                        if (!file->isFile() || file->filename().contains(QStringLiteral("/lang/")))
                            return false;
                        const auto data = file->readAll();
                        if (!data || data->isEmpty())
                            return false;
                        sampleName = file->filename().toUtf8();
                        sampleData = *data;
                        return true;
                    });
                }

                QHash<QString, QString> zh;
                zh.insert(key, QStringLiteral("测试译文"));
                const auto stats = applyToPack(world, zh, tmp.filePath(QStringLiteral("backups")));
                QVERIFY2(stats.ok(), qPrintable(QStringLiteral("%1: %2").arg(instance, stats.error)));
                QCOMPARE(stats.translated, 1);

                MMCZip::ArchiveReader after(zip);
                QVERIFY(after.collectFiles(true).has_value());  // exists() 查的是 collect 出来的表
                QVERIFY(after.exists(QStringLiteral("assets/%1/lang/zh_cn.json").arg(ns)));
                if (!sampleName.isEmpty()) {
                    const auto kept = after.readFile(QString::fromUtf8(sampleName));
                    QVERIFY2(kept.has_value(), "非语言表条目必须还在");
                    QCOMPARE(*kept, sampleData);
                }
                qInfo().noquote() << QStringLiteral("%1: 资源包 %2 键，写入 1 条，%3 字节")
                                         .arg(instance)
                                         .arg(stats.total)
                                         .arg(QFileInfo(zip).size());
                ++tested;
            }
        }
        QVERIFY2(tested > 0, "ATLAS_L10N_WORLDS 下没有带 resources.zip 的世界");
    }
};

QTEST_GUILESS_MAIN(MapL10nApplyTest)

#include "MapL10nApply_test.moc"
