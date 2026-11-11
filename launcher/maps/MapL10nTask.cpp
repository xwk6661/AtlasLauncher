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
#include "MapL10nTask.h"

#include <QMetaObject>
#include <QThreadPool>
#include <QtConcurrent>

namespace maps::l10n {

MapL10nTask::MapL10nTask(QString worldRoot, QString mcVersion, bool deep)
    : m_worldRoot(std::move(worldRoot)), m_mcVersion(std::move(mcVersion)), m_deep(deep)
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &MapL10nTask::onScanFinished);
}

bool MapL10nTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_aborted.store(true);
    return true;
}

void MapL10nTask::executeTask()
{
    setStatus(m_deep ? tr("正在扫描区块…") : tr("正在读取资源包与数据包…"));
    setProgress(0, 0);

    // 立刻返回，别在这里干活：start() 是在调用线程上直接调 executeTask 的
    m_watcher.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        m_result = scanFast(m_worldRoot, m_mcVersion);

        if (m_aborted.load() || !m_result.isValid() || !m_deep)
            return;

        const bool finished = scanRegions(m_result, [this](int done, int total) {
            if (m_aborted.load())
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    setProgress(done, total);
                    setStatus(tr("正在扫描区块… %1 / %2").arg(done).arg(total));
                },
                Qt::QueuedConnection);
            return true;
        });
        m_regionsCompleted.store(finished);
    }));
}

void MapL10nTask::onScanFinished()
{
    if (m_aborted.load()) {
        emitAborted();
        return;
    }
    if (!m_result.isValid()) {
        emitFailed(m_result.error.isEmpty() ? tr("体检失败：没有读到任何内容。") : m_result.error);
        return;
    }
    // 要了深扫却没跑完（又不是取消），按失败处理，别让 UI 拿到半份数据当完整结论
    if (m_deep && !m_regionsCompleted.load()) {
        emitFailed(tr("区块扫描提前中断，结果不完整。"));
        return;
    }
    emitSucceeded();
}

}  // namespace maps::l10n
