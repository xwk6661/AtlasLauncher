// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas - Minecraft Launcher
 *  Copyright (c) 2026 Atlas contributors
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

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace maps {

struct CleanupStats {
    qint64 removedParallelPartFiles = 0;
    qint64 removedStalePartFiles = 0;
    qint64 removedTmpInstanceDirs = 0;
    qint64 freedBytes = 0;
};

class MapDiskCleanup {
   public:
    /**
     * 执行启动期磁盘碎片清理：
     * 1. instances/.tmp 孤儿临时目录（上次意外崩溃/强杀留下的半截导入）
     * 2. *.part.parallel 与 *.part-*（分片模式的不可断点临时空洞文件与临时私有占用文件）
     * 3. 超过 maxAgeDays 天未修改的 *.part 悬挂断点文件（默认 3 天）
     */
    static CleanupStats cleanStaleResidues(int maxAgeDays = 3);

    /// 清理指定目录树（方便单元测试与独立扫描）
    static CleanupStats cleanDirectoryTree(const QString& rootDir, int maxAgeDays = 3);

    /// 异步非阻塞执行清理（在 QThreadPool::globalInstance() 中运行）
    static void cleanAsync(int maxAgeDays = 3);
};

}  // namespace maps
