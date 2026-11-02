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

#include <QHash>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

#include "MapTypes.h"

namespace maps {

/**
 * DataVersion 对照表（版本号 <-> 数据版本 <-> 资源包格式）。
 *
 * 数据来自 AIO 的 `dataversion-table.json`（mcmeta 汇总 + 人工补齐的老版本），
 * 以 Qt 资源形式随二进制发布，**运行时只读**。
 *
 * 为什么不在运行时去拉 mcmeta：判定必须离线可用，且表是纯静态的，一次读入 O(1) 查询即可。
 */
class DataVersionTable {
   public:
    /// 全局单例（首次访问时从 `:/maps/dataversion.json` 加载）
    static const DataVersionTable& instance();

    /// 从指定文件加载；失败返回 false 并保留原因（测试与排障用）
    bool loadFromFile(const QString& path, QString* error = nullptr);

    bool isLoaded() const { return m_loaded; }
    QString generatedAt() const { return m_generatedAt; }

    bool isKnownVersion(const QString& id) const { return m_byVersion.contains(id); }
    std::optional<DataVersionEntry> entry(const QString& id) const;
    /// DataVersion -> 版本号；查不到返回空串
    QString versionForDataVersion(int dataVersion) const;
    const QStringList& releases() const { return m_releases; }

    /// 版本号分段解析：1.12.2 -> {1,12,2}；快照名返回 nullopt
    static std::optional<std::array<int, 3>> parseReleaseKey(const QString& id);
    /// 版本号比较；非正式版按字符串比较（与 AIO 的 fallback 一致）
    static int compareRelease(const QString& a, const QString& b);
    /// 版本 -> 所需 Java 大版本；非正式版返回 -1
    static int javaMajorFor(const QString& mcVersion);
    /// 该版本是否原生支持 --quickPlaySingleplayer（1.20 引入）
    static bool supportsQuickPlay(const QString& mcVersion);

    /// 不高于给定版本的最接近正式版（保守降级用）；没有则返回空串
    QString nearestReleaseAtOrBelow(const QString& id) const;

   private:
    DataVersionTable() = default;

    bool m_loaded = false;
    QString m_generatedAt;
    QHash<QString, DataVersionEntry> m_byVersion;
    QHash<int, QString> m_byDataVersion;
    QStringList m_releases;
};

}  // namespace maps
