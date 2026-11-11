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

#include <QFutureWatcher>
#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

#include "tasks/Task.h"

namespace maps::l10n {

struct ApplyStats {
    /// 处理过的 .mca / .dat 文件数
    int files = 0;
    /// 读到的区块数
    int chunks = 0;
    /// 真的被改过、并写回去的区块数
    int patchedChunks = 0;
    /// 替换的文本处数
    int replaced = 0;
    /**
     * 结构比对的差异数。
     *
     * 每次都做：把两侧的文本字段清空后逐区块比较二进制。**必须为 0**；
     * 不为 0 的区块会被**丢弃**（保留原样），宁可少翻一块也不能写坏。
     */
    int structureDiffs = 0;
    /// 因为结构比对没过而被丢弃的区块数
    int discarded = 0;
    QString error;

    bool ok() const { return error.isEmpty() && structureDiffs == 0; }
};

/// 从 `strings.json` 读 "原文 → 译文"，只取 `zh` 非空的条目
QHash<QString, QString> loadTranslations(const QString& jsonPath, int* withTranslation = nullptr,
                                         QString* error = nullptr);

/**
 * 把世界**复制**到 `outputRoot`，在副本上替换文本。
 *
 * 源目录只读不写（这是整个 C 档的安全底线）。
 *
 * 两个关键纪律：
 *   1. 没被改动的区块**按原始压缩字节原样搬运** —— 解压再压缩一次就会改字节，
 *      而区块文件里任何一处差异都可能让游戏重算或报错；
 *   2. 改过的区块先做结构比对（文本字段清空后比二进制），不一致就丢弃、保留原文。
 *
 * @param progress 每处理完一个文件回调（已完成, 总数）；返回 false 取消
 */
ApplyStats applyToCopy(const QString& sourceRoot, const QString& outputRoot,
                       const QHash<QString, QString>& translations,
                       const std::function<bool(int done, int total)>& progress = {});

/**
 * 把 `applyToCopy` 包成带进度与取消的任务。
 *
 * 和体检任务同一个道理：`Task::start()` 是同步执行 `executeTask()` 的，
 * 复制几百 MB 存档 + 逐区块改写绝不能写在里面，必须扔进线程池。
 */
class MapL10nApplyTask : public Task {
    Q_OBJECT

   public:
    MapL10nApplyTask(QString sourceRoot, QString outputRoot, QHash<QString, QString> translations);
    ~MapL10nApplyTask() override = default;

    void executeTask() override;
    bool abort() override;

    const ApplyStats& stats() const { return m_stats; }

   private slots:
    void onApplyFinished();

   private:
    QString m_sourceRoot;
    QString m_outputRoot;
    QHash<QString, QString> m_translations;
    std::atomic<bool> m_aborted{ false };
    ApplyStats m_stats;
    QFutureWatcher<void> m_watcher;
};

// ────────────────────────── 备份 + 替换 ──────────────────────────

struct ReplaceStats {
    /// 旧存档打成的 zip 完整路径
    QString backupPath;
    /// zip 大小（字节）
    qint64 backupBytes = 0;
    /// 打进 zip 的文件数
    int files = 0;
    /// 被替换的世界目录名（如 `MapWorld`）
    QString worldFolder;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

/**
 * 旧存档**打包成 zip 备份**，然后用 `newWorldRoot` 顶替 `worldRoot`。
 *
 * 顺序是这样的，每一步失败都能退回去：
 *   1. 校验汉化版目录完整（必须有 `level.dat`，否则游戏会把它当新世界重建）；
 *   2. 旧存档逐文件写进 zip（用 `store` 不压缩 —— `.mca` 内部已经压过，再 deflate 只是白烧 CPU）；
 *   3. zip 落盘且非空，才动目录：旧目录先改名挪开，汉化版改名就位；
 *      **汉化版就位失败就把旧目录改回来**，旧存档不会丢；
 *   4. 成功后才删掉那个临时挪开的旧目录（zip 里有完整备份）。
 *
 * @param progress 打包阶段每写一个文件回调（已完成, 总数）；返回 false 取消（此时旧存档原样）
 */
ReplaceStats replaceWorldWithBackup(const QString& worldRoot, const QString& newWorldRoot,
                                    const QString& backupDir,
                                    const std::function<bool(int done, int total)>& progress = {});

/// 把 `replaceWorldWithBackup` 包成带进度与取消的任务（同样只能派活，不能干活）
class MapL10nReplaceTask : public Task {
    Q_OBJECT

   public:
    MapL10nReplaceTask(QString worldRoot, QString newWorldRoot, QString backupDir);
    ~MapL10nReplaceTask() override = default;

    void executeTask() override;
    bool abort() override;

    const ReplaceStats& stats() const { return m_stats; }

   private slots:
    void onReplaceFinished();

   private:
    QString m_worldRoot;
    QString m_newWorldRoot;
    QString m_backupDir;
    std::atomic<bool> m_aborted{ false };
    ReplaceStats m_stats;
    QFutureWatcher<void> m_watcher;
};

}  // namespace maps::l10n
