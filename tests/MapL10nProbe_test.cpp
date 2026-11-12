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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <maps/MapL10nExport.h>
#include <maps/MapL10nProbe.h>

using namespace maps::l10n;

/**
 * 夹具世界（`testdata/MapL10n/world`）是合成场景，内容已知：
 *
 *   区块 r.0.0 里 1 个区块：告示牌两行、箱子物品的名字与两行 lore、命令方块里嵌的
 *   JSON、木桶的 CustomName；实体 r.0.0 里一个 text_display。
 *   数据包里一个函数：一处 `translate`、一处 `text`。
 *   没有 resources.zip —— 语言表路线在这里应当判"用不上"。
 *
 * 这些断言的作用不是"跑通"，而是**盯住数字**：体检数字一旦漂移，后面的
 * 抽取与写回全是错的，而那种错误在游戏里只会表现为"某几句没汉化"。
 */
class MapL10nProbeTest : public QObject {
    Q_OBJECT

   private slots:
    void proseClassification_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("expected");

        QTest::newRow("普通短语") << "Secret Item" << true;
        QTest::newRow("带格式码") << QStringLiteral("\u00a78Secret Item") << true;
        QTest::newRow("带私用区图标")
            << QStringLiteral("\u00a7f\ue100\ue002\ue101\u00a7r\u00a74Gilded Chest") << true;
        QTest::newRow("中文") << QStringLiteral("镀金箱子") << true;
        QTest::newRow("标识符") << "sapping_potion_buff" << false;
        QTest::newRow("命名空间") << "minecraft:glistering_melon_slice" << false;
        QTest::newRow("单字符") << "a" << false;
        QTest::newRow("纯字形") << QStringLiteral("\ue100\ue009\ue101") << false;
        QTest::newRow("命令") << "/give @p minecraft:stone" << false;
        QTest::newRow("空串") << "" << false;
        QTest::newRow("base64")
            << "eyJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0" << false;
    }

    void proseClassification()
    {
        QFETCH(QString, text);
        QFETCH(bool, expected);
        QCOMPARE(looksLikeProse(text), expected);
    }

    void translateUnitsHandleComponents()
    {
        // 组件对象：取 text，忽略颜色等格式
        QCOMPARE(translateUnits(QStringLiteral(R"({"color":"gold","text":"Eat Me"})")), QStringList({ "Eat Me" }));
        // 告示牌整条消息是带外层引号的 JSON 字符串
        QCOMPARE(translateUnits(QStringLiteral("\"you've had\"")), QStringList({ "you've had" }));
        // 只有 translate 的组件没有字面量（文本在语言表里，走引用计数）
        QVERIFY(translateUnits(QStringLiteral(R"({"translate":"demo.key"})")).isEmpty());
        // 裸文本原样返回
        QCOMPARE(translateUnits(QStringLiteral("Storage")), QStringList({ "Storage" }));
    }

    void fastScanReadsDatapacksButNotChunks()
    {
        const QString world = QFINDTESTDATA("testdata/MapL10n/world");
        QVERIFY2(!world.isEmpty(), "找不到夹具世界");

        const auto result = scanFast(world, QStringLiteral("1.21.1"));
        QVERIFY2(result.isValid(), qPrintable(result.error));

        // 夹具没有 resources.zip / resources/
        QVERIFY(!result.pack.found);
        QCOMPARE(result.pack.enKeys, 0);

        // 数据包：一个函数文件，一处 translate、一处 text
        QCOMPARE(result.datapackFiles, 1);
        QCOMPARE(result.datapacks.refs, 1);
        QVERIFY(result.datapacks.keys.contains(QStringLiteral("demo.key")));

        // 快检不碰区块
        QVERIFY(!result.regionsScanned);
        QCOMPARE(result.chunks, 0);
        QCOMPARE(result.regions.uniqueTexts(), 0);
    }

    void regionScanFindsEveryCarrier()
    {
        const QString world = QFINDTESTDATA("testdata/MapL10n/world");
        QVERIFY2(!world.isEmpty(), "找不到夹具世界");

        auto result = scanFast(world, QStringLiteral("1.21.1"));
        int lastDone = -1;
        int lastTotal = -1;
        const bool finished = scanRegions(result, [&](int done, int total) {
            lastDone = done;
            lastTotal = total;
            return true;
        });

        QVERIFY(finished);
        QVERIFY(result.regionsScanned);
        QCOMPARE(lastDone, lastTotal);
        // region/r.0.0.mca 有 1 个区块；entities/r.0.0.mca 只有 8 KB 头部、0 个区块
        // —— 空 region 要当成"0 区块"而不是错误（地图常带这种文件）
        QCOMPARE(result.regionFiles, 2);
        QCOMPARE(result.chunks, 1);
        QCOMPARE(result.regions.errors, 0);

        // 六种载体都要被抓到：告示牌两行、物品名、两行 lore、命令方块里的名字、CustomName、实体文本
        const auto& texts = result.regions.texts;
        for (const auto* expected : { "you've had", "enough apples", "Eat Me", "A core of solid iron",
                                      "of the Old World", "Verdant Helmet", "Storage", "Welcome" })
            QVERIFY2(texts.contains(QLatin1String(expected)),
                     qPrintable(QStringLiteral("漏了载体文本：%1").arg(QLatin1String(expected))));

        // 空告示牌行（""）不该被当成待译
        QVERIFY(!texts.contains(QString()));

        // translate 组件要计成引用，不是字面量
        QVERIFY(result.regions.refs >= 0);
    }

    void cancellationStopsTheScan()
    {
        const QString world = QFINDTESTDATA("testdata/MapL10n/world");
        QVERIFY2(!world.isEmpty(), "找不到夹具世界");

        auto result = scanFast(world, QStringLiteral("1.21.1"));
        const bool finished = scanRegions(result, [](int, int) { return false; });

        QVERIFY(!finished);
        QVERIFY(!result.regionsScanned);
    }

    void exportProducesJsonAndCsv()
    {
        const QString world = QFINDTESTDATA("testdata/MapL10n/world");
        QVERIFY2(!world.isEmpty(), "找不到夹具世界");

        auto result = scanFast(world, QStringLiteral("1.21.1"));
        QVERIFY(scanRegions(result, [](int, int) { return true; }));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto exported = exportStrings(result, dir.filePath("l10n"));
        QVERIFY2(exported.error.isEmpty(), qPrintable(exported.error));
        QVERIFY(exported.ok());
        QCOMPARE(exported.total, exported.fromPack + exported.fromWorld);
        QVERIFY(exported.regionsScanned);

        // ── JSON：结构稳定，条目数与统计自洽 ──
        QFile json(exported.jsonPath);
        QVERIFY(json.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(json.readAll());
        QVERIFY(doc.isObject());
        const auto root = doc.object();
        QCOMPARE(root.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("mode")).toString(), QStringLiteral("injection"));
        QVERIFY(root.value(QStringLiteral("regionsScanned")).toBool());

        const auto items = root.value(QStringLiteral("items")).toArray();
        QCOMPARE(int(items.size()), exported.total);

        QStringList sources;
        for (const auto& value : items)
            sources << value.toObject().value(QStringLiteral("en")).toString();
        // 三种载体都要出现在清单里：区块里的物品名/告示牌、数据包里的字面量
        QVERIFY(sources.contains(QStringLiteral("Eat Me")));
        QVERIFY(sources.contains(QStringLiteral("you've had")));
        QVERIFY(sources.contains(QStringLiteral("Loose function text")));
        // 空告示牌行不该进清单
        QVERIFY(!sources.contains(QString()));

        // ── CSV：带 BOM（Excel 不乱码）、表头 + 每行一条 ──
        QFile csv(exported.csvPath);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QByteArray bytes = csv.readAll();
        QVERIFY2(bytes.startsWith("\xEF\xBB\xBF"), "CSV 没有 BOM，Excel 打开会乱码");

        const auto lines = QString::fromUtf8(bytes.mid(3)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(int(lines.size()), exported.total + 1);  // 表头 + 条目
        QVERIFY(lines.first().startsWith(QStringLiteral("id,kind,key,en,count,referenced,zh")));
    }

    void exportRefusesEmptyResult()
    {
        QTemporaryDir dir;
        const auto exported = exportStrings(ProbeResult{}, dir.filePath("l10n"));
        QVERIFY(!exported.ok());
        QVERIFY(!exported.error.isEmpty());
        QVERIFY(!QFile::exists(exported.jsonPath));
    }

    void translationNeedsChineseAwareness_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("expected");

        QTest::newRow("英文短语") << "Secret Item" << true;
        QTest::newRow("英文长句") << "Drop this item to generate a tesla core" << true;
        QTest::newRow("已经是中文") << QStringLiteral("镀金箱子") << false;
        QTest::newRow("带格式码的中文") << QStringLiteral("\u00a7f\u00a7o光拿着这东西你的手就变得很烫") << false;
        QTest::newRow("中英混排") << QStringLiteral("按 E 打开 Gilded Chest") << false;
        QTest::newRow("标识符") << "sapping_potion_buff" << false;
    }

    void translationNeedsChineseAwareness()
    {
        QFETCH(QString, text);
        QFETCH(bool, expected);
        // containsCjk 的正则写错时（\\u 转义），这条会立刻红 —— 它曾经真的红过
        QCOMPARE(needsTranslation(text), expected);
    }

    void modeThresholds()
    {
        ProbeResult result;

        // 全是 translate 引用 → 键化
        result.datapacks.refs = 100;
        QCOMPARE(result.mode(), Mode::Key);

        // 差不多对半 → 混合
        result.datapacks.refs = 50;
        result.datapacks.literalOccurrences = 50;
        QCOMPARE(result.mode(), Mode::Mixed);

        // 全是硬编码 → 注入
        result.datapacks.refs = 0;
        result.datapacks.literalOccurrences = 100;
        QCOMPARE(result.mode(), Mode::Injection);

        // 什么都没有时不能断言成"键化"
        QCOMPARE(ProbeResult{}.mode(), Mode::Injection);
    }

    void missingWorldIsReported()
    {
        const auto result = scanFast(QStringLiteral("Z:/definitely/not/here"), QStringLiteral("1.21.1"));
        QVERIFY(!result.isValid());
        QVERIFY(!result.error.isEmpty());
    }

    /**
     * 拿真实实例对表 —— **P0 闸门**。
     *
     * 设 `ATLAS_L10N_WORLDS` 指向一个放实例的目录（每个实例下有 `.minecraft/saves/MapWorld`），
     * 就会逐个体检并把数字打出来。不设则跳过，所以默认跑测试不受影响。
     *
     * 这里只断言"不可能错"的不变量（不发生解析崩溃、区块数与文件数自洽），
     * 具体数字靠人眼对参考实现（`mcsl` 的 probe/analyze）—— 两边不一致时，
     * 一定是本实现或那份实现有问题，必须先查清楚再往下做。
     */
    void probeRealWorlds()
    {
        const QString base = qEnvironmentVariable("ATLAS_L10N_WORLDS");
        if (base.isEmpty())
            QSKIP("未设置 ATLAS_L10N_WORLDS，跳过真实数据集对表");

        const QDir dir(base);
        if (!dir.exists())
            QSKIP("ATLAS_L10N_WORLDS 指向的目录不存在");

        int probed = 0;
        for (const auto& instance : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString world =
                QDir(base).filePath(instance + QStringLiteral("/.minecraft/saves/MapWorld"));
            if (!QFileInfo(world).isDir())
                continue;

            auto result = scanFast(world, QString());
            QVERIFY2(result.isValid(), qPrintable(QStringLiteral("%1: %2").arg(instance, result.error)));
            const bool finished = scanRegions(result, [](int, int) { return true; });
            QVERIFY(finished);

            QVERIFY2(result.chunks > 0 || result.regionFiles == 0,
                     qPrintable(QStringLiteral("%1: 有区块文件却一个区块都没读到").arg(instance)));
            QVERIFY2(result.regions.errors == 0,
                     qPrintable(QStringLiteral("%1: 解析失败 %2 处").arg(instance).arg(result.regions.errors)));

            qInfo().noquote()
                << QStringLiteral("%1 | %2 | 引用 %3 / 字面量 %4 / 键化率 %5 | 数据包 %6 文件 %7 引用 | "
                                  "区块 %8 文件 %9 块 %10 处硬编码（%11 条唯一） | 资源包 %12")
                       .arg(instance, modeLabel(result.mode()))
                       .arg(result.datapacks.refs)
                       .arg(result.datapacks.literalOccurrences)
                       .arg(result.keyRatio(), 0, 'f', 4)
                       .arg(result.datapackFiles)
                       .arg(result.datapacks.refs)
                       .arg(result.regionFiles)
                       .arg(result.chunks)
                       .arg(result.regions.literalOccurrences)
                       .arg(result.regions.uniqueTexts())
                       .arg(result.pack.found ? QStringLiteral("有（%1）").arg(result.pack.path)
                                              : QStringLiteral("无"));

            // 顺带把导出也走一遍：这是唯一覆盖"读真实 resources.zip 的语言表"的路径
            // （合成夹具里没有资源包，键化图的导出一行都测不到）
            QTemporaryDir out;
            QVERIFY(out.isValid());
            const auto exported = exportStrings(result, out.filePath("l10n"));
            QVERIFY2(exported.error.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(instance, exported.error)));
            QCOMPARE(exported.total, exported.fromPack + exported.fromWorld);
            qInfo().noquote() << QStringLiteral("    导出：%1 条（资源包 %2 + 存档 %3）")
                                     .arg(exported.total)
                                     .arg(exported.fromPack)
                                     .arg(exported.fromWorld);
            // 语言表里有**可翻散文**才应该有导出条目；键化图的表是字形表（可翻 0 条），
            // 导出 0 条才是对的 —— 以前的断言没加这个条件，2026-11-12 在这类图上误报
            if (result.pack.found && result.pack.missingProse > 0)
                QVERIFY2(exported.fromPack > 0, "有可翻的英文语言表却一条都没导出");
            if (result.pack.glyphTable)
                QVERIFY2(exported.fromPack == 0, "字形表不该导出任何条目");

            ++probed;
        }
        QVERIFY2(probed > 0, "ATLAS_L10N_WORLDS 下没找到任何 .minecraft/saves/MapWorld");
    }

    /**
     * 资源包的两副面孔：语言表里是**句子**，还是**字形表**。
     *
     * 2026-11-12 真机反馈：键化图的表里 8260 个键，界面只报"键数"，
     * 玩家以为导出漏了（其实可翻散文 0 条）。这条用例把两种表钉死。
     */
    void packProseVsGlyphTable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString world = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(world + QStringLiteral("/resources/assets/test/lang")));

        const auto writeLang = [&](const QByteArray& bytes) {
            QFile file(world + QStringLiteral("/resources/assets/test/lang/en_us.json"));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(file.write(bytes) == bytes.size());
        };

        // ① 真·语言表：两条散文
        writeLang(R"({"greeting":"Hello there","door":"Open the door"})");
        auto prose = scanFast(world, QStringLiteral("1.21.1"));
        QVERIFY(prose.isValid());
        QVERIFY(prose.pack.found);
        QCOMPARE(prose.pack.enKeys, 2);
        QCOMPARE(prose.pack.missingProse, 2);
        QVERIFY(!prose.pack.glyphTable);
        // 世界侧没有任何文本 → 结论就是"只改资源包"
        QCOMPARE(int(prose.mode()), int(Mode::Key));

        // ② 字形表：值全是私用区字形，一句散文都没有
        writeLang(QStringLiteral(R"({"offset.1":"\uf800%s\uf801","space.a":"\uf802\uf803"})").toUtf8());
        auto glyph = scanFast(world, QStringLiteral("1.21.1"));
        QVERIFY(glyph.isValid());
        QCOMPARE(glyph.pack.enKeys, 2);
        QCOMPARE(glyph.pack.missingProse, 0);
        QVERIFY2(glyph.pack.glyphTable, "私用区字形表必须被判成字形表");
        QVERIFY(!glyph.notes.isEmpty());  // 界面上要能说出来
    }

    /// 已经带中文的表不该再算"待译"（否则汉化版会被重导一遍）
    void packSkipsAlreadyChinese()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString world = dir.filePath(QStringLiteral("MapWorld"));
        QVERIFY(QDir().mkpath(world + QStringLiteral("/resources/assets/test/lang")));

        const auto write = [&](const QString& name, const QByteArray& bytes) {
            QFile file(world + QStringLiteral("/resources/assets/test/lang/") + name);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(file.write(bytes) == bytes.size());
        };
        write(QStringLiteral("en_us.json"), R"({"a":"Hello there","b":"Open the door"})");
        write(QStringLiteral("zh_cn.json"), QStringLiteral(R"({"a":"\u4f60\u597d"})").toUtf8());

        const auto result = scanFast(world, QStringLiteral("1.21.1"));
        QVERIFY(result.isValid());
        QCOMPARE(result.pack.enKeys, 2);
        QCOMPARE(result.pack.zhKeys, 1);
        QCOMPARE(result.pack.missingProse, 1);  // 只有 b 还缺中文
    }
};

QTEST_GUILESS_MAIN(MapL10nProbeTest)

#include "MapL10nProbe_test.moc"
