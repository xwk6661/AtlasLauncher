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

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace maps {

/**
 * 资源库：把 `assets` / `libraries` / `java`（Mojang 官方 Java 运行时）挪到指定目录。
 *
 * 做法不是目录联接，而是**真搬**：文件移过去，启动器直接读新路径
 * （见 launcher/ResourcePaths.h）。好处是换机器、备份、给别的启动器复用都只是普通目录，
 * 没有 reparse point 那种"看起来在、其实指别处"的模糊状态。
 *
 * 代价：改完要重启启动器 —— assets/libraries 的绝对路径在启动时就进了 meta cache。
 */
class ResourceStore {
   public:
    /// 三个资源目录的固定名（与 Prism 的相对路径一致，不能改）
    static QStringList managedNames();

    /// 一个资源目录在某个候选位置里的存在情况
    struct Candidate {
        QString path;
        bool assets = false;
        bool libraries = false;
        bool java = false;
        bool any() const { return assets || libraries || java; }
    };

    /// 扫描本机常见位置（当前数据目录、官方 .minecraft、别的 Prism/MultiMC…）
    static QList<Candidate> scanCandidates();

    using ProgressFn = std::function<void(int done, int total, const QString& currentFile)>;

    /**
     * 迁移到 `sharedRoot` 下：把数据目录里的三份资源搬过去（`move` 为 false 时只切路径不搬）。
     * 同盘用改名（瞬间完成），跨盘退化为逐个复制后删源。
     */
    static QStringList apply(const QString& sharedRoot, bool move, const ProgressFn& progress);
};

}  // namespace maps
