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

#include <algorithm>

#include "settings/SettingsObject.h"

namespace maps::options {
namespace {

const QString LANGUAGE_KEY = QStringLiteral("lang");
const QString SNEAK_KEY = QStringLiteral("key_key.sneak");
const QString SPRINT_KEY = QStringLiteral("key_key.sprint");
const QString PERSPECTIVE_KEY = QStringLiteral("key_key.togglePerspective");
const QString VERSION_KEY = QStringLiteral("version");
const QString ONBOARD_KEY = QStringLiteral("onboardAccessibility");
const QString FULLSCREEN_KEY = QStringLiteral("fullscreen");
const QString AUTO_JUMP_KEY = QStringLiteral("autoJump");
const QString RENDER_DISTANCE_KEY = QStringLiteral("renderDistance");
const QString BOB_VIEW_KEY = QStringLiteral("bobView");
const QString GAMMA_KEY = QStringLiteral("gamma");

/// 把 `1.7.x` / `1.12` / `21.11` 这类写法拆成三段；认不出返回 false。
/// `1.7.x` 的第三段是 `x`，按 0 处理（1.7.x 在这几项上和 1.7.10 一致）。
bool parseVersion(const QString& mcVersion, int* major, int* minor, int* patch)
{
    static const QRegularExpression release(QStringLiteral("^(\\d{1,2})\\.(\\d{1,2})(?:\\.(\\d{1,2}|x))?"));
    const auto match = release.match(mcVersion.trimmed());
    if (!match.hasMatch())
        return false;
    *major = match.captured(1).toInt();
    *minor = match.captured(2).toInt();
    const QString third = match.captured(3);
    *patch = (third.isEmpty() || third == QLatin1String("x")) ? 0 : third.toInt();
    return true;
}

/// `version >= major.minor.patch`。判不出就 false（宁可不写）。
bool atLeast(const QString& mcVersion, int major, int minor, int patch = 0)
{
    int a = 0, b = 0, c = 0;
    if (!parseVersion(mcVersion, &a, &b, &c))
        return false;
    if (a != major)
        return a > major;
    if (b != minor)
        return b > minor;
    return c >= patch;
}

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

QString perspectiveValue(Format format)
{
    // 47 = LWJGL2 的 KEY_V（F5 是 63，这里要换成 V）
    return format == Format::Modern ? QStringLiteral("key.keyboard.v") : QStringLiteral("47");
}

bool supportsKeybinds(const QString& mcVersion)
{
    // 1.6 起键位才叫 `key_key.*`（1.5 及更早是 `key_*` 的另一套名字）
    return atLeast(mcVersion, 1, 6);
}

bool supportsAutoJump(const QString& mcVersion)
{
    // autoJump 是 1.8 才加进来的（1.7.10 及更早没有这个键）
    return atLeast(mcVersion, 1, 8);
}

QString cloudsKey(const QString& mcVersion)
{
    if (atLeast(mcVersion, 1, 8))
        return QStringLiteral("renderClouds");
    if (atLeast(mcVersion, 1, 6))
        return QStringLiteral("clouds");
    return {};
}

bool cloudsValueQuoted(const QString& mcVersion)
{
    // 1.19 起云是可枚举的三态（FANCY / FAST / OFF），游戏自己写成 `"false"`；
    // 1.18 及更早是裸的 `false`。
    return atLeast(mcVersion, 1, 19);
}

int applyDefaults(QStringList& lines, Format format, const QString& mcVersion, const Defaults& defaults)
{
    int changed = 0;
    if (defaults.setChinese && put(lines, LANGUAGE_KEY, chineseLanguage(format)))
        ++changed;
    const bool keybinds = supportsKeybinds(mcVersion);
    if (defaults.swapSprintSneak && keybinds) {
        if (put(lines, SNEAK_KEY, sneakValue(format)))
            ++changed;
        if (put(lines, SPRINT_KEY, sprintValue(format)))
            ++changed;
    }
    if (defaults.perspectiveOnV && keybinds) {
        if (put(lines, PERSPECTIVE_KEY, perspectiveValue(format)))
            ++changed;
    }
    if (defaults.skipAccessibilityOnboarding && disableOnboarding(lines))
        ++changed;
    if (defaults.setFullscreen && put(lines, FULLSCREEN_KEY, defaults.fullscreen ? QStringLiteral("true")
                                                                                 : QStringLiteral("false")))
        ++changed;

    if (defaults.disableClouds) {
        const QString key = cloudsKey(mcVersion);
        if (!key.isEmpty()) {
            // 文件里已经有这一行就沿用它的方言（`false` / `"false"`），
            // 没有才按版本给 —— 1.19 起带引号，1.18 及更早不带。
            const int index = findLine(lines, key);
            const bool quoted = index >= 0 ? lines.at(index).section(QLatin1Char(':'), 1).startsWith(QLatin1Char('"'))
                                           : cloudsValueQuoted(mcVersion);
            if (put(lines, key, quoted ? QStringLiteral("\"false\"") : QStringLiteral("false")))
                ++changed;
        }
    }
    if (defaults.disableAutoJump && supportsAutoJump(mcVersion)) {
        if (put(lines, AUTO_JUMP_KEY, QStringLiteral("false")))
            ++changed;
    }
    if (defaults.renderDistance > 0 && atLeast(mcVersion, 1, 6)) {
        // 老版本上限是 16，新版本 32；统一夹到 [2, 32]，越界会被游戏当成非法值忽略
        const int chunks = std::clamp(defaults.renderDistance, 2, 32);
        if (put(lines, RENDER_DISTANCE_KEY, QString::number(chunks)))
            ++changed;
    }
    if (defaults.disableViewBobbing && atLeast(mcVersion, 1, 6)) {
        if (put(lines, BOB_VIEW_KEY, QStringLiteral("false")))
            ++changed;
    }
    if (defaults.brightestGamma && atLeast(mcVersion, 1, 6)) {
        if (put(lines, GAMMA_KEY, QStringLiteral("1.0")))
            ++changed;
    }
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

Defaults defaultsFromSettings(SettingsObject* settings)
{
    Defaults defaults;
    if (settings == nullptr)
        return defaults;

    defaults.setChinese = settings->get("MapOptionsForceChinese").toBool();
    defaults.swapSprintSneak = settings->get("MapOptionsSwapSprintSneak").toBool();
    defaults.setFullscreen = settings->get("MapStartFullscreen").toBool();
    defaults.fullscreen = true;
    defaults.disableClouds = settings->get("MapOptionsCloudsOff").toBool();
    defaults.disableAutoJump = settings->get("MapOptionsAutoJumpOff").toBool();
    defaults.perspectiveOnV = settings->get("MapOptionsPerspectiveV").toBool();
    defaults.renderDistance = settings->get("MapOptionsRenderDistance").toInt();
    defaults.disableViewBobbing = settings->get("MapOptionsBobOff").toBool();
    defaults.brightestGamma = settings->get("MapOptionsBrightGamma").toBool();
    return defaults;
}

void saveDefaultsToSettings(SettingsObject* settings, const Defaults& defaults)
{
    if (settings == nullptr)
        return;
    settings->set("MapOptionsForceChinese", defaults.setChinese);
    settings->set("MapOptionsSwapSprintSneak", defaults.swapSprintSneak);
    settings->set("MapStartFullscreen", defaults.setFullscreen);
    settings->set("MapOptionsCloudsOff", defaults.disableClouds);
    settings->set("MapOptionsAutoJumpOff", defaults.disableAutoJump);
    settings->set("MapOptionsPerspectiveV", defaults.perspectiveOnV);
    settings->set("MapOptionsRenderDistance", defaults.renderDistance);
    settings->set("MapOptionsBobOff", defaults.disableViewBobbing);
    settings->set("MapOptionsBrightGamma", defaults.brightestGamma);
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
        result.defaults = applyDefaults(lines, *format, mcVersion, restoreDefaults) > 0;
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
