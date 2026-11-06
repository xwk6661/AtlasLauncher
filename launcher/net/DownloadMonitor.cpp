// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas Launcher - Minecraft launcher fork with a map-first workflow
 *  Copyright (C) 2026 Atlas contributors
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

#include "DownloadMonitor.h"

#include <algorithm>

#include "net/NetJob.h"
#include "net/Request.h"

namespace Net {

namespace {
/// 速度平滑系数：越小越稳、越大越跟手
constexpr double kSmoothing = 0.35;
/// 两次采样间隔小于这个值就不更新（界面偶尔会连着重绘）
constexpr qint64 kMinSampleMs = 120;
}  // namespace

DownloadMonitor::DownloadMonitor(QObject* parent) : QObject(parent)
{
    m_clock.start();
}

auto DownloadMonitor::instance() -> DownloadMonitor*
{
    static DownloadMonitor monitor;
    return &monitor;
}

void DownloadMonitor::registerJob(NetJob* job)
{
    if (job != nullptr)
        m_jobs.insert(job);
}

void DownloadMonitor::unregisterJob(NetJob* job)
{
    m_jobs.remove(job);
}

auto DownloadMonitor::snapshot() -> DownloadSnapshot
{
    DownloadSnapshot out;

    for (auto* job : m_jobs) {
        // 只统计"正在跑"的：已经跑完但还没析构的 NetJob 还留着 m_done，
        // 把它算进来会让"文件 45 / 320"里的总数越来越大。
        if (!job->isRunning())
            continue;
        out.filesTotal += job->size();
        out.filesDone += job->doneCount();
        for (const auto& item : job->activeDownloads()) {
            out.bytesDone += item.done;
            if (item.total > 0)
                out.bytesTotal += item.total;
            out.items.append({ item.key, item.name, item.done, item.total, item.parallel, item.chunks, 0.0 });
        }
    }
    out.running = out.items.size();
    out.active = out.running > 0;

    // ---- 速度：全局与单文件各算一份 ----
    const qint64 now = m_clock.elapsed();

    if (m_lastSampleMs < 0) {
        // 第一次采样只能建立基线：没有参照算不出速度。
        // （原来这里 dt 恒为 0、于是永远落在 else 分支，速度一直是 0 —— 右上角空白就是这个原因）
        m_lastBytes = out.bytesDone;
        m_lastSampleMs = now;
        for (auto& item : out.items) {
            m_itemSamples.insert(item.key, { item.done, now });
            item.speed = 0;
        }
    } else if (const qint64 dt = now - m_lastSampleMs; dt >= kMinSampleMs) {
        const double instant = double(out.bytesDone - m_lastBytes) * 1000.0 / double(dt);
        // 指数平滑，避免数字跳得看不清
        m_speed = kSmoothing * instant + (1.0 - kSmoothing) * m_speed;
        m_lastBytes = out.bytesDone;
        m_lastSampleMs = now;

        QHash<QString, QPair<qint64, qint64>> next;
        QSet<QString> alive;
        for (auto& item : out.items) {
            alive.insert(item.key);
            const auto prev = m_itemSamples.constFind(item.key);
            if (prev != m_itemSamples.constEnd()) {
                const qint64 prevBytes = prev->first;
                const qint64 prevMs = prev->second;
                const qint64 itemDt = now - prevMs;
                if (itemDt >= kMinSampleMs) {
                    const double inst = double(item.done - prevBytes) * 1000.0 / double(itemDt);
                    const double old = m_itemSpeed.value(item.key, std::max(0.0, inst));
                    m_itemSpeed[item.key] = kSmoothing * std::max(0.0, inst) + (1.0 - kSmoothing) * old;
                }
            }
            next.insert(item.key, { item.done, now });
            item.speed = m_itemSpeed.value(item.key, 0.0);
        }
        m_itemSamples = next;
        // 已经不在下载的条目丢掉，避免越积越多
        for (auto it = m_itemSpeed.begin(); it != m_itemSpeed.end();) {
            if (!alive.contains(it.key()))
                it = m_itemSpeed.erase(it);
            else
                ++it;
        }
    } else {
        out.speed = m_speed;
        for (auto& item : out.items)
            item.speed = m_itemSpeed.value(item.key, 0.0);
    }

    out.speed = std::max(0.0, m_speed);

    // 快的排前面：弱网下"哪几个在动"比"第几个"更有信息量
    std::stable_sort(out.items.begin(), out.items.end(),
                     [](const DownloadItem& a, const DownloadItem& b) { return a.speed > b.speed; });

    return out;
}

}  // namespace Net
