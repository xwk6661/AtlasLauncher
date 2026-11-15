// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2025 Trial97 <alexandru.tripon97@gmail.com>
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

#include <QByteArray>
#include <QFileDevice>

#include "Result.h"
#include "archive/ArchiveReader.h"

struct archive;
namespace MMCZip {

class ArchiveWriter {
   public:
    ArchiveWriter(const QString& archiveName);
    virtual ~ArchiveWriter();

    Result<> open();
    Result<> close();

    /**
     * zip 的压缩方式，只在 `open()` 之前调用有效。
     *
     * 默认 `deflate`（保持原有行为）。打包**已经压缩过的数据**（.mca 区块、jar、png）时
     * 用 `store` —— deflate 在这种数据上几乎压不动，只是白烧 CPU 和时间。
     */
    void setCompression(const QString& method) { m_compression = method; }

    Result<> addFile(const QString& fileName, const QString& fileDest);
    Result<> addFile(const QString& fileDest, const QByteArray& data);
    Result<> addFile(ArchiveReader::File* f);

    static std::unique_ptr<archive, void (*)(archive*)> createDiskWriter();

   private:
    struct archive* m_archive = nullptr;
    QString m_filename;
    QString m_format = "zip";
    QString m_compression = "deflate";
};
}  // namespace MMCZip
