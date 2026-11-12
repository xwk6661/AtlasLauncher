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
#include "MapOptionsTxt.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace maps::options {
namespace {

const QString LANGUAGE_KEY = QStringLiteral("lang");
const QString SNEAK_KEY = QStringLiteral("key_key.sneak");
const QString SPRINT_KEY = QStringLiteral("key_key.sprint");
const QString VERSION_KEY = QStringLiteral("version");
const QString ONBOARD_KEY = QStringLiteral("onboardAccessibility");
const QString FULLSCREEN_KEY = QStringLiteral("fullscreen");

/// 找 `key:` 开头的行；找不到返回 -1
int findLine(const QStringList& lines, const QString& key)
{
    const QString prefix = key + QLatin1Char(':');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).startsWith(prefix))
            return i;
    }
    return -1;
}

/// 存在就替换、不存在就追加；返回是否真的改了（值相同算没改，保证幂等）
bool put(QStringList& lines, const QString& key, const QString& value)
{
    const QString line = key + QLatin1Char(':') + value;
    const int index = findLine(lines, key);
    if (index < 0) {
        lines.append(line);
        return true;
    }
    if (lines.at(index) == line)
        return false;
    lines[index] = line;
    return true;
}

/// 和 put 一样，但写不出现在末尾：`version` 必须留在第一行（MC 自己也是这么写的）。
/// 文件被别家工具拼过时可能出现多行同名键，这里一并清掉，保证只剩一行。
bool putFront(QStringList& lines, const QString& key, const QString& value)
{
    const QString prefix = key + QLatin1Char(':');
    const QStringList before = lines;
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines.at(i).startsWith(prefix))
            lines.removeAt(i);
    }
    lines.prepend(key + QLatin1Char(':') + value);
    return lines != before;
}

}  // namespace

std::optional<Format> formatForVersion(const QString& mcVersion)
{
    // 只取前两段：`1.7.x` / `1.12` / `1.21.11` / `26.2` 都归到同一个判据上
    static const QRegularExpression release(QStringLiteral("^(\\d{1,2})\\.(\\d{1,2})"));
    const auto match = release.match(mcVersion.trimmed());
    if (!match.hasMatch())
        return std::nullopt;

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    if (major > 1)
        return Format::Modern;  // 26.2 这类按年份的新版本号一定在现代一侧
    return (major == 1 && minor >= 13) ? Format::Modern : Format::Legacy;
}

QString chineseLanguage(Format format)
{
    // 1.8 的 options.txt 里写的是 `lang:zh_CN`（大写），1.13 起改成 `zh_cn`
    return format == Format::Modern ? QStringLiteral("zh_cn") : QStringLiteral("zh_CN");
}

QString sneakValue(Format format)
{
    // 29 = LWJGL2 的 KEY_LCONTROL
    return format == Format::Modern ? QStringLiteral("key.keyboard.left.control") : QStringLiteral("29");
}

QString sprintValue(Format format)
{
    // 42 = LWJGL2 的 KEY_LSHIFT
    return format == Format::Modern ? QStringLiteral("key.keyboard.left.shift") : QStringLiteral("42");
}

int applyDefaults(QStringList& lines, Format format, const Defaults& defaults)
{
    int changed = 0;
    if (defaults.setChinese && put(lines, LANGUAGE_KEY, chineseLanguage(format)))
        ++changed;
    if (defaults.swapSprintSneak) {
        if (put(lines, SNEAK_KEY, sneakValue(format)))
            ++changed;
        if (put(lines, SPRINT_KEY, sprintValue(format)))
            ++changed;
    }
    if (defaults.skipAccessibilityOnboarding && disableOnboarding(lines))
        ++changed;
    if (defaults.setFullscreen && put(lines, FULLSCREEN_KEY, defaults.fullscreen ? QStringLiteral("true")
                                                                                 : QStringLiteral("false")))
        ++changed;
    return changed;
}

std::optional<Format> detectFormat(const QStringList& lines)
{
    bool sawModern = false;
    bool sawLegacy = false;
    for (const auto& line : lines) {
        if (!line.startsWith(QLatin1String("key_key.")))
            continue;
        const QString value = line.section(QLatin1Char(':'), 1);
        if (value.startsWith(QLatin1String("key.keyboard.")) || value.startsWith(QLatin1String("key.mouse.")))
            sawModern = true;
        // 老版把鼠标键写成负数（key_key.attack:-100），所以负号也算数字键码
        else if (!value.isEmpty() && (value.at(0).isDigit() || value.at(0) == QLatin1Char('-')))
            sawLegacy = true;
    }
    // 两代信号同时出现说明文件被别的工具拼过，按现代（更能容纳）处理
    if (sawModern)
        return Format::Modern;
    if (sawLegacy)
        return Format::Legacy;
    return std::nullopt;
}

bool putVersionStamp(QStringList& lines, Format format, int dataVersion)
{
    // 1.12 及更早没有这个键；1.13 起才由游戏自己写
    if (format != Format::Modern || dataVersion <= 0)
        return false;
    return putFront(lines, VERSION_KEY, QString::number(dataVersion));
}

bool disableOnboarding(QStringList& lines)
{
    return put(lines, ONBOARD_KEY, QStringLiteral("false"));
}

RepairResult repairOptionsTxt(const QString& path, const QString& mcVersion, int dataVersion, bool fixOnboarding,
                              const Defaults& restoreDefaults)
{
    RepairResult result;

    QFile in(path);
    if (!in.exists()) {
        result.missing = true;
        return result;
    }
    if (!in.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取 options.txt：%1").arg(in.errorString());
        return result;
    }
    const QString text = QString::fromUtf8(in.readAll());
    in.close();

    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    auto format = formatForVersion(mcVersion);
    if (!format)
        format = detectFormat(lines);
    if (!format)
        return result;  // 判不出代：一行都不碰（写错一代比不写更糟）

    result.hadVersion = findLine(lines, VERSION_KEY) >= 0;
    result.version = putVersionStamp(lines, *format, dataVersion);
    if (fixOnboarding)
        result.onboarding = disableOnboarding(lines);
    // 从没被游戏读过的那份文件：当初写进去的中文/键位已经丢了，顺手补回来
    if (!result.hadVersion && !restoreDefaults.isEmpty())
        result.defaults = applyDefaults(lines, *format, restoreDefaults) > 0;
    result.changed = result.version || result.onboarding || result.defaults;
    if (!result.changed)
        return result;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("无法写入 options.txt：%1").arg(out.errorString());
        return result;
    }
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    if (!out.commit()) {
        result.error = QStringLiteral("无法写入 options.txt：%1").arg(out.errorString());
        return result;
    }
    return result;
}

}  // namespace maps::options
