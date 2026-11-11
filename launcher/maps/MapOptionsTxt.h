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

/// 导入时要写进 options.txt 的东西（对应设置页的两个开关）
struct Defaults {
    /// 把语言设成简体中文
    bool setChinese = false;
    /// 对调 Shift / Ctrl（Shift 奔跑、Ctrl 蹲下）
    bool swapSprintSneak = false;

    bool isEmpty() const { return !setChinese && !swapSprintSneak; }
};

/**
 * 就地改写 options.txt 的行：键已存在就替换，不存在就追加，其余行一个字节不动。
 *
 * 幂等——同一份内容跑两遍，第二遍返回 0（`put` 只在值真的变了才算改动）。
 * @return 实际改动的行数
 */
int applyDefaults(QStringList& lines, Format format, const Defaults& defaults);

/**
 * 从 options.txt 的现有行里判写法，用于版本判不出来时兜底。
 *
 * 信号：出现 `key_key.*:key.keyboard.*` → Modern；出现纯数字键位 → Legacy。
 * 都没有则返回 `nullopt`（新实例的 options.txt 本来就不存在，这条兜底多半用不上）。
 */
std::optional<Format> detectFormat(const QStringList& lines);

}  // namespace maps::options
