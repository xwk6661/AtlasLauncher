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
#include "JavaRecommend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <memory>

#include "Application.h"
#include "FileSystem.h"
#include "java/JavaInstall.h"
#include "java/JavaInstallList.h"
#include "java/JavaUtils.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

namespace maps {
namespace {

struct McVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
};

/// "1.20.5" / "1.16.5-forge-36.2.39" / "1.7.10" → 三段数字（后面跟什么都不管）
McVersion parseMcVersion(const QString& raw)
{
    McVersion parsed;
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*(\d+)\.(\d+)(?:\.(\d+))?)"));
    const auto match = pattern.match(raw);
    if (!match.hasMatch())
        return parsed;
    parsed.major = match.captured(1).toInt();
    parsed.minor = match.captured(2).toInt();
    parsed.patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    parsed.valid = true;
    return parsed;
}

QString majorsText(const QList<int>& majors)
{
    QStringList parts;
    for (int major : majors)
        parts.append(QString::number(major));
    return parts.join(QStringLiteral(" / "));
}

int compareVersions(const QString& a, const QString& b)
{
    const auto left = a.split(QRegularExpression(QStringLiteral("[._+-]")));
    const auto right = b.split(QRegularExpression(QStringLiteral("[._+-]")));
    const int count = std::max(left.size(), right.size());
    for (int i = 0; i < count; ++i) {
        const int l = i < left.size() ? left.at(i).toInt() : 0;
        const int r = i < right.size() ? right.at(i).toInt() : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return QString::compare(a, b);
}

/// Mojang 运行时目录名 → 主版本（只有读不到 release 时才用）
int managedRuntimeMajor(const QString& name)
{
    static const QHash<QString, int> table = {
        { QStringLiteral("jre-legacy"), 8 },
        { QStringLiteral("java-runtime-alpha"), 16 },
        { QStringLiteral("java-runtime-beta"), 17 },
        { QStringLiteral("java-runtime-gamma"), 17 },
        { QStringLiteral("java-runtime-delta"), 21 },
        { QStringLiteral("java-runtime-epsilon"), 21 },
        { QStringLiteral("java-runtime-zeta"), 21 },
    };
    return table.value(name, 0);
}

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

}  // namespace

int javaMajorFromVersionString(const QString& version)
{
    const QString trimmed = version.trimmed();
    if (trimmed.isEmpty())
        return 0;
    const auto parts = trimmed.split(QLatin1Char('.'));
    bool ok = false;
    const int first = parts.first().toInt(&ok);
    if (!ok)
        return 0;
    // 1.8.0_402 这种老写法：主版本在第二段
    if (first == 1 && parts.size() > 1) {
        const int second = parts.at(1).toInt(&ok);
        return ok ? second : 0;
    }
    return first;
}

bool parseRuntimeRelease(const QString& text, QString* version, bool* is64)
{
    if (version != nullptr)
        version->clear();
    if (is64 != nullptr)
        *is64 = false;

    bool foundVersion = false;
    for (const QString& rawLine : text.split(QLatin1Char('\n'))) {
        const int equals = rawLine.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = rawLine.left(equals).trimmed();
        QString value = rawLine.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);

        if (key == QLatin1String("JAVA_VERSION")) {
            if (version != nullptr)
                *version = value;
            foundVersion = !value.isEmpty();
        } else if (key == QLatin1String("OS_ARCH")) {
            if (is64 != nullptr)
                *is64 = value == QLatin1String("x86_64") || value == QLatin1String("amd64") || value == QLatin1String("aarch64");
        }
    }
    return foundVersion;
}

JavaRequirement requirementForVersion(const QString& mcVersion)
{
    JavaRequirement requirement;
    requirement.mcVersion = mcVersion;
    requirement.source = QStringLiteral("版本表");

    const auto version = parseMcVersion(mcVersion);
    if (!version.valid) {
        requirement.reason = QStringLiteral("版本写法无法解析（%1），不猜 Java").arg(mcVersion);
        return requirement;
    }
    if (version.major != 1) {
        // 26.x 起换了版本写法（26.1、26.2…），运行环境沿用 Java 21。
        // 认不出的年份写法则继续不猜 —— 这里只认"数字.数字"且主版本 ≥ 26 的形态。
        if (version.major >= 26) {
            requirement.majors = { 21 };
            requirement.reason = QStringLiteral("%1 沿用 Java 21（26.x 起的运行环境）").arg(mcVersion);
            return requirement;
        }
        requirement.reason = QStringLiteral("%1 不在 Java 对照表里，不猜").arg(mcVersion);
        return requirement;
    }

    if (version.minor <= 16) {
        requirement.majors = { 8 };
        requirement.reason = QStringLiteral("%1 使用 Java 8（更高版本在旧 Forge / 旧字节码上会出问题）").arg(mcVersion);
    } else if (version.minor == 17) {
        requirement.majors = { 16 };
        requirement.reason = QStringLiteral("%1 需要 Java 16").arg(mcVersion);
    } else if (version.minor <= 20) {
        if (version.minor == 20 && version.patch >= 5) {
            requirement.majors = { 21 };
            requirement.reason = QStringLiteral("%1 起改用 Java 21").arg(mcVersion);
        } else {
            requirement.majors = { 17 };
            requirement.reason = QStringLiteral("%1 需要 Java 17").arg(mcVersion);
        }
    } else {
        requirement.majors = { 21 };
        requirement.reason = QStringLiteral("%1 需要 Java 21").arg(mcVersion);
    }
    return requirement;
}

JavaRequirement requirementForInstance(MinecraftInstance* instance, const QString& mcVersionHint)
{
    QString mcVersion = mcVersionHint.trimmed();
    if (instance != nullptr) {
        if (auto* components = instance->getPackProfile()) {
            if (mcVersion.isEmpty())
                mcVersion = components->getComponentVersion(QStringLiteral("net.minecraft"));
            if (auto profile = components->getProfile()) {
                const auto majors = profile->getCompatibleJavaMajors();
                if (!majors.isEmpty()) {
                    JavaRequirement requirement;
                    requirement.mcVersion = mcVersion;
                    requirement.majors = majors;
                    requirement.managedName = profile->getCompatibleJavaName();
                    requirement.source = QStringLiteral("meta");
                    requirement.reason = QStringLiteral("%1 的版本元数据要求 Java %2").arg(mcVersion, majorsText(majors));
                    return requirement;
                }
            }
        }
    }
    return requirementForVersion(mcVersion);
}

QList<JavaCandidate> collectCandidates()
{
    QList<JavaCandidate> out;
    QSet<QString> seen;

    const auto add = [&out, &seen](JavaCandidate candidate) {
        if (candidate.path.isEmpty())
            return;
        const QFileInfo info(candidate.path);
        if (!info.exists() || !info.isFile())
            return;
        const QString key = normalizedPath(candidate.path);
        if (seen.contains(key))
            return;
        seen.insert(key);
        if (candidate.label.isEmpty())
            candidate.label = info.absoluteFilePath();
        out.append(candidate);
    };

    // 1) 玩家在设置里按大版本固定的路径：优先级最高（与 AutoInstallJava 一致）
    for (int major : JavaUtils::pinnableMajors) {
        const QString pinned = JavaUtils::getPinnedPath(major);
        if (pinned.isEmpty())
            continue;
        const QString resolved = FS::ResolveExecutable(pinned);
        if (resolved.isEmpty())
            continue;
        JavaCandidate candidate;
        candidate.path = resolved;
        candidate.major = major;
        candidate.version = QString::number(major);
        candidate.label = QStringLiteral("设置里固定的路径");
        candidate.is64 = true;
        candidate.pinned = true;
        candidate.source = QStringLiteral("pinned");
        add(candidate);
    }

    if (APPLICATION_DYN == nullptr)
        return out;

    // 2) Mojang 托管运行时（<java>/<runtime>/bin/javaw.exe）
    const QDir runtimeRoot(APPLICATION->javaPath());
    const auto runtimeDirs = runtimeRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : runtimeDirs) {
        const QString binary = FS::PathCombine(entry.absoluteFilePath(), QStringLiteral("bin"), JavaUtils::javaExecutable);
        if (!QFileInfo::exists(binary))
            continue;

        QString version;
        bool is64 = false;
        QFile release(FS::PathCombine(entry.absoluteFilePath(), QStringLiteral("release")));
        if (release.open(QIODevice::ReadOnly))
            parseRuntimeRelease(QString::fromUtf8(release.readAll()), &version, &is64);

        int major = javaMajorFromVersionString(version);
        if (major == 0)
            major = managedRuntimeMajor(entry.fileName());
        if (major == 0)
            continue;  // 判不出版本就不列出来，免得"看起来能用"

        JavaCandidate candidate;
        candidate.path = binary;
        candidate.major = major;
        candidate.version = version.isEmpty() ? QString::number(major) : version;
        candidate.label = entry.fileName();
        candidate.managed = true;
        candidate.is64 = is64;
        candidate.source = QStringLiteral("managed");
        add(candidate);
    }

    // 3) 已经扫过的本机 Java（JavaInstallList：注册表 / 常见目录 / 托管运行时）
    if (auto* list = APPLICATION->javalist(); list != nullptr && list->isLoaded()) {
        for (int i = 0; i < list->count(); ++i) {
            auto install = std::dynamic_pointer_cast<JavaInstall>(list->at(i));
            if (!install || install->id.major() <= 0)
                continue;
            JavaCandidate candidate;
            candidate.path = install->path;
            candidate.major = install->id.major();
            candidate.version = install->id.toString();
            candidate.label = install->path;
            candidate.is64 = install->is_64bit;
            candidate.source = QStringLiteral("detected");
            add(candidate);
        }
    }

    // 4) 全局设置里那个 Java（没有实例覆盖时就是实际会用的那个）
    const QString globalPath = APPLICATION->settings()->get("JavaPath").toString();
    if (!globalPath.isEmpty()) {
        const QString resolved = FS::ResolveExecutable(globalPath);
        if (!resolved.isEmpty()) {
            const int major = javaMajorFromVersionString(APPLICATION->settings()->get("JavaVersion").toString());
            if (major > 0) {
                JavaCandidate candidate;
                candidate.path = resolved;
                candidate.major = major;
                candidate.version = APPLICATION->settings()->get("JavaVersion").toString();
                candidate.label = resolved;
                candidate.is64 = APPLICATION->settings()->get("JavaArchitecture").toString() != QLatin1String("32");
                candidate.source = QStringLiteral("global");
                add(candidate);
            }
        }
    }

    return out;
}

JavaRecommendation recommend(const JavaRequirement& requirement, const QList<JavaCandidate>& candidates)
{
    JavaRecommendation result;
    if (!requirement.isKnown()) {
        result.problem = requirement.reason.isEmpty() ? QStringLiteral("判不出这个版本需要哪个 Java") : requirement.reason;
        return result;
    }

    QList<JavaCandidate> usable;
    for (const auto& candidate : candidates) {
        if (requirement.majors.contains(candidate.major))
            usable.append(candidate);
    }

    if (usable.isEmpty()) {
        result.problem = QStringLiteral("本机没有符合要求的 Java（需要 Java %1）").arg(majorsText(requirement.majors));
        result.alternates = candidates;
        return result;
    }

    // 排序：玩家固定 > meta 指定名字的托管运行时 > 其它托管运行时 > 检测到的 64 位 > 检测到的 32 位 > 全局
    const auto rank = [&requirement](const JavaCandidate& candidate) {
        if (candidate.pinned)
            return 0;
        if (candidate.managed && !requirement.managedName.isEmpty() && candidate.label == requirement.managedName)
            return 1;
        if (candidate.managed)
            return 2;
        if (candidate.source == QLatin1String("detected"))
            return candidate.is64 ? 3 : 4;
        return 5;
    };

    std::stable_sort(usable.begin(), usable.end(), [&rank, &requirement](const JavaCandidate& a, const JavaCandidate& b) {
        const int rankA = rank(a);
        const int rankB = rank(b);
        if (rankA != rankB)
            return rankA < rankB;
        // requirement.majors 的顺序就是偏好顺序（meta 会把自己的取舍排好）
        const int majorA = std::max(0, static_cast<int>(requirement.majors.indexOf(a.major)));
        const int majorB = std::max(0, static_cast<int>(requirement.majors.indexOf(b.major)));
        if (majorA != majorB)
            return majorA < majorB;
        if (a.is64 != b.is64)
            return a.is64;
        return compareVersions(a.version, b.version) > 0;
    });

    result.ok = true;
    result.chosen = usable.first();
    result.alternates = usable.mid(1);

    const JavaCandidate& chosen = result.chosen;
    if (chosen.pinned)
        result.reason = QStringLiteral("用设置里为 Java %1 固定的路径").arg(chosen.major);
    else if (chosen.managed && chosen.label == requirement.managedName)
        result.reason = QStringLiteral("meta 为这个版本指定的运行时是 %1").arg(chosen.label);
    else if (chosen.managed)
        result.reason = QStringLiteral("本地已装的托管运行时 %1（Java %2）").arg(chosen.label, QString::number(chosen.major));
    else
        result.reason = QStringLiteral("本地已装 Java %1（%2）").arg(QString::number(chosen.major), chosen.version);

    if (!requirement.reason.isEmpty())
        result.reason += QStringLiteral("；%1").arg(requirement.reason);
    return result;
}

bool applyToInstance(MinecraftInstance* instance, const JavaCandidate& candidate, bool automatic, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error != nullptr)
            *error = message;
        return false;
    };

    if (instance == nullptr)
        return fail(QStringLiteral("实例不存在"));

    const QString resolved = FS::ResolveExecutable(candidate.path);
    if (resolved.isEmpty() || !QFileInfo::exists(resolved))
        return fail(QStringLiteral("这个 Java 已经不在原来的位置了：%1").arg(candidate.path));

    auto* settings = instance->settings();
    const SettingsObject::Lock lock(settings);
    settings->set("OverrideJavaLocation", true);
    settings->set("JavaPath", resolved);
    // 记住来源：true 表示"这是按版本自动选的"，下次版本变了可以换；
    // 玩家在 Java 设置页手改路径会把它置回 false（上游既有行为）。
    settings->set("AutomaticJava", automatic);

    if (error != nullptr)
        error->clear();
    return true;
}

AutoJavaResult autoSelectForInstance(MinecraftInstance* instance, const QString& mcVersionHint)
{
    AutoJavaResult result;
    if (instance == nullptr) {
        result.problem = QStringLiteral("实例不存在");
        return result;
    }

    auto* settings = instance->settings();
    if (!settings->get("MapAutoJava").toBool()) {
        result.problem = QStringLiteral("已关闭「启动前按地图版本自动选择 Java」");
        return result;
    }

    const QString configured = settings->get("JavaPath").toString();
    const QString current = configured.isEmpty() ? QString() : FS::ResolveExecutable(configured);
    const bool currentExists = !current.isEmpty() && QFileInfo::exists(current);
    // 玩家手选 = 勾了实例覆盖，而且不是自动写入的；这样的路径绝不被替换
    const bool explicitChoice = settings->get("OverrideJavaLocation").toBool() && !settings->get("AutomaticJava").toBool();

    const auto requirement = requirementForInstance(instance, mcVersionHint);
    const auto recommendation = recommend(requirement, collectCandidates());
    if (!recommendation.ok) {
        result.problem = recommendation.problem;
        return result;
    }

    if (explicitChoice && currentExists) {
        const int currentMajor = javaMajorFromVersionString(settings->get("JavaVersion").toString());
        if (currentMajor > 0 && !requirement.majors.contains(currentMajor)) {
            result.problem = QStringLiteral("玩家手选的 Java %1 与该版本要求的 Java %2 不一致，按「不擅自改手选」保留")
                                 .arg(currentMajor)
                                 .arg(majorsText(requirement.majors));
        } else {
            result.problem = QStringLiteral("玩家已手选 Java，保持不动");
        }
        return result;
    }

    if (currentExists && normalizedPath(current) == normalizedPath(recommendation.chosen.path))
        return result;  // 已经是对的那个

    QString error;
    if (!applyToInstance(instance, recommendation.chosen, true, &error)) {
        result.problem = error;
        return result;
    }
    result.applied = true;
    result.path = recommendation.chosen.path;
    result.reason = recommendation.reason;
    return result;
}

}  // namespace maps
