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
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <maps/MapOptionsTxt.h>

using namespace maps::options;

/**
 * options.txt 的两代写法在 1.13 断过一次（数字键码 → key.keyboard.*，zh_CN → zh_cn）。
 *
 * 这组测试的一半价值在"判不出来就别写"：版本落在 1.8 却被写成 1.13 的格式，
 * 游戏会静默丢掉键位、玩家的蹲/跑回默认——比完全不写更糟。
 *
 * 期望值取自库里真实实例的 options.txt：
 *   Terra Restore(1.8.1)    key_key.sneak:29   key_key.sprint:42   lang:zh_CN
 *   Torre de Piedra(1.21.1) key_key.sneak:key.keyboard.left.control   …left.shift   lang:zh_cn
 *
 * 注意槽位不要和被测的自由函数同名：成员名一旦命中，非限定调用不会再走 ADL。
 */
class MapOptionsTxtTest : public QObject {
    Q_OBJECT

   private slots:
    void versionFormat_data()
    {
        QTest::addColumn<QString>("version");
        QTest::addColumn<int>("expected");  // -1 = nullopt

        QTest::newRow("1.7.10") << "1.7.10" << int(Format::Legacy);
        QTest::newRow("1.7.x 区间") << "1.7.x" << int(Format::Legacy);
        QTest::newRow("1.8.1") << "1.8.1" << int(Format::Legacy);
        QTest::newRow("1.8.9") << "1.8.9" << int(Format::Legacy);
        QTest::newRow("1.12") << "1.12" << int(Format::Legacy);
        QTest::newRow("1.12.2") << "1.12.2" << int(Format::Legacy);
        QTest::newRow("1.13 分水岭") << "1.13" << int(Format::Modern);
        QTest::newRow("1.16.5") << "1.16.5" << int(Format::Modern);
        QTest::newRow("1.21.1") << "1.21.1" << int(Format::Modern);
        QTest::newRow("26.2 年份版本号") << "26.2" << int(Format::Modern);
        QTest::newRow("空串") << "" << -1;
        QTest::newRow("unknown") << "unknown" << -1;
    }

    void versionFormat()
    {
        QFETCH(QString, version);
        QFETCH(int, expected);

        const auto detected = maps::options::formatForVersion(version);
        if (expected < 0) {
            QVERIFY2(!detected.has_value(), qPrintable(QStringLiteral("%1 不该判出写法").arg(version)));
            return;
        }
        QVERIFY2(detected.has_value(), qPrintable(QStringLiteral("%1 应该判出写法").arg(version)));
        QCOMPARE(int(*detected), expected);
    }

    void valuesMatchRealOptionsTxt()
    {
        // ≤1.12：29 = LWJGL2 的 KEY_LCONTROL，42 = KEY_LSHIFT
        QCOMPARE(sneakValue(Format::Legacy), QStringLiteral("29"));
        QCOMPARE(sprintValue(Format::Legacy), QStringLiteral("42"));
        QCOMPARE(chineseLanguage(Format::Legacy), QStringLiteral("zh_CN"));

        QCOMPARE(sneakValue(Format::Modern), QStringLiteral("key.keyboard.left.control"));
        QCOMPARE(sprintValue(Format::Modern), QStringLiteral("key.keyboard.left.shift"));
        QCOMPARE(chineseLanguage(Format::Modern), QStringLiteral("zh_cn"));
    }

    void writesFullSetIntoEmptyFile()
    {
        Defaults defaults;
        defaults.setChinese = true;
        defaults.swapSprintSneak = true;

        QStringList legacy;
        QCOMPARE(applyDefaults(legacy, Format::Legacy, defaults), 3);
        QCOMPARE(legacy,
                 QStringList({ QStringLiteral("lang:zh_CN"), QStringLiteral("key_key.sneak:29"),
                               QStringLiteral("key_key.sprint:42") }));

        QStringList modern;
        QCOMPARE(applyDefaults(modern, Format::Modern, defaults), 3);
        QCOMPARE(modern.at(0), QStringLiteral("lang:zh_cn"));
        QCOMPARE(modern.at(1), QStringLiteral("key_key.sneak:key.keyboard.left.control"));
        QCOMPARE(modern.at(2), QStringLiteral("key_key.sprint:key.keyboard.left.shift"));
    }

    void preservesUnrelatedLinesAndOrder()
    {
        QStringList lines{ QStringLiteral("version:2586"), QStringLiteral("fov:0.5"),
                           QStringLiteral("key_key.sneak:key.keyboard.left.shift"), QStringLiteral("guiScale:3") };

        Defaults defaults;
        defaults.setChinese = true;
        defaults.swapSprintSneak = true;
        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 3);

        // 原有行一个不丢；被改的那行留在原位，不搬到末尾
        QCOMPARE(lines.size(), 6);
        QCOMPARE(lines.at(0), QStringLiteral("version:2586"));
        QCOMPARE(lines.at(2), QStringLiteral("key_key.sneak:key.keyboard.left.control"));
        QCOMPARE(lines.at(3), QStringLiteral("guiScale:3"));
        QCOMPARE(lines.at(4), QStringLiteral("lang:zh_cn"));
        QCOMPARE(lines.at(5), QStringLiteral("key_key.sprint:key.keyboard.left.shift"));
    }

    void updatesExistingKeyInPlaceWithoutDuplicating()
    {
        QStringList lines{ QStringLiteral("lang:en_us"), QStringLiteral("key_key.sprint:key.keyboard.left.control") };

        Defaults defaults;
        defaults.setChinese = true;
        defaults.swapSprintSneak = true;
        applyDefaults(lines, Format::Modern, defaults);

        QCOMPARE(lines.size(), 3);  // lang / sprint 就地改，sneak 追加
        QCOMPARE(lines.at(0), QStringLiteral("lang:zh_cn"));
        QCOMPARE(lines.at(1), QStringLiteral("key_key.sprint:key.keyboard.left.shift"));
        QCOMPARE(lines.at(2), QStringLiteral("key_key.sneak:key.keyboard.left.control"));
    }

    void isIdempotent()
    {
        QStringList lines;
        Defaults defaults;
        defaults.setChinese = true;
        defaults.swapSprintSneak = true;

        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 3);
        const QStringList afterFirst = lines;
        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 0);
        QCOMPARE(lines, afterFirst);
    }

    void skipsWhenNoDefaultRequested()
    {
        QStringList lines{ QStringLiteral("lang:en_us") };
        QCOMPARE(applyDefaults(lines, Format::Modern, Defaults{}), 0);
        QCOMPARE(lines, QStringList({ QStringLiteral("lang:en_us") }));
    }

    void onlyLanguageTouchesNothingElse()
    {
        QStringList lines{ QStringLiteral("key_key.sneak:key.keyboard.left.shift") };
        Defaults defaults;
        defaults.setChinese = true;

        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 1);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0), QStringLiteral("key_key.sneak:key.keyboard.left.shift"));
        QCOMPARE(lines.at(1), QStringLiteral("lang:zh_cn"));
    }

    void detectFormatFromLines_data()
    {
        QTest::addColumn<QStringList>("lines");
        QTest::addColumn<int>("expected");

        QTest::newRow("数字键位") << QStringList({ QStringLiteral("key_key.sneak:29") }) << int(Format::Legacy);
        // 1.8 把鼠标键写成负数，一样是 LWJGL2 的键码
        QTest::newRow("负数码键位") << QStringList({ QStringLiteral("key_key.attack:-100") }) << int(Format::Legacy);
        QTest::newRow("名称键位")
            << QStringList({ QStringLiteral("key_key.sneak:key.keyboard.left.shift") }) << int(Format::Modern);
        QTest::newRow("鼠标键位")
            << QStringList({ QStringLiteral("key_key.attack:key.mouse.left") }) << int(Format::Modern);
        QTest::newRow("没有键位行") << QStringList({ QStringLiteral("fov:0.5") }) << -1;
        QTest::newRow("空文件") << QStringList() << -1;
    }

    void detectFormatFromLines()
    {
        QFETCH(QStringList, lines);
        QFETCH(int, expected);

        const auto detected = maps::options::detectFormat(lines);
        if (expected < 0) {
            QVERIFY(!detected.has_value());
            return;
        }
        QVERIFY(detected.has_value());
        QCOMPARE(int(*detected), expected);
    }

    /**
     * `version:` 行：缺了它游戏会整份丢弃 options.txt（NumberFormatException），
     * 首屏退回无障碍引导、quickPlay 进不去图。所以它必须在第一行，且不能被写成两行。
     */
    void versionStampGoesFirstOnlyForModern()
    {
        QStringList lines{ QStringLiteral("lang:zh_cn"), QStringLiteral("key_key.sneak:key.keyboard.left.control") };
        QVERIFY(putVersionStamp(lines, Format::Modern, 4556));
        QCOMPARE(lines.at(0), QStringLiteral("version:4556"));
        QCOMPARE(lines.size(), 3);

        // 幂等：再写一次不算改动，也不会多出一行
        QVERIFY(!putVersionStamp(lines, Format::Modern, 4556));
        QCOMPARE(lines.size(), 3);

        // 已经有了但要改值：搬到第一行，不留重复
        lines.append(QStringLiteral("fov:0.5"));
        lines.append(QStringLiteral("version:999"));
        // 上面那行是故意的（模拟被别家启动器拼过的文件）
        QVERIFY(putVersionStamp(lines, Format::Modern, 4556));
        QCOMPARE(lines.count(QStringLiteral("version:4556")), 1);
        QCOMPARE(lines.count(QStringLiteral("version:999")), 0);
        QCOMPARE(lines.at(0), QStringLiteral("version:4556"));

        // 1.8 里没有这个键：一行都不写
        QStringList legacy{ QStringLiteral("lang:zh_CN") };
        QVERIFY(!putVersionStamp(legacy, Format::Legacy, 4556));
        QCOMPARE(legacy.size(), 1);

        // 拿不到 DataVersion 时也不写（写错值比不写更糟）
        QStringList unknown{ QStringLiteral("lang:zh_cn") };
        QVERIFY(!putVersionStamp(unknown, Format::Modern, -1));
        QCOMPARE(unknown.size(), 1);
    }

    void onboardingIsTurnedOffWhenAsked()
    {
        QStringList lines{ QStringLiteral("fov:0.5") };
        Defaults defaults;
        defaults.skipAccessibilityOnboarding = true;

        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 1);
        QCOMPARE(lines.at(1), QStringLiteral("onboardAccessibility:false"));
        QCOMPARE(applyDefaults(lines, Format::Modern, defaults), 0);

        QStringList already{ QStringLiteral("onboardAccessibility:true") };
        QVERIFY(disableOnboarding(already));
        QCOMPARE(already, QStringList({ QStringLiteral("onboardAccessibility:false") }));
        QVERIFY(!disableOnboarding(already));
    }

    void repairFileFixesLegacyWrittenFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("options.txt"));

        // 导入流程曾经写过的东西：中文 + key.keyboard.*，但没有 version:
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("lang:zh_cn\n"
                       "key_key.sneak:key.keyboard.left.control\n"
                       "key_key.sprint:key.keyboard.left.shift\n"
                       "resourcePacks:[\"vanilla\"]\n");
        }

        const auto first = repairOptionsTxt(path, QStringLiteral("1.21.10"), 4556, true);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QVERIFY(first.changed);
        QVERIFY(first.version);
        QVERIFY(first.onboarding);

        QStringList lines;
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        QCOMPARE(lines.at(0), QStringLiteral("version:4556"));
        QVERIFY(lines.contains(QStringLiteral("lang:zh_cn")));
        QVERIFY(lines.contains(QStringLiteral("key_key.sneak:key.keyboard.left.control")));
        QVERIFY(lines.contains(QStringLiteral("onboardAccessibility:false")));

        // 幂等
        const auto second = repairOptionsTxt(path, QStringLiteral("1.21.10"), 4556, true);
        QVERIFY(second.error.isEmpty());
        QVERIFY(!second.changed);

        // 1.8 的实例不碰 version，但引导页同样关掉（1.8 不认这个键，多一行无害）
        const auto legacy = repairOptionsTxt(path, QStringLiteral("1.8.9"), 0, false);
        QVERIFY(legacy.error.isEmpty());
        QVERIFY(!legacy.version);
    }


    /// 从没被游戏读过的那份文件（没有 version:）：语言与键位一起补回来
    void repairRestoresDefaultsOnlyWhenNeverRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("options.txt"));

        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("lang:en_us\nkey_key.sneak:key.keyboard.left.shift\n");
        }

        Defaults restore;
        restore.setChinese = true;
        restore.swapSprintSneak = true;
        const auto result = repairOptionsTxt(path, QStringLiteral("1.21.10"), 4556, true, restore);
        QVERIFY(result.error.isEmpty());
        QVERIFY(!result.hadVersion);
        QVERIFY(result.changed);
        QVERIFY(result.defaults);

        QStringList lines;
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        QVERIFY(lines.contains(QStringLiteral("lang:zh_cn")));
        QVERIFY(lines.contains(QStringLiteral("key_key.sneak:key.keyboard.left.control")));

        // 已经带 version 的文件（玩家自己动过）不再补默认值
        Defaults restore2;
        restore2.setChinese = true;
        const auto again = repairOptionsTxt(path, QStringLiteral("1.21.10"), 4556, false, restore2);
        QVERIFY(again.hadVersion);
        QVERIFY(!again.defaults);
        QVERIFY(!again.changed);
    }

    void repairFileReportsMissingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto result = repairOptionsTxt(dir.filePath(QStringLiteral("options.txt")), QStringLiteral("1.21.10"), 4556, true);
        QVERIFY(result.missing);
        QVERIFY(!result.changed);
        QVERIFY(result.error.isEmpty());  // 新实例本来就没有 options.txt，不算错
    }
};

QTEST_GUILESS_MAIN(MapOptionsTxtTest)

#include "MapOptionsTxt_test.moc"
