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
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

class SettingsObject;

namespace maps::options {

/**
 * `options.txt` 的写法在 **1.13** 断过一次，两代完全不兼容：
 *
 * | | ≤1.12（LWJGL2） | ≥1.13 |
 * | --- | --- | --- |
 * | 键位 | 数字键码 `key_key.sneak:29` | 名字 `key_key.sneak:key.keyboard.left.control` |
 * | 语言 | `lang:zh_CN` | `lang:zh_cn` |
 *
 * 依据是库里真实实例的 options.txt：Terra Restore（1.8.1）与盘灵古域（1.8.9）是数字码 +
 * `zh_CN`，Final Paradox（1.16.5）与 Torre de Piedra（1.21.1）是名字 + `zh_cn`。
 * 写错一代不是"没生效"这么轻——1.8 读到 `key.keyboard.*` 会当成未知键位丢掉，
 * 玩家的蹲/跑直接回默认。所以版本拿不准时**什么都不写**。
 */
enum class Format {
    Legacy,  ///< ≤1.12：LWJGL2 数字键码、`zh_CN`
    Modern,  ///< ≥1.13：`key.keyboard.*` 名字、`zh_cn`
};

/**
 * 判 `options.txt` 该用哪一代写法。
 *
 * 拿不准（空串 / `unknown` / 无法识别的形态）返回 `nullopt` —— 调用方应当放弃改写。
 * 认 `1.7.x` 这种区间写法（按 1.7 判），也认 `26.2` 这种按年份的新版本号（按现代判）。
 */
std::optional<Format> formatForVersion(const QString& mcVersion);

/// 简体中文在该代写法里的语言码
QString chineseLanguage(Format format);

/// 「Shift 奔跑、Ctrl 蹲下」——即与 MC 默认值对调后的 `key_key.sneak` 值
QString sneakValue(Format format);
/// 同上，`key_key.sprint` 的值
QString sprintValue(Format format);
/// `key_key.togglePerspective` 的值（切换视角键，现代写 `key.keyboard.v`，老版写 47）
QString perspectiveValue(Format format);

/**
 * 版本能力：某一项在**这个版本**上到底有没有那个键。
 *
 * 为什么要单独判：`options.txt` 里不存在的键会被游戏当成坏行跳过
 * （`Skipping bad option`），不会坏档，但玩家会以为"勾了没生效"。
 * 所以宁可少写一条，也不写一个该版本没有的键。
 *
 * 判不出（空串 / `unknown` / 认不出的形态）时全部返回 false —— 与"判不出代就一条都不写"同一条原则。
 */
bool supportsKeybinds(const QString& mcVersion);
/// `autoJump` 是 1.8 才有的
bool supportsAutoJump(const QString& mcVersion);
/// 云的键名：1.8+ 叫 `renderClouds`，更早叫 `clouds`；判不出返回空串
QString cloudsKey(const QString& mcVersion);
/**
 * 关云的取值要不要带引号。
 *
 * 1.19 起游戏自己写的是 `renderClouds:"false"`（三态字符串），1.18 及更早是 `renderClouds:false`。
 * 两种写法在**本文件里**都会被读成"关"，但跟着游戏自己的方言写最稳 ——
 * 而且如果文件里已经有这一行，`applyDefaults` 会直接沿用那一行的方言，不看版本。
 */
bool cloudsValueQuoted(const QString& mcVersion);

/// 导入时要写进 options.txt 的东西
struct Defaults {
    /// 把语言设成简体中文
    bool setChinese = false;
    /// 对调 Shift / Ctrl（Shift 奔跑、Ctrl 蹲下）
    bool swapSprintSneak = false;
    /// 关掉「无障碍引导」首屏 —— 它是 quickPlay 的死路，见下方 repairOptionsTxt 的说明
    bool skipAccessibilityOnboarding = false;
    /// 写 `fullscreen:`（地图实例默认全屏进图，见 MapSettingsWidget 里的说明）
    bool setFullscreen = false;
    /// 配合 setFullscreen 的值
    bool fullscreen = true;
    /// 云：关掉（`renderClouds` / 老版的 `clouds`）
    bool disableClouds = false;
    /// 自动跳远：关掉（`autoJump`）
    bool disableAutoJump = false;
    /// 切换视角改到 V（默认是 F5）
    bool perspectiveOnV = false;
    /// 视野距离（区块）；0 = 不写
    int renderDistance = 0;
    /// 关掉视角摇晃（`bobView`）
    bool disableViewBobbing = false;
    /// 亮度拉到最满（`gamma:1.0`）
    bool brightestGamma = false;

    bool isEmpty() const
    {
        return !setChinese && !swapSprintSneak && !skipAccessibilityOnboarding && !setFullscreen && !disableClouds &&
               !disableAutoJump && !perspectiveOnV && renderDistance <= 0 && !disableViewBobbing && !brightestGamma;
    }
};

/**
 * 从启动器设置里读「导入时的游戏设置」。
 *
 * 导入对话框把它当作初始勾选状态，玩家改完点确定后由对话框写回设置
 * （于是下一次导入记住这次的选择）；不经过对话框的入口也直接用它。
 * `settings == nullptr` 时返回全 false（等于"什么都不写"）。
 */
Defaults defaultsFromSettings(SettingsObject* settings);

/**
 * 把 `defaults` 写回启动器设置（导入对话框点确定时调用）。
 */
void saveDefaultsToSettings(SettingsObject* settings, const Defaults& defaults);

/**
 * 就地改写 options.txt 的行：键已存在就替换，不存在就追加，其余行一个字节不动。
 *
 * 幂等——同一份内容跑两遍，第二遍返回 0（`put` 只在值真的变了才算改动）。
 *
 * @param mcVersion 用来判「这个版本有没有这个键」（见 supportsAutoJump / cloudsKey）。
 *        判不出时只写语言、键位对调、全屏这些跨版本稳定的项。
 * @return 实际改动的行数
 */
int applyDefaults(QStringList& lines, Format format, const QString& mcVersion, const Defaults& defaults);

/**
 * 从 options.txt 的现有行里判写法，用于版本判不出来时兜底。
 *
 * 信号：出现 `key_key.*:key.keyboard.*` → Modern；出现纯数字键位 → Legacy。
 * 都没有则返回 `nullopt`（新实例的 options.txt 本来就不存在，这条兜底多半用不上）。
 */
std::optional<Format> detectFormat(const QStringList& lines);

/**
 * 补/修首行的 `version:<DataVersion>`。
 *
 * **这一行是启动失败的元凶，不是装饰。** MC 1.13 起会在 options.txt 第一行写
 * `version:<DataVersion>`；**缺这一行时数据修复器把整份文件当成"最老的格式"**，
 * 于是 `OptionsKeyLwjgl3Fix` 会把 `key_key.*` 的值当数字键码 `Integer.parseInt` 一把 ——
 * 而我们写的正是 `key.keyboard.left.control`，直接
 *
 *     java.lang.NumberFormatException: For input string: "key.keyboard.left.control"
 *     [Render thread/ERROR]: Failed to load options
 *
 * 结果：**整份设置全丢**（中文语言、Shift/Ctrl 互换一起没），并且 `onboardAccessibility`
 * 回到默认 `true`，游戏第一屏变成无障碍引导 —— 那一页会把 `--quickPlaySingleplayer`
 * 挡在外面，玩家看到的就是「点进去没有反应」（2026-11-12 复现）。
 *
 * 只有 Modern 需要：1.8 的 options.txt 里没有这个键，写了反而多一条 `Skipping bad option`。
 *
 * @param dataVersion 该实例的 DataVersion（取不到就传 <=0，函数什么都不做）
 * @return 是否真的改了
 */
bool putVersionStamp(QStringList& lines, Format format, int dataVersion);

/// 把 `onboardAccessibility` 写成 `false`（键不存在就追加）；返回是否真的改了
bool disableOnboarding(QStringList& lines);

/// 一次读-改-写 options.txt 的结果
struct RepairResult {
    bool missing = false;    ///< 文件不存在（新实例，本来就没有 —— 不算错）
    bool changed = false;    ///< 是否落盘
    bool version = false;    ///< 补了 version
    bool onboarding = false; ///< 关了无障碍引导
    bool defaults = false;   ///< 补回了语言 / 键位 / 全屏
    /// 修之前文件里有没有 `version:`。没有 = 这份文件是我们自己写的，
    /// 而且游戏从没成功读进去过（读进去就会把 version 写回来）。
    bool hadVersion = true;
    QString error;           ///< 非空 = 读写失败
};

/**
 * 就地修一份 options.txt：补 `version`、按需关掉无障碍引导。
 *
 * 幂等 —— 已经是好的文件再跑一遍返回 `changed == false`。
 * 拿不准版本（`formatForVersion` 与 `detectFormat` 都给不出结论）时一行都不碰。
 *
 * @param restoreDefaults 只在「文件里没有 version」时应用：那种文件是导入流程写的、
 *        游戏一次都没读成功过（读成功就会把 version 写回来），说明当初写进去的
 *        中文与键位已经跟着 `Failed to load options` 一起丢了 —— 正好补回去。
 *        玩家自己动过的文件（有 version）一律不碰。
 */
RepairResult repairOptionsTxt(const QString& path, const QString& mcVersion, int dataVersion, bool fixOnboarding,
                              const Defaults& restoreDefaults = {});

}  // namespace maps::options
