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
#include <QSet>
#include <QStringList>
#include <QTest>

#include "maps/ModPresets.h"

/// 只测不依赖 APPLICATION（数据目录）的部分：内置预设本身和加载器匹配规则
class ModPresetsTest : public QObject {
    Q_OBJECT

   private slots:
    void builtinPresetsAreUsable()
    {
        const auto presets = maps::ModPresets::builtin();
        QVERIFY(!presets.isEmpty());

        const QStringList allowedLoaders{ QStringLiteral("fabric"), QStringLiteral("forge"),
                                          QStringLiteral("neoforge"), QStringLiteral("quilt"),
                                          QStringLiteral("any") };

        QSet<QString> ids;
        for (const auto& preset : presets) {
            QVERIFY2(preset.isValid(), qPrintable(preset.id));
            QVERIFY2(allowedLoaders.contains(preset.loader), qPrintable(preset.loader));
            QVERIFY2(!ids.contains(preset.id), qPrintable(preset.id));
            ids.insert(preset.id);

            for (const auto& mod : preset.mods) {
                QVERIFY2(!mod.project.isEmpty(), qPrintable(preset.id));
                // slug 里不会有空格；有空格通常是把显示名误填进了 project
                QVERIFY2(!mod.project.contains(QLatin1Char(' ')), qPrintable(mod.project));
                // 每个内置条目都要有一句人话说明，否则表格里只有名字
                QVERIFY2(!mod.note.isEmpty(), qPrintable(mod.project));
                QCOMPARE(mod.provider, QStringLiteral("modrinth"));
            }
        }
    }

    void loaderMatching_data()
    {
        QTest::addColumn<QString>("presetLoader");
        QTest::addColumn<QString>("instanceLoader");
        QTest::addColumn<bool>("matches");

        QTest::newRow("any matches fabric") << "any" << "fabric" << true;
        QTest::newRow("any matches vanilla") << "any" << "" << true;
        QTest::newRow("fabric matches fabric") << "fabric" << "fabric" << true;
        QTest::newRow("fabric matches quilt") << "fabric" << "quilt" << true;
        QTest::newRow("fabric not forge") << "fabric" << "forge" << false;
        QTest::newRow("forge matches neoforge") << "forge" << "neoforge" << true;
        QTest::newRow("neoforge not forge") << "neoforge" << "forge" << false;
        QTest::newRow("forge not fabric") << "forge" << "fabric" << false;
    }

    void searchHits_data()
    {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<int>("rows");

        // Modrinth 的 /v2/search 返回 { hits: [...] }，不是裸数组 —— 这里专门钉住这个结构
        QTest::newRow("modrinth") << "modrinth"
                                  << QByteArray(R"({"hits":[{"project_id":"AABB","slug":"sodium","title":"Sodium","description":"rendering","author":"jelly","downloads":1234}],"offset":0,"limit":10,"total_hits":1})")
                                  << 1;

        QTest::newRow("modrinth empty") << "modrinth"
                                        << QByteArray(R"({"hits":[],"offset":0,"limit":10,"total_hits":0})") << 0;

        // CurseForge 返回 { data: [...] }
        QTest::newRow("curseforge") << "curseforge"
                                    << QByteArray(R"({"data":[{"id":394468,"name":"Sodium","summary":"rendering","downloadCount":999,"authors":[{"name":"jelly"}]}]})")
                                    << 1;
    }

    void searchHits()
    {
        QFETCH(QString, provider);
        QFETCH(QByteArray, payload);
        QFETCH(int, rows);

        const auto hits = maps::ModSearchTask::parseHits(provider, payload);
        QCOMPARE(hits.size(), rows);
        if (rows == 0)
            return;

        QCOMPARE(hits.first().provider, provider);
        QVERIFY(!hits.first().project.isEmpty());
        QVERIFY(!hits.first().name.isEmpty());
        QVERIFY(!hits.first().author.isEmpty());
    }

    void loaderMatching()
    {
        QFETCH(QString, presetLoader);
        QFETCH(QString, instanceLoader);
        QFETCH(bool, matches);

        QCOMPARE(maps::ModPresets::matchesLoader(presetLoader, instanceLoader), matches);
    }
};

QTEST_GUILESS_MAIN(ModPresetsTest)

#include "ModPresets_test.moc"
