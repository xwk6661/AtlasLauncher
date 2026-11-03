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
#include "MapQuitWatcher.h"

#include <QDebug>
#include <QTimer>

#include "Application.h"
#include "BaseInstance.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace maps {
namespace {

/// 玩家离开存档（集成服务端开始收摊）—— 这一行在 1.20+ 的日志里一定会出现
constexpr const char* LEAVE_WORLD_MARKER = "Stopping singleplayer server as player logged out";
/// 区块全部落盘。不同版本类名变过（ThreadedAnvilChunkStorage → ChunkStorage），只认后半句
constexpr const char* SAVED_MARKER = "All dimensions are saved";
/// 等不到落盘标记时的兜底：存档卡住也不能把玩家永久晾在标题画面
constexpr int ARM_TIMEOUT_MS = 10000;
/// 决定要关之后给日志/自动存档留的余量
constexpr int FIRE_DELAY_MS = 700;
/// 发了 WM_CLOSE 还赖着不走，就强制结束
constexpr int FORCE_KILL_MS = 8000;

#ifdef Q_OS_WIN
int closeWindowsOfPid(qint64 pid)
{
    struct Context {
        DWORD pid;
        int count;
    } context{ static_cast<DWORD>(pid), 0 };

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(param);
            DWORD windowPid = 0;
            GetWindowThreadProcessId(hwnd, &windowPid);
            if (windowPid != ctx->pid)
                return TRUE;
            // 只要顶层可见窗口：GLFW 还会建隐藏的消息窗、辅助窗
            if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
                return TRUE;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ++ctx->count;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.count;
}
#endif

}  // namespace

void MapQuitWatcher::attach(MinecraftInstance* instance)
{
    if (instance == nullptr)
        return;
    // 用「有没有这个子对象」判断，而不是实例属性：属性一旦置上就永远为真，
    // 万一对象被销毁（父对象重建等），后续启动会以为已经挂过，于是再也不挂 —— 表现为
    // 「第一次能自动关，第二次起静默失效」（2026-11-03 实测）。
    if (instance->findChild<MapQuitWatcher*>(QString(), Qt::FindDirectChildrenOnly) != nullptr)
        return;
    instance->setProperty("mapQuitWatcherInstalled", true);
    qDebug() << "Map quit watcher: attached to" << instance->id();
    new MapQuitWatcher(instance);
}

MapQuitWatcher::MapQuitWatcher(MinecraftInstance* instance) : QObject(instance), m_instance(instance)
{
    m_killTimer = new QTimer(this);
    m_killTimer->setSingleShot(true);
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        if (m_instance->isRunning()) {
            qDebug() << "Map quit watcher: game did not close by itself, killing" << m_instance->id();
            APPLICATION->kill(m_instance);
        }
    });

    // 每 300ms 核对一次「连的是不是当前那个 LaunchTask」。信号来得太早、指针太旧，
    // 只靠信号必然漏（见头文件注释）；这里只是指针比较，开销可以忽略。
    m_recheckTimer = new QTimer(this);
    m_recheckTimer->setInterval(300);
    connect(m_recheckTimer, &QTimer::timeout, this, &MapQuitWatcher::hookTask);

    connect(instance, &BaseInstance::runningStatusChanged, this, &MapQuitWatcher::onRunningChanged);
    // 已经跑起来了（例如从实例窗口重启）就立刻接上
    if (instance->isRunning())
        onRunningChanged(true);
}

void MapQuitWatcher::hookTask()
{
    auto* task = m_instance->getLaunchTask();
    if (task == nullptr)
        return;  // 任务还没建：等下一次核对
    if (task == m_hookedTask && m_logConnection)
        return;  // 已经连在实例当前的 LaunchTask 上

    if (m_logConnection)
        disconnect(m_logConnection);
    m_logConnection = connect(task, &LaunchTask::gameLogLine, this, &MapQuitWatcher::onGameLogLine);
    m_hookedTask = task;

    const qint64 pid = task->pid();
    qDebug() << "Map quit watcher: hooked to launch task" << static_cast<const void*>(task) << "pid" << pid << "of"
             << m_instance->id();
}

void MapQuitWatcher::onRunningChanged(bool running)
{
    m_armed = false;
    m_fired = false;
    m_killTimer->stop();

    if (!running) {
        m_recheckTimer->stop();
        if (m_logConnection)
            disconnect(m_logConnection);
        m_hookedTask.clear();
        return;
    }
    hookTask();
    m_recheckTimer->start();
}

void MapQuitWatcher::onGameLogLine(const QString& line)
{
    if (m_fired || !m_instance->isRunning())
        return;

    // 玩家可能中途把「退出存档后关闭游戏」关掉，这里要读当下的值
    if (!m_instance->settings()->get("MapSafeExit").toBool())
        return;

    if (!m_armed) {
        if (!line.contains(QLatin1String(LEAVE_WORLD_MARKER)))
            return;
        m_armed = true;
        m_armedAt.start();
        qDebug() << "Map quit watcher: player left the world, waiting for the save marker" << m_instance->id();
        QTimer::singleShot(ARM_TIMEOUT_MS, this, [this] {
            if (m_armed && !m_fired) {
                qDebug() << "Map quit watcher: no save marker in time, closing anyway";
                fire();
            }
        });
        return;
    }

    // 先看到「离开存档」，再看到「全部落盘」才动手
    if (line.contains(QLatin1String(SAVED_MARKER)))
        fire();
}

void MapQuitWatcher::fire()
{
    if (m_fired)
        return;
    m_fired = true;
    // 落盘标记往往比最后一笔写入早一点点，留一点余量再关
    QTimer::singleShot(FIRE_DELAY_MS, this, &MapQuitWatcher::closeGame);
}

void MapQuitWatcher::closeGame()
{
    if (!m_instance->isRunning())
        return;

    auto* task = m_instance->getLaunchTask();
    const qint64 pid = task != nullptr ? task->pid() : -1;

#ifdef Q_OS_WIN
    const int closed = pid > 0 ? closeWindowsOfPid(pid) : 0;
    if (closed > 0) {
        qDebug() << "Map quit watcher: asked the game window to close, pid" << pid;
        // 走游戏自己的退出流程；真的不动再强制结束
        m_killTimer->start(FORCE_KILL_MS);
        return;
    }
#else
    Q_UNUSED(pid)
#endif

    // 找不到窗口（无边框/被包装器接管）就直接让它退；标题画面没有任何未落盘数据
    qDebug() << "Map quit watcher: no game window found, terminating instance" << m_instance->id();
    APPLICATION->kill(m_instance);
}

}  // namespace maps
