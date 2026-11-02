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

#include <QElapsedTimer>
#include <QObject>

class QTimer;
class MinecraftInstance;

namespace maps {

/**
 * 1.20+ 那条轨道（`--quickPlaySingleplayer`）没有 Agent，没人替你关游戏：
 * 玩家在游戏里点「保存并退出」，客户端只是回到标题画面，进程还在。
 *
 * 这里补上这一环 —— **不改游戏、不注入 jar**，只读游戏自己的日志：
 *   · 看到「Stopping singleplayer server as player logged out」= 玩家离开了存档；
 *   · 再看到「All dimensions are saved」= 区块真的落盘了（这一行之后才敢关）；
 *   · 超时没等到落盘标记也会关（存档卡住时不能把玩家永久晾在标题画面）。
 *
 * 关闭方式是给游戏窗口发 `WM_CLOSE`（等同点右上角 ×，走游戏自己的退出流程），
 * 8 秒还没退才回退到启动器的强制结束。
 *
 * 为什么不用 Agent 判断：Agent 靠每版混淆符号表，本项目只带了 ≤1.19.4 的 6 张，
 * 1.20+ 没有符号可用（AIO 的 1.20+ 方案同样是「quickPlay + 只拦截退出」）。
 */
class MapQuitWatcher : public QObject {
    Q_OBJECT

   public:
    /// 只在「自动进图 + 退出即关」都开着时由 Application::launch 调用；重复调用无副作用
    static void attach(MinecraftInstance* instance);

   private:
    explicit MapQuitWatcher(MinecraftInstance* instance);

    void hookTask();
    void onRunningChanged(bool running);
    void onGameLogLine(const QString& line);
    void fire();
    void closeGame();

    MinecraftInstance* m_instance = nullptr;
    QTimer* m_killTimer = nullptr;
    QElapsedTimer m_armedAt;
    /// 已经看到「离开存档」
    bool m_armed = false;
    /// 已经决定要关（避免日志里反复出现的标记重复触发）
    bool m_fired = false;
    int m_hookRetries = 0;
};

}  // namespace maps
