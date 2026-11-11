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

#include <QFutureWatcher>
#include <QString>

#include <atomic>

#include "MapL10nProbe.h"
#include "tasks/Task.h"

namespace maps::l10n {

/**
 * 汉化体检任务：把 `scanFast` / `scanRegions` 包成带进度与取消的 Task。
 *
 * **`Task::start()` 是同步执行 `executeTask()` 的**（就在调用它的线程上跑），
 * 所以扫描绝对不能写在 `executeTask` 里 —— 那样 90 秒的区块扫描会把界面卡成
 * 「未响应」（踩过这个坑）。真正的活扔给 `QtConcurrent::run`，用 `QFutureWatcher` 收尾，
 * 进度通过 `QMetaObject::invokeMethod` 排队回主线程。
 *
 * 分两档由 `deep` 决定：
 *   · false —— 只读资源包与数据包，毫秒到秒级，页面打开时静默跑一次；
 *   · true  —— 追加区块扫描，几十秒级，走 `ProgressDialog` 显示进度条。
 *
 * 结果只读，取消不会留下任何东西。
 */
class MapL10nTask : public Task {
    Q_OBJECT

   public:
    MapL10nTask(QString worldRoot, QString mcVersion, bool deep);
    ~MapL10nTask() override = default;

    void executeTask() override;
    bool abort() override;

    /// 任务结束后取结果（失败时 `result().isValid()` 为 false）
    const ProbeResult& result() const { return m_result; }
    bool isDeep() const { return m_deep; }

   private slots:
    void onScanFinished();

   private:
    QString m_worldRoot;
    QString m_mcVersion;
    bool m_deep = false;
    std::atomic<bool> m_aborted{ false };
    /// 区块扫描是否真的跑完（没跑完又没取消 = 中途出错）
    std::atomic<bool> m_regionsCompleted{ false };
    ProbeResult m_result;
    QFutureWatcher<void> m_watcher;
};

}  // namespace maps::l10n
