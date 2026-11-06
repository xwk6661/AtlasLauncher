// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once

#include <QtNetwork>

#include "net/Request.h"
#include "tasks/ConcurrentTask.h"

// Those are included so that they are also included by anyone using NetJob
#include "net/HttpMetaCache.h"

class NetJob : public ConcurrentTask {
    Q_OBJECT

   public:
    // TODO: delete
    using Ptr = shared_qobject_ptr<NetJob>;

    /**
     * 正在下的一个文件（给下载面板看）。
     *
     * 注意 `done/total` 是**字节**，不是"第几个文件"：`Task::progress` 的语义由子任务自己定，
     * `Net::Request` 报的是字节，所以这里直接拿来用。
     */
    struct ActiveDownload {
        QString key;      ///< 稳定键（URL），面板用它算速度
        QString name;     ///< 显示名（文件名）
        qint64 done = 0;
        qint64 total = -1;  ///< 还没拿到 Content-Length 时是 -1
        bool parallel = false;
        int chunks = 0;
    };

    explicit NetJob(const QString& jobName, QNetworkAccessManager* network, int maxConcurrent = -1);
    ~NetJob() override;

    auto size() const -> int;
    /// 已经完成的子任务数（并发任务按"文件"计数）
    auto doneCount() const -> int;
    /// 当前正在下的文件快照（顺序不保证；面板自己排序）
    auto activeDownloads() const -> QList<ActiveDownload>;

    auto canAbort() const -> bool override;
    auto addNetAction(const Net::Request::Ptr& action) -> bool;

    auto getFailedActions() -> QList<Net::Request*>;
    auto getFailedFiles() -> QList<QString>;
    void setAskRetry(bool askRetry);

   public slots:
    // Qt can't handle auto at the start for some reason?
    bool abort() override;

   protected slots:
    void emitFailed(QString reason) override;
    void executeNextSubTask() override;

   protected:
    void updateState() override;
    bool isOnline();

   private:
    QNetworkAccessManager* m_network;

    int m_try = 1;
    int m_maxTries = 3;
    bool m_askRetry = true;
};
