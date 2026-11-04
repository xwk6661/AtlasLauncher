// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
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

#include <QFile>

#include "PSaveFile.h"
#include "Sink.h"

#include <utility>

namespace Net {
class FileSink : public Sink {
   public:
    explicit FileSink(QString filename) : m_filename(std::move(filename)) {};
    ~FileSink() override = default;

   public:
    InitResult init(QNetworkRequest& request) override;
    Result<> write(const QByteArray& data) override;
    Result<> finalize(QNetworkReply& reply) override;
    void abort() override;

    auto hasLocalData() -> bool override;

    /**
     * 切到"分段并发"模式：把目标文件当成一块可直接定位写入的空间。
     *
     * 为什么要这个：这条线路上**每条连接**被限速在 60 KB/s 左右（实测 1 条 60 KB/s、
     * 8 条 487 KB/s、32 条 1.19 MB/s），PCL2 / HMCL 快就是靠多连接。所以大文件要能
     * 由多个 Range 请求同时写进同一个文件的不同偏移。
     *
     * 与顺序模式的区别只有三处：
     * 1. 数据写到 `<目标>.part`（普通 QFile，可 seek），不是 QSaveFile 的临时文件；
     * 2. `writeAt()` 按偏移写，`write()` 在分段模式下不可用；
     * 3. `finalize()` 先关文件、把整份内容过一遍校验器，再原子替换到目标路径。
     * 中途失败时目标位置上原有的好文件不受影响（和顺序模式一致）。
     */
    Result<> beginParallel(qint64 totalSize);
    Result<> writeAt(qint64 offset, const QByteArray& data);
    bool isParallel() const { return m_parallel; }

   protected:
    virtual InitResult initCache(QNetworkRequest&) { return InitType::Ok; }
    virtual Result<> finalizeCache(QNetworkReply& /*reply*/) { return {}; }

    /// 分段模式下把整个 part 文件按顺序喂给校验器，再问它们认不认
    Result<> validateSequential(bool commit);

   protected:
    QString m_filename;
    bool m_wroteAnyData = false;
    std::unique_ptr<PSaveFile> m_outputFile;

    bool m_parallel = false;
    qint64 m_parallelSize = 0;
    QFile m_partFile;
    QString m_partPath;
};
}  // namespace Net
