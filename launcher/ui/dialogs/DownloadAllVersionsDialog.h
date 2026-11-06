// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include "net/NetJob.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

/**
 * 调试用：把**所有正式版**的公共资源下载到当前数据目录。
 *
 * 目的不是"给用户用"，而是拿真实流量去压下载栈：
 *   · 客户端 jar → `libraries/com/mojang/minecraft/<ver>/`（每版一份，无法共享）
 *   · 库文件     → `libraries/`（走 metacache，天然去重）
 *   · 素材索引   → `assets/indexes/<id>.json`
 *   · 素材对象   → `assets/objects/<xx>/<hash>`（按 hash 共享，跨版本去重）
 *
 * 复用启动器自己的路径（`Library::getDownloads`、`AssetsIndex::getDownloadJob`），
 * 所以它测的就是玩家真正会走的那条路，包括重试、连接池、分片。
 *
 * **临时功能**，用 `#ifndef NDEBUG` 包起来，随调试期结束删除。
 */
class DownloadAllVersionsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit DownloadAllVersionsDialog(QWidget* parent = nullptr);

   public slots:
    void start();

   private:
    struct VersionEntry {
        QString id;
        QUrl url;
        QString sha1;
    };

    using Callback = std::function<void()>;

    void appendLog(const QString& line);
    void stepManifest();
    void stepVersionJson();
    void stepVersionPayload();
    void stepAssets();
    void advance();
    void finishRun(bool cancelled);
    void runJob(NetJob::Ptr job, Callback onSucceeded, std::function<void(const QString&)> onFailed);
    void clearJob();
    auto versionJsonPath(const QString& id) const -> QString;
    /// 当前版本已经花了多少秒（日志里带上，方便算吞吐）
    auto currentVersionSeconds() const -> double;
    auto currentVersion() const -> const VersionEntry& { return m_versions.at(m_index); }

    QLabel* m_headline = nullptr;
    QLabel* m_currentLabel = nullptr;
    QProgressBar* m_overall = nullptr;
    QProgressBar* m_step = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_closeButton = nullptr;

    QList<VersionEntry> m_versions;
    QSet<QString> m_seenIndexes;
    int m_index = 0;
    int m_ok = 0;
    int m_bad = 0;
    int m_requests = 0;
    qint64 m_bytes = 0;
    qint64 m_jobBytes = 0;
    qint64 m_runStartMs = 0;
    qint64 m_versionStartMs = 0;
    bool m_running = false;
    bool m_cancelled = false;

    NetJob::Ptr m_job;
    QStringList m_report;
};
