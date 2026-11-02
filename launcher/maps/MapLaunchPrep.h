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

#include "MapMeta.h"
#include "Result.h"

class MinecraftInstance;

namespace maps {

/**
 * 启动前三件事：选轨道、装参数、记下「期望安全退出」。
 *
 * 轨道**只认 trait**（`feature:is_quick_play_singleplayer`），不认版本号：
 * 写死 `>=1.20` 会在 Forge/NeoForge 派生版本上出错（Prism 自己也是按 trait 判的）。
 */
class MapLaunchPrep {
   public:
    enum class Track {
        QuickPlay,  ///< 1.20+ 原生 --quickPlaySingleplayer
        Agent,      ///< ≤1.19.4 走 Java Agent
        Plain,      ///< 不支持，普通启动（停在主菜单）
    };

    struct Plan {
        Track track = Track::Plain;
        QString worldFolder;
        /// 给界面看的人话（为什么选这条轨道 / 为什么不支持）
        QString reason;
        QStringList warnings;
        bool canRequestQuit = false;
        /// 三个设置项的快照（MapAutoEnter / MapSafeExit / MapEnterDelay）
        bool autoEnter = true;
        bool closeOnQuit = true;
        int enterDelay = 1;
        /// 这个实例到底是不是地图实例（有 map.json）。false 时其余字段无意义。
        bool applicable = false;
    };

    /// worldFolder 为空 = 用主世界（meta.worlds[0]）
    static Plan plan(MinecraftInstance* instance, const MapMeta& meta, const QString& worldFolder = {});

    /// 按计划落地：设置 JoinWorldOnLaunch 或注入 -javaagent
    static Result<> prepare(MinecraftInstance* instance, const MapMeta& meta, Plan* appliedPlan = nullptr,
                            const QString& worldFolder = {});

    /**
     * 所有启动入口的唯一准备点（Application::launch 里调）。
     *
     * 为什么放在 Application 而不是主窗口：启动入口有五个（原生启动按钮、双击实例、
     * 实例窗口的启动按钮、命令行/快捷方式、地图信息区的「进入地图」），
     * 只在主窗口拦路径会漏掉「从实例窗口启动」和「快捷方式启动」，
     * 老版本实例那两条路会静默停在主菜单。
     *
     * 非地图实例直接返回（applicable == false），不做任何改动。
     */
    static Plan prepareForLaunch(MinecraftInstance* instance, const QString& worldFolder = {});

    /**
     * 清掉上一次留下的 agent.log。
     *
     * 必须每次启动前清：退出判定读的是这个文件的尾部，只要它还留着上一次的
     * EXIT_SAFE，本次崩溃就会被误判成「正常落盘」，统计面板与 map.json 全错。
     */
    static void clearAgentLog(const QString& instanceRoot);

    /// 挂「启动开始 → 退出 → 回写统计」的钩子（每个实例只挂一次）
    static void installExitTracker(MinecraftInstance* instance);

    /// 从实例目录下的 agent.log 尾部判定上次退出是否安全（safe / crash / unknown）
    static QString detectExitStatus(const QString& instanceRoot);

    /**
     * 综合判定：Agent 没留标记时（quickPlay 轨道根本不注入 Agent），用启动器自己的
     * 「是否非正常退出」兜底 —— 否则每次正常退出都会把上次的 safe 覆盖成 unknown。
     */
    static QString resolveExitStatus(const QString& instanceRoot, bool crashed);

    /// 退出后回写游玩统计。status: safe / crash / unknown
    /// unknown 只更新时间，不覆盖上一次的结论（不知道就别编，别把 safe 抹成未知）
    static void recordExit(const QString& instanceRoot, const QString& status, qint64 playedSeconds = -1);

    static QString trackName(Track track);
};

}  // namespace maps
