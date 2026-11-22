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
#include <QTest>

#include "maps/SeriesNames.h"

/// 实例名里的系列代码 → 站上的系列名（`RC3` → `Ragecraft 3`）
class MapSeriesNamesTest : public QObject {
    Q_OBJECT

   private slots:
    void knownCodes()
    {
        QCOMPARE(maps::series::displayLabel(QStringLiteral("RC3")), QStringLiteral("Ragecraft 3"));
        QCOMPARE(maps::series::displayLabel(QStringLiteral("US1")), QStringLiteral("Untold Stories 1"));
        // 长系列名走表里的短写；编号前面的 0 不进界面（SP03 → 3）
        QCOMPARE(maps::series::displayLabel(QStringLiteral("SP03")), QStringLiteral("Sim. Protocol 3"));
    }

    void codeLookupIsCaseInsensitive()
    {
        QCOMPARE(maps::series::entryForCode(QStringLiteral("rc")).name, QStringLiteral("Ragecraft"));
        QVERIFY(maps::series::entryForCode(QStringLiteral("ZZ")).name.isEmpty());
    }

    void unknownStaysAsTyped()
    {
        // 表里没有就原样返回 —— 猜一个系列名会把两张不同的图叫成同一系列
        QCOMPARE(maps::series::displayLabel(QStringLiteral("ZZ9")), QStringLiteral("ZZ9"));
        QCOMPARE(maps::series::displayLabel(QStringLiteral("XYZ")), QStringLiteral("XYZ"));
        QCOMPARE(maps::series::displayLabel(QStringLiteral("")), QStringLiteral(""));
    }

    void abbrevKeepsIssueNumber()
    {
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("Ragecraft"), 3), QStringLiteral("RC3"));
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("Mapwreck"), 7), QStringLiteral("MW7"));
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("Simulation Protocol"), 3), QStringLiteral("SP3"));
    }

    void abbrevDefaultsToIssueOne()
    {
        // 站上只在部分图上给 series_num：`[US]` 与 `[US9]` 同属 Untold Stories，
        // 缺号按 1 显示，同一系列里不会出现两种写法
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("Untold Stories"), 0), QStringLiteral("US1"));
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("Little Nightmares"), -1), QStringLiteral("LN1"));
    }

    void abbrevFallsBackToInitials()
    {
        // 表里没有的系列取首字母（虚词跳过），期号同样缺省为 1；系列名为空返回空
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("The Pantheon"), 0), QStringLiteral("TP1"));
        QCOMPARE(maps::series::abbrevFor(QStringLiteral("  "), 0), QString());
    }
};

QTEST_GUILESS_MAIN(MapSeriesNamesTest)

#include "MapSeriesNames_test.moc"
