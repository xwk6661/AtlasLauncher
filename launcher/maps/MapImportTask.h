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
#include "MapVariant.h"
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
    /// 关联的 CTM 地图 slug（空 = 不关联）。写进实例的 `hall-link.json`，
    /// 同时也记进库索引 —— 图的身份以它为准，文件名只是载体。
    QString hallSlug;
    /// 语言变体：原版 / 官中 / 汉化（含来源）。同时写进库索引。
    variant::Info variant;
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
     * @param archiveOverride 换了语言版本时指定另一个压缩包；空 = 用 map.json 记录的那个。
     */
    static Result<> reinstallWorlds(const QString& instanceRoot, const MapMeta& meta,
                                    bool backupExisting = true,
                                    std::function<bool(qint64, qint64)> progress = nullptr,
                                    const QString& archiveOverride = {});

    void executeTask() override;
    bool abort() override;

   protected:
    /// 由 executeTask 按顺序调用，任一步失败即整体失败
    Result<> createInstance();
    Result<> extractWorlds();
    Result<> deployResources();
    Result<> writeMetadata();
    /**
     * 落 options.txt：随包材质包 + 语言 + 键位。
     *
     * 三件事都只在**新建实例**时写一次；之后玩家在游戏里怎么改都不会被覆盖
     * （重装走的是 `reinstallWorlds`，不经过这里）。
     */
    Result<> applyOptionsDefaults();

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

    /**
     * 导入时要写进 options.txt 的偏好，**在构造时从全局设置读一次**。
     *
     * 构造发生在 UI 线程（三个调用点都在对话框里），而 `executeTask` 跑在线程池；
     * `SettingsObject` 不是线程安全的，所以不在任务线程里读。
     */
    bool m_optionsSetChinese = false;
    bool m_optionsSwapSprintSneak = false;
    bool m_optionsFullscreen = true;

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
    /**
     * @param archivePath 选中的库条目（另一语言版本）；空 = 沿用 map.json 记录的归档
     * @param variant     选中的语言变体，成功后写回 map.json
     */
    MapReinstallTask(QString instanceRoot, MapMeta meta, bool backupExisting = true, QString archivePath = {},
                     variant::Info variant = {});
    ~MapReinstallTask() override = default;

    void executeTask() override;
    bool abort() override;

   private slots:
    void onReinstallFinished();

   private:
    QString m_instanceRoot;
    MapMeta m_meta;
    bool m_backupExisting = true;
    /// 换版本重装时选的压缩包（空 = 用 m_meta 里的）
    QString m_archivePath;
    variant::Info m_variant;
    std::atomic<bool> m_abortRequested{false};
    QFutureWatcher<Result<>> m_watcher;
};

}  // namespace maps
