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

#include <QFuture>
#include <QFutureWatcher>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#include "InstanceTask.h"
#include "MapArchiveScanner.h"
#include "MapMeta.h"
#include "MapTypes.h"
#include "tasks/Task.h"

class MinecraftInstance;

namespace maps {

/// 导入时的用户决定（覆盖自动判定）
struct MapImportOptions {
    QString displayName;
    /// 用户手动指定的版本；为空表示用判定结果
    QString forcedVersion;
    /// 用户手动指定的加载器（vanilla / forge / fabric / neoforge / quilt）
    QString forcedLoader;
    /// 用户手动指定的加载器版本
    QString loaderVersion;
    /// 选中的世界在 scan.worlds 里的下标；< 0 表示用主世界（[0]）
    int worldIndex = -1;
    /// 是否把存档内的材质包与模组一并部署（默认是）
    bool deployBundledResources = true;
};

/**
 * 导入一张地图：建实例 → 解世界 → 落材质包/模组 → 写 map.json。
 *
 * 复用 Prism 的 InstanceTask/InstanceStaging，不自己管实例目录：
 * 任务失败时 staging 由 Prism 清掉；只有「已经写进 saves/ 的半截世界」要自己删，
 * 所以世界先解到 .tmp-world 再改名（中途失败不会留下半个存档）。
 */
class MapImportTask : public InstanceTask {
    Q_OBJECT

   public:
    MapImportTask(QString archivePath, MapScanResult scan, MapImportOptions options);
    ~MapImportTask() override = default;

    /// 提交成功后由调用方取用：主存档目录名（saves/ 下的那级）
    QString primaryWorldFolder() const { return m_primaryWorldFolder; }

    /**
     * 重装：用 map.json 里记录的归档重新解压世界，实例本身保留。
     *
     * @param backupExisting 若为 true，旧存档挪到 `saves/.mcml-backup/<时间戳>/<目录>/`；
     *                       若为 false，则不创建备份直接覆盖旧存档。
     */
    static Result<> reinstallWorlds(const QString& instanceRoot, const MapMeta& meta,
                                    bool backupExisting = true,
                                    std::function<bool(qint64, qint64)> progress = nullptr);

    void executeTask() override;
    bool abort() override;

   protected:
    /// 由 executeTask 按顺序调用，任一步失败即整体失败
    Result<> createInstance();
    Result<> extractWorlds();
    Result<> deployResources();
    Result<> writeMetadata();
    Result<> mergeOptionsTxt();

    /// 解压单个条目到目标文件；目标目录会自动创建
    Result<> extractEntry(const QString& entryPath, const QString& targetPath);

    QString instanceRoot() const;
    QString minecraftRoot() const;
    QString worldFolderFor(int index) const;
    /**
     * 世界的存盘顺序：选中的那个排第一（落到 MapWorld），其余按扫描顺序跟在后面。
     * 解压与写 map.json 必须用同一份顺序，否则重装会把世界解到别的目录去。
     */
    QList<int> worldOrder() const;

   private:
    QString m_archivePath;
    MapScanResult m_scan;
    MapImportOptions m_options;

    /// 生效的版本与加载器（可能是用户指定，也可能是判定结果）
    QString m_mcVersion;
    QString m_loader;
    QString m_loaderVersion;
    /// 判成模组环境但缺加载器版本、被迫按原版导入时的说明（写进证据链）
    QString m_loaderFallbackNote;

    QString m_primaryWorldFolder;
    /// 已解出的世界目录（失败回滚用）
    QStringList m_extractedWorldDirs;
    std::unique_ptr<MinecraftInstance> m_instance;

    std::atomic<bool> m_abortRequested{false};
    QFutureWatcher<Result<>> m_extractWatcher;

   private slots:
    void onExtractionFinished();
};

/**
 * 异步重装地图任务：在线程池中解压，支持进度显示与中途取消
 */
class MapReinstallTask : public Task {
    Q_OBJECT

   public:
    MapReinstallTask(QString instanceRoot, MapMeta meta, bool backupExisting = true);
    ~MapReinstallTask() override = default;

    void executeTask() override;
    bool abort() override;

   private slots:
    void onReinstallFinished();

   private:
    QString m_instanceRoot;
    MapMeta m_meta;
    bool m_backupExisting = true;
    std::atomic<bool> m_abortRequested{false};
    QFutureWatcher<Result<>> m_watcher;
};

}  // namespace maps
