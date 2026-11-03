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
#include "MapWindowFocus.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

#include <memory>

#include "BaseInstance.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"

#ifdef Q_OS_WIN
#include <windows.h>

namespace {

constexpr const char* FOCUS_TIMER_NAME = "mapGameWindowFocusTimer";

/// 至少盯这么久才允许收工（覆盖启动器进度框关闭、画廊刷新这些抢前台的时刻）
constexpr qint64 FOCUS_MIN_WATCH_MS = 30000;

/// 轮询过程中的小状态（lambda 里要可变，用 shared_ptr 传）
struct FocusState {
    /// 已经连续多少拍看到「游戏窗口在前台」
    int settle = 0;
    /// 「找到游戏窗口」这件事只记一次日志，避免 4 次/秒刷屏
    bool reportedWindow = false;
    /// 上一次看到的 pid，变化时打一行，方便排查「到底拿没拿到进程号」
    qint64 lastPid = -1;
};

struct FindContext {
    DWORD pid;
    HWND glfwWindow;
    HWND anyWindow;
};

BOOL CALLBACK findWindowProc(HWND hwnd, LPARAM param)
{
    auto* ctx = reinterpret_cast<FindContext*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid)
        return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;

    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, 64);
    const QString cls = QString::fromWCharArray(className);
    if (cls.startsWith(QLatin1String("GLFW"))) {
        ctx->glfwWindow = hwnd;
        return FALSE;  // 主窗口，不必再找
    }
    if (ctx->anyWindow == nullptr && GetWindowTextLengthW(hwnd) > 0)
        ctx->anyWindow = hwnd;
    return TRUE;
}

/// GLFW 的主窗口优先；找不到就退回「该进程任意一个可见的顶层窗口」
HWND findGameWindow(DWORD pid)
{
    FindContext ctx{ pid, nullptr, nullptr };
    EnumWindows(findWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.glfwWindow != nullptr ? ctx.glfwWindow : ctx.anyWindow;
}

/// SetForegroundWindow 对「不是前台进程」的调用方通常直接失败，先 attach 到前台线程
void giveForeground(HWND hwnd)
{
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD ourThread = GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != ourThread;
    if (attached)
        AttachThreadInput(ourThread, foregroundThread, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);

    if (attached)
        AttachThreadInput(ourThread, foregroundThread, FALSE);
}

}  // namespace
#endif

namespace maps {

void armGameWindowFocus(MinecraftInstance* instance, int timeoutMs)
{
    if (instance == nullptr)
        return;

#ifdef Q_OS_WIN
    // 上一次的定时器还没收尾（同一实例连续启动）就直接作废，避免两个一起抢焦点
    if (auto* old = instance->findChild<QTimer*>(QString::fromLatin1(FOCUS_TIMER_NAME))) {
        old->stop();
        old->deleteLater();
    }

    auto* timer = new QTimer(instance);
    timer->setObjectName(QString::fromLatin1(FOCUS_TIMER_NAME));
    timer->setInterval(250);

    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    const qint64 deadline = startedAt + timeoutMs;
    // 前台交出去之后还要盯几拍：启动器的进度框/画廊刷新可能又把前台抢回去
    auto state = std::make_shared<FocusState>();

    QObject::connect(timer, &QTimer::timeout, timer, [timer, instance, startedAt, deadline, state] {
        auto finish = [timer] {
            timer->stop();
            timer->deleteLater();
        };

        auto* launchTask = instance->getLaunchTask();
        const qint64 pid = launchTask != nullptr ? launchTask->pid() : -1;
        if (pid != state->lastPid) {
            state->lastPid = pid;
            qDebug() << "Map window focus: launch task pid is now" << pid << "for" << instance->id();
        }
        if (pid > 0) {
            HWND game = findGameWindow(static_cast<DWORD>(pid));
            if (game != nullptr) {
                if (GetForegroundWindow() == game) {
                    // 连续 3 秒（12 拍）在前台、且已经盯满 30 秒才算稳住。
                    // 为什么不能更早收工：启动器的进度框要等游戏进程起来才关（约 T+17s），
                    // 它一关就可能把前台还给主窗口 —— 那一刻正好在 quickPlay 真正 grabMouse 之前。
                    if (++state->settle >= 12 &&
                        QDateTime::currentMSecsSinceEpoch() - startedAt >= FOCUS_MIN_WATCH_MS) {
                        qDebug() << "Map window focus: game window kept the foreground, pid" << pid;
                        finish();
                    }
                    return;
                }

                const HWND foreground = GetForegroundWindow();
                DWORD foregroundPid = 0;
                if (foreground != nullptr)
                    GetWindowThreadProcessId(foreground, &foregroundPid);

                if (!state->reportedWindow) {
                    state->reportedWindow = true;
                    qDebug() << "Map window focus: game window found, pid" << pid << "foreground pid" << foregroundPid;
                }

                // 只在「前台还是我们自己」或「压根没有前台」时动手 ——
                // 用户已经切到别的程序就不抢。注意后者是真实会出现的：
                // 启动器把画廊窗口藏起来时前台可能变成空，那时更需要我们接手。
                if (foregroundPid == GetCurrentProcessId() || foregroundPid == 0) {
                    giveForeground(game);
                    if (GetForegroundWindow() == game) {
                        qDebug() << "Map window focus: gave the foreground to the game window, pid" << pid;
                        state->settle = 0;
                    }
                } else {
                    state->settle = 0;
                }
            }
        }

        if (QDateTime::currentMSecsSinceEpoch() > deadline) {
            qWarning() << "Map window focus: gave up waiting for the game window of" << instance->id()
                       << "(if the camera spins, click the game window once)";
            finish();
        }
    });

    timer->start();
    qDebug() << "Map window focus: armed for" << instance->id();
#else
    Q_UNUSED(instance)
    Q_UNUSED(timeoutMs)
#endif
}

}  // namespace maps
