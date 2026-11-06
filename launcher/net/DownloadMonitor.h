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

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class NetJob;

namespace Net {

struct DownloadItem {
    QString key;
    QString name;
    qint64 done = 0;
    qint64 total = -1;
    bool parallel = false;
    int chunks = 0;
    double speed = 0;  // B/s，按 500ms 采样算出来的（单文件）
};

struct DownloadSnapshot {
    bool active = false;
    int filesTotal = 0;
    int filesDone = 0;
    int running = 0;
    qint64 bytesDone = 0;  // 正在下的这些文件已经收到的字节
    qint64 bytesTotal = 0;  // 已知长度之和（未开始的还没算）
    double speed = 0;       // B/s，整体
    QList<DownloadItem> items;
};

/**
 * 下载观测点：把所有"正在跑的 NetJob"聚合成一个快照，给进度界面用。
 *
 * 为什么要有它：Prism 的进度对话框只会显示"第几个文件 / 共几个"，
 * 所以下载这件事在界面上完全没有"速度"和"并行"的观感 —— 弱网下十几个文件
 * 同时在跑，用户看到的却是一条几乎不动的条（06 §41）。
 *
 * 设计上有意做成**旁路**：NetJob 只在构造/析构时登记，界面按 500ms 轮询取快照。
 * 下载逻辑本身不认识这个类，将来要删掉界面也不影响网络层。
 */
class DownloadMonitor : public QObject {
    Q_OBJECT

   public:
    static auto instance() -> DownloadMonitor*;

    void registerJob(NetJob* job);
    void unregisterJob(NetJob* job);

    /// 取当前快照（同时推进一次速度采样）
    auto snapshot() -> DownloadSnapshot;

    /// 最近一次采样的整体速度（B/s），给状态栏之类的地方用
    auto speed() const -> double { return m_speed; }

   private:
    explicit DownloadMonitor(QObject* parent = nullptr);

   private:
    QSet<NetJob*> m_jobs;
    QElapsedTimer m_clock;
    qint64 m_lastBytes = 0;
    qint64 m_lastSampleMs = -1;
    double m_speed = 0;
    /// 每个文件上一次的 (字节, 时刻)，用来算单文件速度
    QHash<QString, QPair<qint64, qint64>> m_itemSamples;
    QHash<QString, double> m_itemSpeed;
};

}  // namespace Net
