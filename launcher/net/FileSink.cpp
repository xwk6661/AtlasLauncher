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

#include "FileSink.h"
#include <expected>

#include <QFileInfo>

#include "FileSystem.h"

#include "net/Logging.h"

namespace Net {

namespace {
constexpr qint64 kValidationBlockSize = 1 << 20;
}

auto FileSink::init(QNetworkRequest& request) -> InitResult
{
    auto result = initCache(request);
    if (!result || *result != InitType::Ok) {
        return result;
    }

    if (!FS::ensureFilePathExists(m_filename)) {
        qCCritical(taskNetLogC) << "Could not create folder for " + m_filename;
        return std::unexpected("Could not create folder");
    }

    // 上一轮的状态一律作废：可能是并行分片（分片偏移已经没意义），
    // 也可能是同一个 Request 的重试（要按 .part 的真实长度重算断点）。
    if (m_partFile.isOpen())
        m_partFile.close();
    if (m_parallel) {
        m_parallel = false;
        m_parallelSize = 0;
    }

    m_partPath = m_filename + QStringLiteral(".part");
    m_partFile.setFileName(m_partPath);

    // 分片模式的临时文件是**预分配**的（先 resize 到总长度），失败时留下的是"满尺寸但有空洞"
    // 的文件 —— 绝不能当成顺序下载的断点（会 Range 到文件末尾、或者把空洞当成数据）。
    QFile::remove(m_filename + QStringLiteral(".part.parallel"));

    const qint64 existing = QFileInfo(m_partPath).size();
    m_resumeFrom = 0;
    m_rangeRequested = false;

    if (existing > 0) {
        // 断点续传：这一轮只要剩下的部分。
        //
        // 为什么必须这样：这条线路上 CDN 经常在传到一半时 `Connection closed`
        // （实测 19 MB 的 client.jar 连炸 6 次），而每次重试都从 0 开始的话，
        // 下载时间永远大于"连接活多久"，于是**永远下不完**（06 §36）。
        if (!m_partFile.open(QIODevice::ReadWrite)) {
            const auto error = QString("Could not reopen %1 for resuming: %2").arg(m_partPath, m_partFile.errorString());
            qCCritical(taskNetLogC) << error;
            return std::unexpected(error);
        }
        if (!m_partFile.seek(existing)) {
            const auto error = QString("Could not seek to %1 in %2: %3").arg(existing).arg(m_partPath, m_partFile.errorString());
            qCCritical(taskNetLogC) << error;
            m_partFile.close();
            return std::unexpected(error);
        }
        m_resumeFrom = existing;
        m_rangeRequested = true;
        request.setRawHeader(QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=") + QByteArray::number(existing) + '-');
        qCDebug(taskNetLogC) << "Resuming" << m_filename << "from byte" << existing;
    } else {
        QFile::remove(m_partPath);  // 0 字节的残留没有价值
        if (!m_partFile.open(QIODevice::ReadWrite)) {
            const auto error = QString("Could not open %1 for writing: %2").arg(m_partPath, m_partFile.errorString());
            qCCritical(taskNetLogC) << error;
            return std::unexpected(error);
        }
    }

    m_wroteAnyData = m_resumeFrom > 0;
    initAllValidators();

    // 校验器要从头算：已有的一半也要喂进去，否则 sha1/md5 对不上。
    if (m_resumeFrom > 0 && !feedValidatorsFromDisk()) {
        qCWarning(taskNetLogC) << "Could not re-read the partial file, restarting from zero:" << m_filename;
        restartFromZero();
    }

    return InitType::Ok;
}

auto FileSink::write(const QByteArray& data) -> Result<>
{
    if (!m_partFile.isOpen()) {
        return std::unexpected(QString("Sink for %1 is not open for writing").arg(m_filename));
    }

    writeAllValidators(data);
    if (m_partFile.write(data) != data.size()) {
        const auto error = QString("Failed writing into %1: %2").arg(m_partPath, m_partFile.errorString());
        qCCritical(taskNetLogC) << error;
        return std::unexpected(error);
    }

    m_wroteAnyData = true;
    return {};
}

void FileSink::abort()
{
    if (m_partFile.isOpen())
        m_partFile.close();
    if (!m_partPath.isEmpty())
        QFile::remove(m_partPath);
    m_partPath.clear();
    m_parallel = false;
    m_parallelSize = 0;
    m_resumeFrom = 0;
    m_rangeRequested = false;
    m_wroteAnyData = false;
    failAllValidators();
}

auto FileSink::bytesWritten() const -> qint64
{
    // 并行模式会先把文件 resize 到总长度，那个 size 不代表"已经下到哪了"
    if (m_parallel || !m_partFile.isOpen())
        return 0;
    return m_partFile.size();
}

void FileSink::restartFromZero()
{
    if (m_partFile.isOpen())
        m_partFile.close();
    QFile::remove(m_partPath);

    m_partFile.setFileName(m_partPath);
    if (!m_partFile.open(QIODevice::ReadWrite)) {
        qCCritical(taskNetLogC) << "Could not reopen" << m_partPath << "after restart:" << m_partFile.errorString();
        return;
    }

    m_resumeFrom = 0;
    m_rangeRequested = false;
    m_wroteAnyData = false;
    initAllValidators();
}

auto FileSink::feedValidatorsFromDisk() -> bool
{
    QFile reader(m_partPath);
    if (!reader.open(QIODevice::ReadOnly))
        return false;

    while (true) {
        const auto block = reader.read(kValidationBlockSize);
        if (block.isEmpty())
            break;
        writeAllValidators(block);
    }
    const bool ok = reader.error() == QFileDevice::NoError;
    reader.close();
    return ok;
}

auto FileSink::writeAt(qint64 offset, const QByteArray& data) -> Result<>
{
    if (!m_parallel || !m_partFile.isOpen()) {
        return std::unexpected(QString("writeAt() on a sink that is not in parallel mode: %1").arg(m_filename));
    }
    if (!m_partFile.seek(offset)) {
        const auto error = QString("Could not seek to %1 in %2: %3").arg(offset).arg(m_partPath, m_partFile.errorString());
        qCCritical(taskNetLogC) << error;
        return std::unexpected(error);
    }
    if (m_partFile.write(data) != data.size()) {
        const auto error = QString("Failed writing into %1 at %2: %3").arg(m_partPath).arg(offset).arg(m_partFile.errorString());
        qCCritical(taskNetLogC) << error;
        return std::unexpected(error);
    }
    m_wroteAnyData = true;
    return {};
}

auto FileSink::beginParallel(qint64 totalSize) -> Result<>
{
    // 顺序模式已经开了一个 .part（可能还带着续传的偏移），这里要重建成
    // "预分配整块空间、按偏移写"的形态。
    if (m_partFile.isOpen())
        m_partFile.close();

    // 分片用独立文件：预分配 + 空洞的语义和"顺序下载的前缀"完全不同，
    // 混用会让下一次顺序重试把空洞当成已下载的数据（06 §36.5）。
    QFile::remove(m_filename + QStringLiteral(".part"));
    m_partPath = m_filename + QStringLiteral(".part.parallel");
    QFile::remove(m_partPath);
    m_partFile.setFileName(m_partPath);
    if (!m_partFile.open(QIODevice::ReadWrite)) {
        const auto error = QString("Could not open %1 for writing: %2").arg(m_partPath, m_partFile.errorString());
        qCCritical(taskNetLogC) << error;
        m_partPath.clear();
        return std::unexpected(error);
    }
    // 先按总长度把空间占出来：各分片可以直接 seek 到自己的偏移写
    if (!m_partFile.resize(totalSize)) {
        const auto error = QString("Could not reserve %1 bytes in %2: %3").arg(totalSize).arg(m_partPath, m_partFile.errorString());
        qCCritical(taskNetLogC) << error;
        abort();
        return std::unexpected(error);
    }

    m_parallelSize = totalSize;
    m_parallel = true;
    m_resumeFrom = 0;
    m_rangeRequested = false;
    m_wroteAnyData = false;
    m_partFile.seek(0);
    return {};
}

auto FileSink::validateSequential(bool commit) -> Result<>
{
    // 分段写入时数据没经过校验器（各分片乱序到达），这里按顺序把整份文件喂一遍，
    // sha1 / md5 才能对得上。
    QFile reader(m_partPath);
    if (!reader.open(QIODevice::ReadOnly)) {
        return std::unexpected(QString("Could not reopen %1 for validation: %2").arg(m_partPath, reader.errorString()));
    }
    while (true) {
        const auto block = reader.read(kValidationBlockSize);
        if (block.isEmpty()) {
            if (reader.error() != QFileDevice::NoError)
                return std::unexpected(QString("Failed reading %1 for validation: %2").arg(m_partPath, reader.errorString()));
            break;
        }
        writeAllValidators(block);
    }
    reader.close();

    if (!commit)
        return {};
    return finalizeAllValidators();
}

auto FileSink::expectedTotalBytes(QNetworkReply& reply) const -> qint64
{
    bool ok = false;
    const qint64 contentLength = reply.header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
    if (!ok || contentLength < 0)
        return -1;

    // 206 只是"从 m_resumeFrom 开始的那一段"，完整长度要把前缀加回来。
    // 200 时如果之前带过 Range，说明服务器没理它 —— 调用方已经 restartFromZero()，
    // 这里按整份算。压缩传输（Content-Encoding）下长度对不上，所以只对 206 强校验。
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 206)
        return m_resumeFrom + contentLength;
    return contentLength;
}

auto FileSink::finalize(QNetworkReply& reply) -> Result<>
{
    const int statusCode = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // this leaves out 304 Not Modified
    const bool gotFile = statusCode == 200 || statusCode == 203;

    if (m_parallel) {
        // 分段模式：关文件 → 整份过校验器 → 替换到目标路径
        if (m_partFile.isOpen() && !m_partFile.flush()) {
            const auto error = QString("Failed to flush %1: %2").arg(m_partPath, m_partFile.errorString());
            qCCritical(taskNetLogC) << error;
            abort();
            return std::unexpected(error);
        }
        if (m_partFile.isOpen())
            m_partFile.close();

        if (m_partFile.size() != m_parallelSize) {
            const auto error =
                QString("Incomplete download to %1: %2 of %3 bytes").arg(m_filename).arg(m_partFile.size()).arg(m_parallelSize);
            qCCritical(taskNetLogC) << error;
            abort();
            return std::unexpected(error);
        }

        auto result = validateSequential(true);
        if (!result) {
            abort();
            return result;
        }

        if (!QFile::remove(m_filename) && QFile::exists(m_filename)) {
            const auto error = QString("Could not replace %1").arg(m_filename);
            qCCritical(taskNetLogC) << error;
            abort();
            return std::unexpected(error);
        }
        if (!QFile::rename(m_partPath, m_filename)) {
            const auto error = QString("Could not move %1 to %2").arg(m_partPath, m_filename);
            qCCritical(taskNetLogC) << error;
            abort();
            return std::unexpected(error);
        }
        m_partPath.clear();
        m_parallel = false;
        m_wroteAnyData = true;
        return finalizeCache(reply);
    }

    // ---- 顺序模式（含续传） ----
    if (m_partFile.isOpen()) {
        if (!m_partFile.flush()) {
            const auto error = QString("Failed to flush %1: %2").arg(m_partPath, m_partFile.errorString());
            qCCritical(taskNetLogC) << error;
            return std::unexpected(error);
        }
        m_partFile.close();
    }

    const qint64 have = QFileInfo(m_partPath).size();

    // 服务器答了 304 / 什么都没写：目标位置上原有的文件就是最新版，别动它。
    if (have == 0 && !m_wroteAnyData && !gotFile) {
        QFile::remove(m_partPath);
        return finalizeCache(reply);
    }

    // 206 的长度是可校验的：短了就说明这段没传完，把 .part 留着等下一轮续传。
    const qint64 expected = expectedTotalBytes(reply);
    if (statusCode == 206 && expected >= 0 && have != expected) {
        return std::unexpected(QString("Incomplete download to %1: %2 of %3 bytes").arg(m_filename).arg(have).arg(expected));
    }

    auto valid = finalizeAllValidators();
    if (!valid) {
        // 内容确实坏了：留着只会让下一轮"续"出更坏的文件
        QFile::remove(m_partPath);
        m_partPath.clear();
        return valid;
    }

    if (!QFile::remove(m_filename) && QFile::exists(m_filename)) {
        const auto error = QString("Could not replace %1").arg(m_filename);
        qCCritical(taskNetLogC) << error;
        return std::unexpected(error);
    }
    if (!QFile::rename(m_partPath, m_filename)) {
        const auto error = QString("Could not move %1 to %2").arg(m_partPath, m_filename);
        qCCritical(taskNetLogC) << error;
        return std::unexpected(error);
    }

    m_partPath.clear();
    m_resumeFrom = 0;
    m_rangeRequested = false;
    m_wroteAnyData = true;
    return finalizeCache(reply);
}

bool FileSink::hasLocalData()
{
    QFileInfo info(m_filename);
    return info.exists() && info.size() != 0;
}
}  // namespace Net
