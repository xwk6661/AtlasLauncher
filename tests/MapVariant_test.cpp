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
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "maps/HallMatcher.h"
#include "maps/MapLibrary.h"
#include "maps/MapVariant.h"

using maps::variant::Delivery;
using maps::variant::Kind;
using maps::variant::Language;
using maps::variant::Source;

/// 只测不依赖 APPLICATION 的纯函数：文件名推断、归一化、分组键、大厅候选打分
class MapVariantTest : public QObject {
    Q_OBJECT

   private slots:
    void inferVariant_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("note");
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("language");
        QTest::addColumn<int>("source");
        QTest::addColumn<QString>("label");

        // 真实库里的 6 组变体 + 两张没有语言标注的
        QTest::newRow("简中汉化") << QStringLiteral("[1.16.5]Bitstream World 1.16.5（简中汉化v1.0）.zip") << QString()
                                  << int(Kind::Localized) << int(Language::SimplifiedChinese) << int(Source::Community)
                                  << QStringLiteral("简中汉化v1.0");
        QTest::newRow("繁中") << QStringLiteral("[1.8.9]Ragecraft III - The Prophecy （繁中）.rar") << QString()
                              << int(Kind::Localized) << int(Language::TraditionalChinese) << int(Source::Unknown)
                              << QStringLiteral("繁中");
        QTest::newRow("汉化无版本号")
            << QStringLiteral("[1.20.4]Mapwreck VII - Subterranean v1.4 破碎领域7·地下秘境（汉化v1.0）.zip") << QString()
            << int(Kind::Localized) << int(Language::SimplifiedChinese) << int(Source::Community)
            << QStringLiteral("汉化v1.0");
        QTest::newRow("官中") << QStringLiteral("[1.16.5]Temple World（官中）.zip") << QString()
                              << int(Kind::Localized) << int(Language::SimplifiedChinese) << int(Source::Author)
                              << QStringLiteral("官中");
        QTest::newRow("英文原版") << QStringLiteral("[1.8.9]Ragecraft III - The Prophecy.zip") << QString()
                                  << int(Kind::Original) << int(Language::Unknown) << int(Source::Unknown) << QString();
        QTest::newRow("无标注") << QStringLiteral("[1.21.1]Torre de Piedra v1.2 石之恐惧.zip") << QString()
                                << int(Kind::Original) << int(Language::Unknown) << int(Source::Unknown) << QString();
        // 备注里写着「与繁中版同图不同语言」，不能把英文原版判成繁中
        QTest::newRow("备注提到别的语言")
            << QStringLiteral("[1.8.9]Ragecraft III - The Prophecy.zip")
            << QStringLiteral("英文原版；与繁中版同图不同语言") << int(Kind::Original) << int(Language::Unknown)
            << int(Source::Unknown) << QString();
        // 自制：备注能补来源，但语言仍只认文件名
        QTest::newRow("自译") << QStringLiteral("[1.21.1]Torre de Piedra v1.2 石之恐惧（简中汉化v1.0）.zip")
                              << QStringLiteral("自制汉化") << int(Kind::Localized)
                              << int(Language::SimplifiedChinese) << int(Source::Self)
                              << QStringLiteral("简中汉化v1.0");
    }

    void inferVariant()
    {
        QFETCH(QString, file);
        QFETCH(QString, note);
        QFETCH(int, kind);
        QFETCH(int, language);
        QFETCH(int, source);
        QFETCH(QString, label);

        const auto info = maps::variant::inferFromFileName(file, note);
        QCOMPARE(int(info.kind), kind);
        QCOMPARE(int(info.language), language);
        QCOMPARE(int(info.source), source);
        QCOMPARE(info.label, label);
        QCOMPARE(int(info.delivery), int(Delivery::Archive));
    }

    void normalizeName_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("带版本与扩展名") << QStringLiteral("[1.16.5]Final Paradox v1.1.15.zip")
                                        << QStringLiteral("final paradox");
        QTest::newRow("繁中后缀") << QStringLiteral("[1.8.9]Ragecraft III - The Prophecy （繁中）.rar")
                                  << QStringLiteral("ragecraft iii prophecy");
        QTest::newRow("作者署名") << QStringLiteral("Bitstream - by Gibbs & Asometric")
                                  << QStringLiteral("bitstream gibbs asometric");
        QTest::newRow("保留章节号") << QStringLiteral("Untold Stories 09 - Castlemania V1.0.5")
                                    << QStringLiteral("untold stories 09 castlemania");
        QTest::newRow("MC 版本 token") << QStringLiteral("[1.21.9]Untold Stories - Lacuna Sanctum - MC1.21.9.zip")
                                       << QStringLiteral("untold stories lacuna sanctum");
        QTest::newRow("纯中文") << QStringLiteral("[1.8.9]Panilla 1.2.3 盤靈古域.rar") << QStringLiteral("panilla 盤靈古域");
    }

    void normalizeName()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(maps::variant::normalizeName(input), expected);
    }

    void groupKeyPrefersHallSlug()
    {
        maps::LibraryItem item;
        item.file = QStringLiteral("[1.16.5]Final Paradox v1.1.15.zip");
        item.englishName = QStringLiteral("Final Paradox");
        QCOMPARE(item.groupKey(), QStringLiteral("local:final paradox"));

        // 有关联就以 slug 为准：之后改名、换机器都还在同一组
        item.hallSlug = QStringLiteral("final_paradox");
        QCOMPARE(item.groupKey(), QStringLiteral("hall:final_paradox"));

        // 没有英文名就退回中文名
        maps::LibraryItem chinese;
        chinese.file = QStringLiteral("[1.8.9]Panilla 1.2.3 盤靈古域.rar");
        chinese.chineseName = QStringLiteral("盘灵古域");
        QCOMPARE(chinese.groupKey(), QStringLiteral("local:盘灵古域"));
    }

    void rankPrefersTheSameMap()
    {
        QJsonArray rows;
        rows.append(row(QStringLiteral("torre_de_piedra"), QStringLiteral("Torre de Piedra"), QStringLiteral("1.21.1")));
        rows.append(row(QStringLiteral("tower_and_prison"), QStringLiteral("Tower and Prison"), QStringLiteral("1.21.1")));
        rows.append(row(QStringLiteral("tower_of_sinner"), QStringLiteral("Tower of Sinner"), QStringLiteral("26.2")));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Torre de Piedra v1.2 石之恐惧");
        query.mcVersion = QStringLiteral("1.21.1");

        const auto candidates = maps::HallMatcher::rank(rows, query, 3);
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().slug, QStringLiteral("torre_de_piedra"));
        // 「Tower of …」跟它只共有一个词，不能挤到前面
        for (const auto& candidate : candidates) {
            if (candidate.slug.startsWith(QLatin1String("tower")))
                QVERIFY(candidate.score < candidates.first().score);
        }
    }

    void rankHandlesAuthorSuffix()
    {
        QJsonArray rows;
        rows.append(row(QStringLiteral("bitstream"), QStringLiteral("Bitstream"), QStringLiteral("1.16.5")));
        rows.append(row(QStringLiteral("bitstream_world"), QStringLiteral("Bitstream World"), QStringLiteral("1.16.5")));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Bitstream - by Gibbs & Asometric");
        query.mcVersion = QStringLiteral("1.16.5");

        const auto candidates = maps::HallMatcher::rank(rows, query, 2);
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().slug, QStringLiteral("bitstream"));
    }

    void rankPenalizesVersionMismatch()
    {
        QJsonArray rows;
        rows.append(row(QStringLiteral("torre_de_piedra"), QStringLiteral("Torre de Piedra"), QStringLiteral("1.21.9")));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Torre de Piedra");
        query.mcVersion = QStringLiteral("1.16.5");

        const auto candidates = maps::HallMatcher::rank(rows, query, 1);
        QVERIFY(!candidates.isEmpty());
        QVERIFY2(candidates.first().score < 0.8, qPrintable(QString::number(candidates.first().score)));
        QVERIFY(candidates.first().reason.contains(QStringLiteral("站上")));
    }

    void rankUsesSeriesAndNumber()
    {
        // 站上的真实结构：name 只有「Subterranean」，系列与编号在别的列
        QJsonArray rows;
        rows.append(row(QStringLiteral("subterranean"), QStringLiteral("Subterranean"), QStringLiteral("1.20.4"),
                        QStringLiteral("Mapwreck"), 7));
        rows.append(row(QStringLiteral("subterranean_blast"), QStringLiteral("Subterranean Blast"),
                        QStringLiteral("1.19.3")));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Mapwreck VII - Subterranean");
        query.mcVersion = QStringLiteral("1.20.4");

        const auto candidates = maps::HallMatcher::rank(rows, query, 2);
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().slug, QStringLiteral("subterranean"));
        QVERIFY2(candidates.first().reason.contains(QStringLiteral("第 7 作")), qPrintable(candidates.first().reason));
        QVERIFY(candidates.first().score >= 0.9);
    }

    void rankSeriesNumberMustMatch()
    {
        QJsonArray rows;
        rows.append(row(QStringLiteral("subterranean"), QStringLiteral("Subterranean"), QStringLiteral("1.20.4"),
                        QStringLiteral("Mapwreck"), 8));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Mapwreck VII - Subterranean");

        const auto candidates = maps::HallMatcher::rank(rows, query, 2);
        QVERIFY(!candidates.isEmpty());
        // 系列名对得上、编号不对 → 不能拿到"第 N 作"那一档
        QVERIFY2(candidates.first().score < 0.9, qPrintable(QString::number(candidates.first().score)));
        QVERIFY(!candidates.first().reason.contains(QStringLiteral("第 8 作")));
    }

    void rankDoesNotPenalizeUnnumberedOriginal()
    {
        // 站上把原作的 series_num 也填成 1，但本地名字里根本没写编号 —— 不能因此扣分
        QJsonArray rows;
        rows.append(row(QStringLiteral("divinitys_end"), QStringLiteral("Divinity's End"), QStringLiteral("1.19.2"),
                        QStringLiteral("Divinity's End"), 1));
        rows.append(row(QStringLiteral("divinitys_end_randomizer"), QStringLiteral("Divinity's End Randomizer"),
                        QStringLiteral("1.19.2")));

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Divinity's End");
        query.mcVersion = QStringLiteral("1.19.2");

        const auto candidates = maps::HallMatcher::rank(rows, query, 2);
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().slug, QStringLiteral("divinitys_end"));
        QVERIFY2(candidates.first().score >= 0.99, qPrintable(QString::number(candidates.first().score)));
    }

    void rankReturnsNothingWithoutInput()
    {
        QJsonArray rows;
        rows.append(row(QStringLiteral("x"), QStringLiteral("Something"), QStringLiteral("1.20.1")));
        QVERIFY(maps::HallMatcher::rank(rows, maps::HallMatchQuery{}, 3).isEmpty());

        maps::HallMatchQuery query;
        query.englishName = QStringLiteral("Totally Different Map");
        QVERIFY(maps::HallMatcher::rank(QJsonArray{}, query, 3).isEmpty());
    }

    /**
     * 给真实库出报告（门控）。
     *
     * 设 `ATLAS_MAPLIB_DIR=<数据目录>` 才跑：读该目录下的 `maps/map-library.json`
     * 与 `maps/hall-cache.json`，逐条给出候选。报告用来判断哪些条目能自动关联、
     * 哪些必须人工确认。再给 `ATLAS_MAPLIB_REPORT=<路径>` 就写成 UTF-8 文件
     * （控制台是 GBK 时中文会乱码），否则直接打印。**只读索引，不改任何数据。**
     */
    void librarySuggestions()
    {
        const QString dir = qEnvironmentVariable("ATLAS_MAPLIB_DIR");
        if (dir.isEmpty())
            QSKIP("set ATLAS_MAPLIB_DIR=<data dir> to print suggestions for the real library");

        const QString index = dir + QStringLiteral("/maps/map-library.json");
        const QString cache = dir + QStringLiteral("/maps/hall-cache.json");

        const auto indexDocument = readJson(index);
        const auto cacheDocument = readJson(cache);
        QVERIFY2(indexDocument.isObject(), qPrintable(index));
        QVERIFY2(cacheDocument.isObject(), qPrintable(cache));

        const auto rows = cacheDocument.object().value("list").toArray();
        QVERIFY(!rows.isEmpty());

        const auto entries = indexDocument.object().value("entries").toArray();

        // 控制台在中文 Windows 上是 GBK，QINFO 里的中文会被编坏；给个落 UTF-8 的口子
        const QString reportPath = qEnvironmentVariable("ATLAS_MAPLIB_REPORT");
        QStringList report;
        for (const auto& value : entries) {
            const auto entry = value.toObject();
            const QString file = entry.value("file").toString();

            maps::HallMatchQuery query;
            query.englishName = entry.value("englishName").toString();
            query.chineseName = entry.value("chineseName").toString();
            query.fileName = file;
            query.mcVersion = entry.value("mcVersion").toString();

            const auto info = maps::variant::inferFromFileName(file, entry.value("note").toString());
            const auto candidates = maps::HallMatcher::rank(rows, query, 3);

            QString best = QStringLiteral("—");
            if (!candidates.isEmpty()) {
                const auto& top = candidates.first();
                best = QStringLiteral("%1 (%2) %3 · %4")
                           .arg(top.slug)
                           .arg(top.name)
                           .arg(top.score, 0, 'f', 2)
                           .arg(top.reason);
            }
            const QString line = QStringLiteral("%1\t%2\t%3").arg(file, info.summary(), best);
            if (reportPath.isEmpty())
                qInfo().noquote() << line;
            else
                report << line;
        }

        if (!reportPath.isEmpty()) {
            QFile out(reportPath);
            QVERIFY2(out.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(reportPath));
            out.write(report.join(QLatin1Char('\n')).toUtf8());
            out.write("\n");
        }
    }

   private:
    static QJsonObject row(const QString& slug, const QString& name, const QString& version,
                           const QString& series = {}, int seriesNumber = -1)
    {
        QJsonObject object;
        object.insert(QStringLiteral("slug"), slug);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("version"), version);
        if (!series.isEmpty())
            object.insert(QStringLiteral("series"), series);
        if (seriesNumber > 0)
            object.insert(QStringLiteral("series_num"), seriesNumber);
        return object;
    }

    static QJsonDocument readJson(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll());
    }
};

QTEST_GUILESS_MAIN(MapVariantTest)

#include "MapVariant_test.moc"
