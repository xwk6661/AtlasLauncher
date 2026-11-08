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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>

#include <maps/AgentBootstrap.h>
#include <maps/BrowserFetch.h>
#include <maps/HallAPI.h>
#include <maps/DataVersionTable.h>
#include <maps/MapArchiveScanner.h>
#include <maps/MapCoverLoader.h>
#include <maps/MapDiskCleanup.h>
#include <maps/MapImportTask.h>
#include <maps/MapLaunchPrep.h>
#include <maps/MapMeta.h>
#include <maps/MapNotes.h>
#include <maps/MapVersionDetector.h>

using namespace maps;

namespace {

/**
 * 夹具目录解析顺序：
 *   1. 环境变量 PRISM_MAP_FIXTURES（CI / 本地指到任意样本目录）
 *   2. 与 AIO 参考项目并排的默认位置
 */
QString fixturesDir()
{
    const QString fromEnv = qEnvironmentVariable("PRISM_MAP_FIXTURES");
    if (!fromEnv.isEmpty() && QDir(fromEnv).exists())
        return fromEnv;

    const QString sibling = QFINDTESTDATA("../../../Minecraft-AIO/launcher/fixtures");
    if (!sibling.isEmpty())
        return sibling;

    return {};
}

}  // namespace

class MapArchiveScanTest : public QObject {
    Q_OBJECT

   private slots:
    // 注意：这里**不**做 Q_INIT_RESOURCE —— dataversion 表必须能在真实 App 里自加载。
    // DataVersionTable::instance() 内部会显式初始化 qrc（静态库不会自动注册），
    // 这个测试就是它的证据：去掉手动初始化后仍然要能读到 483 个版本。

    void test_dataVersionTable()
    {
        const auto& table = DataVersionTable::instance();
        QVERIFY2(table.isLoaded(), "dataversion.json 未随资源打包");
        QVERIFY(table.isKnownVersion(QStringLiteral("1.20.1")));
        QCOMPARE(table.versionForDataVersion(3465), QStringLiteral("1.20.1"));
        QVERIFY(DataVersionTable::supportsQuickPlay(QStringLiteral("1.20.1")));
        QVERIFY(!DataVersionTable::supportsQuickPlay(QStringLiteral("1.16.5")));
        QCOMPARE(DataVersionTable::javaMajorFor(QStringLiteral("1.16.5")), 8);
        QCOMPARE(DataVersionTable::javaMajorFor(QStringLiteral("1.17.1")), 16);
        QCOMPARE(DataVersionTable::javaMajorFor(QStringLiteral("1.20.4")), 17);
        QCOMPARE(DataVersionTable::javaMajorFor(QStringLiteral("1.21.1")), 21);
    }

    void test_normalizeVersion()
    {
        using Detector = MapVersionDetector;

        QCOMPARE(Detector::normalizeVersion(QStringLiteral("1.19.2")).value, QStringLiteral("1.19.2"));
        QCOMPARE(Detector::normalizeVersion(QStringLiteral("\u00a7b\u00a7l1.19.2")).value, QStringLiteral("1.19.2"));
        QCOMPARE(Detector::normalizeVersion(QStringLiteral("1.12.2-forge-14.23.5")).value, QStringLiteral("1.12.2"));
        QCOMPARE(Detector::normalizeVersion(QStringLiteral("1.20.0")).value, QStringLiteral("1.20"));
        QCOMPARE(Detector::normalizeVersion(QStringLiteral("  1.16.5  ")).value, QStringLiteral("1.16.5"));
        QCOMPARE(Detector::normalizeVersion(QString()).kind, QStringLiteral("unknown"));
        QCOMPARE(Detector::normalizeVersion(QStringLiteral("23w14a")).kind, QStringLiteral("snapshot"));
    }

    /// M2 前置：Agent jar 与主流版本的离线符号表必须在资源里，且能拼出合法参数
    void test_agentAssets()
    {
        QVERIFY2(AgentBootstrap::hasAgentJar(), "resources/agents/quickboot.jar 没打进资源");

        const QStringList versions{ QStringLiteral("1.7.10"),  QStringLiteral("1.8.9"),
                                    QStringLiteral("1.12"),    QStringLiteral("1.12.2"),
                                    QStringLiteral("1.14.4"),  QStringLiteral("1.15.2"),
                                    QStringLiteral("1.16.5"),  QStringLiteral("1.17.1"),
                                    QStringLiteral("1.18.2"),  QStringLiteral("1.19.2"),
                                    QStringLiteral("1.19.4"),  QStringLiteral("1.21.1") };
        for (const auto& version : versions) {
            QVERIFY2(AgentBootstrap::hasSymbols(version), qPrintable(QStringLiteral("缺 %1 的符号表").arg(version)));

            QString error;
            const auto table = AgentBootstrap::loadSymbols(version, &error);
            QVERIFY2(table.has_value(), qPrintable(QStringLiteral("%1：%2").arg(version, error)));
            QVERIFY2(!table->classes.isEmpty() && !table->methods.isEmpty(), qPrintable(version));

            AgentBootstrap::AgentOptions options;
            options.worldFolder = QStringLiteral("MapWorld");
            options.enterDelay = 1;
            options.agentLogPath = QStringLiteral("D:/tmp/agent.log");
            const auto args = AgentBootstrap::buildAgentArgs(*table, options);
            QVERIFY2(!args.args.isEmpty(), qPrintable(QStringLiteral("%1：%2").arg(version, args.skipReason)));
            QVERIFY(args.args.contains(QStringLiteral("world=MapWorld")));
            QVERIFY(args.args.contains(QStringLiteral("menu=")));
            QVERIFY(args.args.contains(QStringLiteral("mc=")));
            QVERIFY(args.args.contains(QStringLiteral("launch=")));
            QVERIFY(args.args.contains(QStringLiteral("haltOnSafe=1")));
            // 某些版本解析不出 setLevel，这时不能传 loadWorld，也不能声称能请求退出
            if (!args.canRequestQuit)
                QVERIFY(!args.args.contains(QStringLiteral("loadWorld=")));
        }
    }

    /// 启动期磁盘碎片清理：清理 *.part.parallel、私有 .part-* 以及超过过期天数的 *.part 文件
    void test_diskCleanup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString p1 = FS::PathCombine(dir.path(), "client.jar.part.parallel");
        const QString p2 = FS::PathCombine(dir.path(), "meta.json.part-4f2a");
        const QString p3 = FS::PathCombine(dir.path(), "active.part");
        const QString p4 = FS::PathCombine(dir.path(), "stale.part");

        QFile f1(p1);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write("parallel");
        f1.close();

        QFile f2(p2);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write("private");
        f2.close();

        QFile f3(p3);
        QVERIFY(f3.open(QIODevice::WriteOnly));
        f3.write("active");
        f3.close();

        QFile f4(p4);
        QVERIFY(f4.open(QIODevice::WriteOnly));
        f4.write("stale");
        f4.close();

        // 重新打开并设 stale.part 的修改时间为 5 天前
        QVERIFY(f4.open(QIODevice::ReadWrite));
        QVERIFY(f4.setFileTime(QDateTime::currentDateTime().addDays(-5), QFileDevice::FileModificationTime));
        f4.close();

        const auto stats = MapDiskCleanup::cleanDirectoryTree(dir.path(), 3);
        QCOMPARE(stats.removedParallelPartFiles, 2);  // p1 and p2
        QCOMPARE(stats.removedStalePartFiles, 1);     // p4
        QVERIFY(!QFile::exists(p1));
        QVERIFY(!QFile::exists(p2));
        QVERIFY(!QFile::exists(p4));
        QVERIFY(QFile::exists(p3));  // active.part 未过期被保留
    }

    /// Agent 参数：三个设置项必须真的落到参数上（关掉自动进图也要注入，落盘判定要用）
    void test_agentOptions()
    {
        QString error;
        const auto table = AgentBootstrap::loadSymbols(QStringLiteral("1.16.5"), &error);
        QVERIFY2(table.has_value(), qPrintable(error));

        AgentBootstrap::AgentOptions base;
        base.worldFolder = QStringLiteral("MapWorld");

        // 默认：确认落盘后自己退出，且不带 autoEnter=0
        const auto defaults = AgentBootstrap::buildAgentArgs(*table, base);
        QVERIFY(defaults.args.contains(QStringLiteral("haltOnSafe=1")));
        QVERIFY(!defaults.args.contains(QStringLiteral("autoEnter=0")));

        // 关掉「退出即关」：不能 halt，否则玩家回不到标题画面
        auto noClose = base;
        noClose.closeOnQuit = false;
        QVERIFY(AgentBootstrap::buildAgentArgs(*table, noClose).args.contains(QStringLiteral("haltOnSafe=0")));

        // 关掉「自动跳过主菜单」：仍然注入，但明确告诉 Agent 别替玩家进图
        auto noEnter = base;
        noEnter.autoEnter = false;
        QVERIFY(AgentBootstrap::buildAgentArgs(*table, noEnter).args.contains(QStringLiteral("autoEnter=0")));

        // 延迟会被钳到 0..60，防止手滑配出 -1 或 999
        auto slow = base;
        slow.enterDelay = 999;
        QVERIFY(AgentBootstrap::buildAgentArgs(*table, slow).args.contains(QStringLiteral("enterDelay=60")));

        // 日志路径里的逗号会破坏 Agent 的参数切分，必须被剔除
        auto withComma = base;
        withComma.agentLogPath = QStringLiteral("D:/a,b/agent.log");
        QVERIFY(!AgentBootstrap::buildAgentArgs(*table, withComma).args.contains(QStringLiteral("a,b")));
    }

    /// Agent 事件解析（不依赖游戏进程）
    void test_agentEvents()
    {
        int sequence = 0;
        QCOMPARE(AgentBootstrap::parseEvent(QStringLiteral("[mcml-agent] ENTER_WORLD_OK MapWorld"), &sequence),
                 AgentBootstrap::Event::EnteredWorld);
        QCOMPARE(AgentBootstrap::parseEvent(QStringLiteral("[mcml-agent] SAVE_DONE #3"), &sequence),
                 AgentBootstrap::Event::SaveDone);
        QCOMPARE(sequence, 3);
        QCOMPARE(AgentBootstrap::parseEvent(QStringLiteral("[mcml-agent] EXIT_SAFE"), &sequence),
                 AgentBootstrap::Event::ExitSafe);
        QCOMPARE(AgentBootstrap::parseEvent(QStringLiteral("[mcml-agent] EXIT_UNSAFE_TIMEOUT"), &sequence),
                 AgentBootstrap::Event::ExitUnsafeTimeout);
        // 普通游戏日志不能误判
        QCOMPARE(AgentBootstrap::parseEvent(QStringLiteral("[Render thread/INFO] Stopping!"), &sequence),
                 AgentBootstrap::Event::Unknown);
    }

    /// 退出判定：以 agent.log 里**最后一条**标记为准，没有标记才用启动器判定兜底
    void test_exitStatus()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString logPath = QDir(dir.path()).filePath(QStringLiteral("agent.log"));

        const auto writeLog = [&logPath](const QString& text) {
            QFile file(logPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return file.write(text.toUtf8()) == text.toUtf8().size();
        };

        // 没有 agent.log（quickPlay 轨道根本不注入）：只能靠启动器的崩溃判定
        QVERIFY(!QFile::exists(logPath));
        QCOMPARE(MapLaunchPrep::detectExitStatus(dir.path()), QStringLiteral("unknown"));
        QCOMPARE(MapLaunchPrep::resolveExitStatus(dir.path(), false), QStringLiteral("safe"));
        QCOMPARE(MapLaunchPrep::resolveExitStatus(dir.path(), true), QStringLiteral("crash"));

        // Agent 给了结论就用它，哪怕启动器认为崩了
        QVERIFY(writeLog(QStringLiteral("[mcml-agent] ENTER_WORLD_OK MapWorld\n"
                                        "[mcml-agent] SAVE_DONE #2 (quit save)\n"
                                        "[mcml-agent] EXIT_SAFE\n")));
        QCOMPARE(MapLaunchPrep::detectExitStatus(dir.path()), QStringLiteral("safe"));
        QCOMPARE(MapLaunchPrep::resolveExitStatus(dir.path(), true), QStringLiteral("safe"));

        QVERIFY(writeLog(QStringLiteral("[mcml-agent] EXIT_UNSAFE_TIMEOUT\n")));
        QCOMPARE(MapLaunchPrep::detectExitStatus(dir.path()), QStringLiteral("crash"));

        // 先超时、后落盘成功：以最后一条为准（只判断 contains 会把结论写反）
        QVERIFY(writeLog(QStringLiteral("[mcml-agent] EXIT_UNSAFE_TIMEOUT\n"
                                        "[mcml-agent] SAVE_DONE #3\n"
                                        "[mcml-agent] EXIT_SAFE\n")));
        QCOMPARE(MapLaunchPrep::detectExitStatus(dir.path()), QStringLiteral("safe"));

        // 启动前必须能清干净，否则本次崩溃会被上一次的 EXIT_SAFE 覆盖成「正常落盘」
        MapLaunchPrep::clearAgentLog(dir.path());
        QVERIFY(!QFile::exists(logPath));
        QCOMPARE(MapLaunchPrep::detectExitStatus(dir.path()), QStringLiteral("unknown"));
    }

    /// 玩家自己的数据（notes.json）：与 map.json 分开存，越界值要钳住
    void test_mapNotes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 没写过 = 空对象，不是错误；显示名回落到传入的原名
        const auto empty = MapNotes::load(dir.path());
        QVERIFY(empty.isEmpty());
        QCOMPARE(empty.displayName(QStringLiteral("原名")), QStringLiteral("原名"));
        QVERIFY(empty.ratingText().isEmpty());

        MapNotes notes;
        notes.rating = 7;  // 半星粒度：3.5 星
        notes.alias = QStringLiteral("  盘灵古域  ");
        notes.note = QStringLiteral("第三章卡在传送门");
        QString error;
        QVERIFY2(notes.save(dir.path(), &error), qPrintable(error));

        const auto loaded = MapNotes::load(dir.path());
        QCOMPARE(loaded.rating, 7);
        QCOMPARE(loaded.alias, QStringLiteral("盘灵古域"));  // 去空白
        QCOMPARE(loaded.note, notes.note);
        QVERIFY(loaded.updatedAt.isValid());
        QCOMPARE(loaded.ratingText(), QStringLiteral("3.5 / 5"));
        // 中文名优先于导入时识别出的标题
        QCOMPARE(loaded.displayName(QStringLiteral("Ξ Torre de Piedra Ξ")), QStringLiteral("盘灵古域"));

        // 坏数据不能写进界面：评分钳到 10、中文名 40、笔记 2000
        MapNotes bad;
        bad.rating = 99;
        bad.alias = QString(60, QLatin1Char('x'));
        bad.note = QString(MapNotes::MAX_NOTE + 50, QLatin1Char('y'));
        QVERIFY(bad.save(dir.path()));
        const auto clamped = MapNotes::load(dir.path());
        QCOMPARE(clamped.rating, MapNotes::MAX_RATING);
        QCOMPARE(clamped.alias.size(), MapNotes::MAX_ALIAS);
        QCOMPARE(clamped.note.size(), MapNotes::MAX_NOTE);
    }

    /**
     * 旧 map.json 的「需要整合包」误判必须被钳掉。
     *
     * 现场（2026-11-01，Torre de Piedra 1.21.1）：纯原版地图，但作者的客户端开了
     * Fabric（level.dat 里 ServerBrands=["fabric"]、WasModded=1），旧版导入逻辑把
     * 它当成"缺整合包"，于是原版地图也被挂上警告。判据只能是 Forge 的
     * FML LoadingModList（= requiredMods）。
     */
    void test_legacyModpackFlag()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 旧数据：只有布尔标记，没有 requiredMods → 读回来必须是 false
        QJsonObject legacy;
        legacy.insert(QStringLiteral("schema"), 1);
        legacy.insert(QStringLiteral("title"), QStringLiteral("Torre de Piedra"));
        legacy.insert(QStringLiteral("requiresExternalModpack"), true);
        QJsonObject version;
        version.insert(QStringLiteral("mcVersion"), QStringLiteral("1.21.1"));
        version.insert(QStringLiteral("confidence"), QStringLiteral("HIGH"));
        legacy.insert(QStringLiteral("version"), version);

        QFile file(FS::PathCombine(dir.path(), MapMeta::fileName()));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(legacy).toJson());
        file.close();

        const auto loaded = MapMeta::load(dir.path());
        QVERIFY(!loaded.requiresExternalModpack);

        // 真的有模组依赖：标记保留
        legacy.insert(QStringLiteral("requiredMods"), QJsonArray{ QStringLiteral("CustomNPCs@1.7.10") });
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(legacy).toJson());
        file.close();

        const auto withMods = MapMeta::load(dir.path());
        QVERIFY(withMods.requiresExternalModpack);
    }

    /// map.json 往返（写出去再读回来，字段不能丢）
    void test_mapMetaRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();

        MapMeta meta;
        meta.title = QStringLiteral("盘灵古域");
        meta.archive.path = QStringLiteral("E:/maps/panilla.rar");
        meta.archive.size = 734003200;
        meta.version.mcVersion = QStringLiteral("1.7.10");
        meta.version.confidence = Confidence::Low;
        meta.version.decidedBy = QStringLiteral("user");
        meta.version.evidence = { QStringLiteral("Data.version：Anvil"), QStringLiteral("文件名：1.7.10") };
        meta.loader.type = QStringLiteral("forge");
        meta.loader.version = QStringLiteral("10.13.4.1614");
        meta.worlds.append({ QStringLiteral("MapWorld"), QStringLiteral("解密地图v2/saves/MyWorld") });
        meta.packs = { QStringLiteral("resourcepacks/材质包.zip") };
        meta.mods = { QStringLiteral("mods/CustomNPCs.jar") };
        meta.experimentalFeatures = { QStringLiteral("minecraft:update_1_21") };
        meta.requiredMods = { QStringLiteral("CustomNPCs@1.7.10"), QStringLiteral("bibliocraft@1.9.5") };
        meta.requiresExternalModpack = true;
        meta.stats.playSeconds = 3420;
        meta.stats.lastExit = QStringLiteral("safe");
        meta.importedAt = QDateTime::currentDateTimeUtc();

        QString error;
        QVERIFY2(meta.save(root, &error), qPrintable(error));

        const auto loaded = MapMeta::load(root, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.title, meta.title);
        QCOMPARE(loaded.archive.size, meta.archive.size);
        QCOMPARE(loaded.version.mcVersion, QStringLiteral("1.7.10"));
        QCOMPARE(loaded.version.confidence, Confidence::Low);
        QCOMPARE(loaded.version.decidedBy, QStringLiteral("user"));
        QCOMPARE(loaded.version.evidence.size(), 2);
        QCOMPARE(loaded.loader.type, QStringLiteral("forge"));
        QCOMPARE(loaded.worlds.size(), 1);
        QCOMPARE(loaded.worlds.first().folder, QStringLiteral("MapWorld"));
        QCOMPARE(loaded.packs.size(), 1);
        QCOMPARE(loaded.mods.size(), 1);
        QCOMPARE(loaded.experimentalFeatures, QStringList{ QStringLiteral("minecraft:update_1_21") });
        QCOMPARE(loaded.requiredMods.size(), 2);
        QCOMPARE(loaded.requiredMods.first(), QStringLiteral("CustomNPCs@1.7.10"));
        QVERIFY(loaded.requiresExternalModpack);
        QCOMPARE(loaded.stats.playSeconds, 3420);
        QCOMPARE(loaded.stats.lastExit, QStringLiteral("safe"));
    }

    /// 大厅评论总数解析（PostgREST 的 content-range）
    void test_contentRangeParsing()
    {
        QCOMPARE(HallAPI::totalFromContentRange("0-7/123"), 123);
        QCOMPARE(HallAPI::totalFromContentRange("0-5/6"), 6);
        // 总数为未知时站方给 `*`，必须当成「不知道」而不是 0
        QCOMPARE(HallAPI::totalFromContentRange("0-7/*"), -1);
        QCOMPARE(HallAPI::totalFromContentRange(""), -1);
    }

    /**
     * 大厅封面：浏览器通道的端到端用例（要联网 + 本机有 Edge/Chrome，默认跳过）。
     *
     * 覆盖的是**生产路径**：生成临时页面 → 起无头浏览器 → canvas 转 JPEG → 从
     * `--dump-dom` 的 `<pre>` 里取回 base64 → 落盘 →  emit coverReady。
     * `*.supabase.co` 在本机被 SNI 阻断，Qt 直连必然失败，所以这里强制走 browser。
     *
     * 跑法：
     *   set PRISM_TEST_COVER_URL=https://qtxjbukjyuylqjgiphxs.supabase.co/storage/v1/object/public/map_images/xxx.jpg
     * 多个地址用分号分隔，可以顺便验证「一个浏览器进程批量取图」。
     */
    void test_coverLoaderBrowser()
    {
        const QString raw = qEnvironmentVariable("PRISM_TEST_COVER_URL");
        if (raw.isEmpty())
            QSKIP("未设置 PRISM_TEST_COVER_URL：跳过联网取图用例");

        QStringList urls;
        for (const auto& part : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const auto trimmed = part.trimmed();
            if (!trimmed.isEmpty())
                urls.append(trimmed);
        }
        QVERIFY(!urls.isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        auto* loader = MapCoverLoader::instance();
        loader->setCacheDir(dir.filePath(QStringLiteral("covers")));
        loader->setTransportMode(QStringLiteral("browser"));

        QSignalSpy ready(loader, &MapCoverLoader::coverReady);
        QSignalSpy failed(loader, &MapCoverLoader::coverFailed);

        for (const auto& url : urls) {
            QVERIFY(loader->cached(url).isNull());
            loader->request(url);
        }

        QTRY_VERIFY_WITH_TIMEOUT(ready.count() + failed.count() >= urls.size(), 120000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(ready.count(), urls.size());

        for (int i = 0; i < ready.count(); ++i) {
            const auto image = ready.at(i).at(1).value<QImage>();
            QVERIFY(!image.isNull());
            QVERIFY2(image.width() >= 300, qPrintable(QStringLiteral("缩略图太窄：%1").arg(image.width())));
        }

        // 落盘之后第二次请求应该直接命中缓存，不再开浏览器
        const auto entries = QDir(dir.filePath(QStringLiteral("covers"))).entryList({ QStringLiteral("*.jpg") });
        QCOMPARE(entries.size(), urls.size());
        for (const auto& url : urls)
            QVERIFY(!loader->cached(url).isNull());
    }

    /**
     * 浏览器通道取 JSON 的端到端用例（要联网 + 本机有 Edge/Chrome，默认跳过）。
     *
     * 覆盖 `BrowserFetch` 现在的做法：本地页面里 XHR + 退避重试，再 base64 回传。
     * 跑法：
     *   set PRISM_TEST_FETCH_URL=https://<ref>.supabase.co/rest/v1/maps?select=slug&limit=2&apikey=…
     */
    void test_browserChannelJson()
    {
        const QString url = qEnvironmentVariable("PRISM_TEST_FETCH_URL");
        if (url.isEmpty())
            QSKIP("未设置 PRISM_TEST_FETCH_URL：跳过浏览器通道用例");
        if (!BrowserFetch::available())
            QSKIP("本机没有 Edge / Chrome");

        BrowserFetch fetch;
        QSignalSpy done(&fetch, &BrowserFetch::finished);
        fetch.get(QUrl(url), 2);

        QTRY_VERIFY_WITH_TIMEOUT(done.count() > 0, 120000);
        const bool ok = done.first().at(0).toBool();
        const auto body = done.first().at(1).toByteArray();
        QVERIFY2(ok, qPrintable(done.first().at(2).toString()));
        QVERIFY2(body.size() > 2, "响应体是空的");

        const auto document = QJsonDocument::fromJson(body);
        QVERIFY2(document.isArray() || document.isObject(), "拿回来的不是 JSON");
    }

    /// M0 验收：AI0 的 23 个夹具逐个比对版本 / 置信度 / 世界数 / 材质包数
    void test_manifestFixtures()
    {
        const QString dir = fixturesDir();
        if (dir.isEmpty())
            QSKIP("找不到夹具目录，设置 PRISM_MAP_FIXTURES 指向 AIO 的 launcher/fixtures");

        QFile manifest(QDir(dir).filePath(QStringLiteral("manifest.json")));
        QVERIFY2(manifest.open(QIODevice::ReadOnly), "manifest.json 打不开");
        const auto entries = QJsonDocument::fromJson(manifest.readAll()).array();
        QVERIFY(!entries.isEmpty());

        QStringList failures;
        int checked = 0;

        for (const auto& value : entries) {
            const auto item = value.toObject();
            const QString file = item.value(QStringLiteral("file")).toString();
            const QString path = QDir(dir).filePath(file);
            if (!QFile::exists(path)) {
                failures.append(QStringLiteral("%1：文件缺失").arg(file));
                continue;
            }

            const auto scanned = MapArchiveScanner::scan(path);
            if (!scanned) {
                failures.append(QStringLiteral("%1：扫描失败 %2").arg(file, scanned.error()));
                continue;
            }

            const auto& result = *scanned;
            const QString expectVersion = item.value(QStringLiteral("expectVersion")).toString();
            const QString expectConfidence = item.value(QStringLiteral("expectConfidence")).toString();
            const int expectWorlds = item.value(QStringLiteral("expectWorlds")).toInt();
            const int expectPacks = item.value(QStringLiteral("expectResourcePacks")).toInt();

            if (result.worlds.size() != expectWorlds) {
                failures.append(QStringLiteral("%1：世界数 %2，期望 %3").arg(file).arg(result.worlds.size()).arg(expectWorlds));
                continue;
            }

            const int packs = result.resourcePacks().size();
            if (packs != expectPacks)
                failures.append(QStringLiteral("%1：材质包 %2，期望 %3").arg(file).arg(packs).arg(expectPacks));

            if (result.worlds.isEmpty())
                continue;

            const auto& version = result.worlds.first().version;
            if (version.mcVersion != expectVersion) {
                failures.append(QStringLiteral("%1：版本 %2，期望 %3").arg(file, version.mcVersion, expectVersion));
            }
            if (confidenceToString(version.confidence) != expectConfidence) {
                failures.append(QStringLiteral("%1：置信度 %2，期望 %3")
                                    .arg(file, confidenceToString(version.confidence), expectConfidence));
            }

            ++checked;
        }

        qInfo().noquote() << QStringLiteral("夹具回归：比对 %1 个，失败 %2 项").arg(checked).arg(failures.size());
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }
};

QTEST_GUILESS_MAIN(MapArchiveScanTest)

#include "MapArchiveScan_test.moc"
