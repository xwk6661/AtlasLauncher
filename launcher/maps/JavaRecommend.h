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

#include <QList>
#include <QString>

class MinecraftInstance;

namespace maps {

/**
 * 本机的一个 Java 运行时。
 *
 * `path` 一定是可执行文件的绝对路径；`major` 是判出来的主版本（8 / 17 / 21…），
 * 判不出来就不进候选列表 —— 宁可显示"没找到"，也不拿未知版本去试。
 */
struct JavaCandidate {
    QString path;
    int major = 0;
    QString version;      ///< 17.0.8 / 1.8.0_402（只有大版本时写成 "17"）
    QString label;        ///< 给人看的名字：托管运行时写目录名，其余写路径
    bool managed = false;  ///< 在 <数据目录>/java 下（Mojang 托管运行时）
    bool is64 = false;
    bool pinned = false;  ///< 玩家在设置里按大版本固定过
    QString source;       ///< pinned / managed / detected / global
};

/**
 * 「这个版本需要哪个 Java」。
 *
 * `majors` 按偏好排序，空 = **判不出来**（调用方必须什么都不做，不许猜）。
 * 正常情况下来自 meta 的 `compatibleJavaMajors`（整合包/加载器会叠加自己的约束），
 * meta 还没解析时退回本文件里的版本表。
 */
struct JavaRequirement {
    QString mcVersion;
    QList<int> majors;
    QString managedName;  ///< 例：java-runtime-gamma（只有 meta 给得出）
    QString source;       ///< meta / 版本表
    QString reason;       ///< 给界面看的一句话：为什么是这个版本

    bool isKnown() const { return !majors.isEmpty(); }
};

struct JavaRecommendation {
    bool ok = false;
    JavaCandidate chosen;
    QString reason;   ///< 为什么选它（带证据：固定路径 / meta 指定 / 本地已装）
    QString problem;  ///< ok == false 时的人话
    QList<JavaCandidate> alternates;
};

/// 启动/导入前的自动选择结果
struct AutoJavaResult {
    bool applied = false;
    QString path;
    QString reason;   ///< applied 时：为什么换成这个
    QString problem;  ///< 没动的原因（已关闭 / 手选过 / 没有合适的）
};

/// 纯函数：按 MC 版本查表（meta 缺席时的兜底）。判不出返回空 majors。
JavaRequirement requirementForVersion(const QString& mcVersion);

/// 优先用 meta 的 compatibleJavaMajors / JavaName，缺失才退回版本表。
/// `mcVersionHint` 给冷启动用：那一刻组件还没解析，只有 map.json 里判过的版本可靠。
JavaRequirement requirementForInstance(MinecraftInstance* instance, const QString& mcVersionHint = {});

/// 收集本机可用的 Java：固定路径 → 托管运行时 → 已检测列表 → 全局路径
QList<JavaCandidate> collectCandidates();

/// 纯函数：只在 `requirement.majors` 里挑，绝不降级猜
JavaRecommendation recommend(const JavaRequirement& requirement, const QList<JavaCandidate>& candidates);

/// 纯函数：解析 Mojang 运行时的 `release` 文件（JAVA_VERSION / OS_ARCH）
bool parseRuntimeRelease(const QString& text, QString* version, bool* is64);

/// 纯函数："1.8.0_402" → 8、"17.0.8" → 17；认不出返回 0
int javaMajorFromVersionString(const QString& version);

/// 把选定的 Java 写进实例（OverrideJavaLocation + JavaPath + AutomaticJava）
bool applyToInstance(MinecraftInstance* instance, const JavaCandidate& candidate, bool automatic, QString* error);

/**
 * 启动/导入前调用：按版本挑一个并写进实例。
 *
 * 只在「没有一个玩家手选的路径」时动手：
 *   · `OverrideJavaLocation == false`（跟随全局，从没为这个实例选过）；
 *   · 或 `AutomaticJava == true`（上次就是自动选的，可以换）；
 *   · 或当前路径已经不存在。
 * 玩家手选且文件还在 —— 一律不动，只在日志里说明。
 */
AutoJavaResult autoSelectForInstance(MinecraftInstance* instance, const QString& mcVersionHint = {});

}  // namespace maps
