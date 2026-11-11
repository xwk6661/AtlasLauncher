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
#include <QHash>
#include <QTest>

#include <maps/MapL10nTranslate.h>

using namespace maps::l10n;

/**
 * 只测"解析模型输出"这一段：它是唯一不依赖网络、又最容易出错的环节。
 * 模型很爱裹代码围栏、或者在 JSON 前后多说一句客套话，这些都必须容忍。
 */
class MapL10nTranslateTest : public QObject {
    Q_OBJECT

   private slots:
    void parsesCleanJson()
    {
        const auto result = MapL10nTranslateTask::parseResponse(
            R"({"items":[{"i":0,"zh":"苦力怕"},{"i":3,"zh":"末影龙"}]})");
        QCOMPARE(result.size(), 2);
        QCOMPARE(result.value(0), QStringLiteral("苦力怕"));
        QCOMPARE(result.value(3), QStringLiteral("末影龙"));
    }

    void toleratesCodeFenceAndChatter()
    {
        const auto fenced = MapL10nTranslateTask::parseResponse(
            "Here you go:\n```json\n{\"items\":[{\"i\":1,\"zh\":\"下界\"}]}\n```\nHope this helps!");
        QCOMPARE(fenced.size(), 1);
        QCOMPARE(fenced.value(1), QStringLiteral("下界"));
    }

    void skipsInvalidEntries()
    {
        // 缺 i、i 为负、zh 为空 都不该进结果
        const auto result = MapL10nTranslateTask::parseResponse(
            R"({"items":[{"zh":"没有编号"},{"i":-1,"zh":"负号"},{"i":2,"zh":""},{"i":3,"zh":"好的"}]})");
        QCOMPARE(result.size(), 1);
        QCOMPARE(result.value(3), QStringLiteral("好的"));
    }

    void rejectsGarbage()
    {
        QVERIFY(MapL10nTranslateTask::parseResponse("").isEmpty());
        QVERIFY(MapL10nTranslateTask::parseResponse("not json at all").isEmpty());
        QVERIFY(MapL10nTranslateTask::parseResponse("{broken").isEmpty());
        // 合法 JSON 但没有 items 字段
        QVERIFY(MapL10nTranslateTask::parseResponse(R"({"result":"nope"})").isEmpty());
    }

    void keepsFormatCodesVerbatim()
    {
        // § 码与私用区字形是原样搬运的，解析层不该改动它们
        const auto result = MapL10nTranslateTask::parseResponse(
            QStringLiteral(R"({"items":[{"i":0,"zh":"\u00a7a\u00a7l苦力怕 \ue100"}]})").toUtf8());
        QCOMPARE(result.size(), 1);
        QVERIFY(result.value(0).contains(QStringLiteral("\u00a7a\u00a7l")));
        QVERIFY(result.value(0).contains(QStringLiteral("\ue100")));
    }
};

QTEST_GUILESS_MAIN(MapL10nTranslateTest)

#include "MapL10nTranslate_test.moc"
