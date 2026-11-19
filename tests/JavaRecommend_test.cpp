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

#include "maps/JavaRecommend.h"

namespace {

maps::JavaCandidate candidate(const QString& path, int major, const QString& version, const QString& label,
                              const QString& source, bool managed = false, bool pinned = false, bool is64 = true)
{
    maps::JavaCandidate c;
    c.path = path;
    c.major = major;
    c.version = version;
    c.label = label;
    c.source = source;
    c.managed = managed;
    c.pinned = pinned;
    c.is64 = is64;
    return c;
}

}  // namespace

/// 「这个版本要哪个 Java」以及「本地这些 Java 里挑哪个」——纯函数，不碰磁盘与网络
class JavaRecommendTest : public QObject {
    Q_OBJECT

   private slots:
    void requirementTable()
    {
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.7.10")).majors, QList<int>{ 8 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.12.2")).majors, QList<int>{ 8 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.16.5")).majors, QList<int>{ 8 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.17")).majors, QList<int>{ 16 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.17.1")).majors, QList<int>{ 16 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.18")).majors, QList<int>{ 17 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.20.1")).majors, QList<int>{ 17 });
        // 1.20.5 是分界线（物品组件化那一次）
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.20.4")).majors, QList<int>{ 17 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.20.5")).majors, QList<int>{ 21 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.21.1")).majors, QList<int>{ 21 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("1.21.9")).majors, QList<int>{ 21 });
    }

    void version26UsesJava21()
    {
        // 26.x 换了版本写法，运行环境沿用 Java 21
        QCOMPARE(maps::requirementForVersion(QStringLiteral("26.1")).majors, QList<int>{ 21 });
        QCOMPARE(maps::requirementForVersion(QStringLiteral("26.2")).majors, QList<int>{ 21 });
    }

    void unknownVersionStaysUnknown()
    {
        // 判不出来就只能返回空 —— 猜一个"大概能用"的版本正是这个项目最怕的事
        QVERIFY(!maps::requirementForVersion(QString()).isKnown());
        QVERIFY(!maps::requirementForVersion(QStringLiteral("23w45a")).isKnown());
        QVERIFY(!maps::requirementForVersion(QStringLiteral("abc")).isKnown());
    }

    void majorFromVersionString()
    {
        QCOMPARE(maps::javaMajorFromVersionString(QStringLiteral("17.0.8")), 17);
        QCOMPARE(maps::javaMajorFromVersionString(QStringLiteral("1.8.0_402")), 8);
        QCOMPARE(maps::javaMajorFromVersionString(QStringLiteral("21")), 21);
        QCOMPARE(maps::javaMajorFromVersionString(QStringLiteral("1.8.0_402-b06")), 8);
        QCOMPARE(maps::javaMajorFromVersionString(QString()), 0);
        QCOMPARE(maps::javaMajorFromVersionString(QStringLiteral("garbage")), 0);
    }

    void parseRuntimeReleaseFile()
    {
        const QString release = QStringLiteral(
            "IMPLEMENTOR=\"Eclipse Adoptium\"\n"
            "JAVA_VERSION=\"17.0.8\"\n"
            "JAVA_VERSION_DATE=\"2023-07-18\"\n"
            "OS_ARCH=\"x86_64\"\n");
        QString version;
        bool is64 = false;
        QVERIFY(maps::parseRuntimeRelease(release, &version, &is64));
        QCOMPARE(version, QStringLiteral("17.0.8"));
        QVERIFY(is64);

        // 有些运行时写裸值
        QVERIFY(maps::parseRuntimeRelease(QStringLiteral("JAVA_VERSION=8\nOS_ARCH=x86\n"), &version, &is64));
        QCOMPARE(version, QStringLiteral("8"));
        QVERIFY(!is64);

        // 没有 JAVA_VERSION 就不算解析成功
        QVERIFY(!maps::parseRuntimeRelease(QStringLiteral("OS_ARCH=\"x86_64\"\n"), &version, &is64));
    }

    void recommendationPrefersPinned()
    {
        const auto requirement = maps::requirementForVersion(QStringLiteral("1.20.1"));
        const QList<maps::JavaCandidate> candidates = {
            candidate(QStringLiteral("C:/java/gamma/bin/javaw.exe"), 17, QStringLiteral("17.0.8"),
                      QStringLiteral("java-runtime-gamma"), QStringLiteral("managed"), true),
            candidate(QStringLiteral("D:/jdk17/bin/javaw.exe"), 17, QStringLiteral("17.0.9"), QStringLiteral("D:/jdk17"),
                      QStringLiteral("detected")),
            candidate(QStringLiteral("E:/pinned17/bin/javaw.exe"), 17, QStringLiteral("17.0.2"), QStringLiteral("pinned"),
                      QStringLiteral("pinned"), false, true),
        };

        const auto result = maps::recommend(requirement, candidates);
        QVERIFY(result.ok);
        QCOMPARE(result.chosen.path, QStringLiteral("E:/pinned17/bin/javaw.exe"));
        // 其余都是备选，顺序按优先级
        QCOMPARE(result.alternates.size(), 2);
        QCOMPARE(result.alternates.first().label, QStringLiteral("java-runtime-gamma"));
    }

    void recommendationPrefersManagedRuntimeNamedByMeta()
    {
        maps::JavaRequirement requirement;
        requirement.mcVersion = QStringLiteral("1.20.1");
        requirement.majors = { 17 };
        requirement.managedName = QStringLiteral("java-runtime-gamma");
        requirement.source = QStringLiteral("meta");

        const QList<maps::JavaCandidate> candidates = {
            candidate(QStringLiteral("C:/program files/java/jdk-17/bin/javaw.exe"), 17, QStringLiteral("17.0.10"),
                      QStringLiteral("jdk-17"), QStringLiteral("detected")),
            candidate(QStringLiteral("D:/data/java/java-runtime-gamma/bin/javaw.exe"), 17, QStringLiteral("17.0.8"),
                      QStringLiteral("java-runtime-gamma"), QStringLiteral("managed"), true),
        };

        const auto result = maps::recommend(requirement, candidates);
        QVERIFY(result.ok);
        // meta 点名的那个运行时优先于"版本号更高的系统 JDK"
        QCOMPARE(result.chosen.label, QStringLiteral("java-runtime-gamma"));
    }

    void recommendationNeverDowngrades()
    {
        // 要 Java 8，本地只有 Java 17：不许"大概能用"地选 17（Forge 1.12 会直接崩）
        const auto requirement = maps::requirementForVersion(QStringLiteral("1.12.2"));
        const QList<maps::JavaCandidate> candidates = {
            candidate(QStringLiteral("C:/java17/bin/javaw.exe"), 17, QStringLiteral("17.0.8"), QStringLiteral("jdk-17"),
                      QStringLiteral("detected")),
        };

        const auto result = maps::recommend(requirement, candidates);
        QVERIFY(!result.ok);
        QVERIFY(!result.problem.isEmpty());
        // 仍然把"本机有什么"交出去，界面才能说明为什么没选
        QCOMPARE(result.alternates.size(), 1);
    }

    void recommendationFollowsMajorPreferenceOrder()
    {
        // meta 有时给多个可接受的大版本，顺序就是偏好
        maps::JavaRequirement requirement;
        requirement.mcVersion = QStringLiteral("1.16.5");
        requirement.majors = { 8, 11 };
        requirement.source = QStringLiteral("meta");

        const QList<maps::JavaCandidate> candidates = {
            candidate(QStringLiteral("C:/java11/bin/javaw.exe"), 11, QStringLiteral("11.0.20"), QStringLiteral("jdk-11"),
                      QStringLiteral("detected")),
            candidate(QStringLiteral("C:/java8/bin/javaw.exe"), 8, QStringLiteral("1.8.0_402"), QStringLiteral("jre-legacy"),
                      QStringLiteral("managed"), true),
        };

        const auto result = maps::recommend(requirement, candidates);
        QVERIFY(result.ok);
        QCOMPARE(result.chosen.major, 8);
    }

    void sixtyFourBitWinsAmongDetected()
    {
        const auto requirement = maps::requirementForVersion(QStringLiteral("1.21.1"));
        const QList<maps::JavaCandidate> candidates = {
            candidate(QStringLiteral("C:/java21-x86/bin/javaw.exe"), 21, QStringLiteral("21.0.1"), QStringLiteral("jdk-21-x86"),
                      QStringLiteral("detected"), false, false, false),
            candidate(QStringLiteral("C:/java21-x64/bin/javaw.exe"), 21, QStringLiteral("21.0.1"), QStringLiteral("jdk-21-x64"),
                      QStringLiteral("detected"), false, false, true),
        };

        const auto result = maps::recommend(requirement, candidates);
        QVERIFY(result.ok);
        QVERIFY(result.chosen.is64);
        QCOMPARE(result.chosen.label, QStringLiteral("jdk-21-x64"));
    }

    void emptyRequirementSelectsNothing()
    {
        const auto result = maps::recommend(maps::requirementForVersion(QStringLiteral("23w45a")), {});
        QVERIFY(!result.ok);
        QVERIFY(!result.problem.isEmpty());
    }
};

QTEST_GUILESS_MAIN(JavaRecommendTest)

#include "JavaRecommend_test.moc"
