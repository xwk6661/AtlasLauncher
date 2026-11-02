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

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class MinecraftInstance;

namespace maps {

/**
 * 「安装 OptiFine」对话框。
 *
 * 为什么不是装成一个版本：OptiFine 官方安装器只有 GUI（`optifine.InstallerFrame`），
 * 没有命令行入口，没法自动化跑它的 patch 流程。所以照 AIO / HMCL 的做法，
 * 把 OptiFine 的 jar **当模组放进 `mods/`** —— 它的 jar 里带
 * `TweakClass: optifine.OptiFineForgeTweaker`，在 Forge 环境里能被当模组加载。
 *
 * 网络这块照 AIO 踩过的坑办（`src/loader/index.ts downloadOptifineJar`）：
 *   · 列表走 BMCLAPI（官网是 Cloudflare + 广告页，不适合程序拉）；
 *   · 下载**两个源依次试**：BMCLAPI 的下载节点经常连不上（302 到一个连不通的
 *     下载域名），官网 `optifine.net` 反而通常能通，但它要先从 `adloadx` 页面里
 *     抠出 `downloadx?...` 链接，并且必须带 `Referer`，否则 403；
 *   · 每个源各重试两次，全失败才报错，并把两边的错误原文都列出来。
 */
class OptiFineDialog : public QDialog {
    Q_OBJECT

   public:
    explicit OptiFineDialog(MinecraftInstance* instance, QWidget* parent = nullptr);

   private:
    struct Build {
        QString type;
        QString patch;
        QString filename;
    };

    /// 读实例的 MC 版本与加载器，决定装到哪、要不要警告
    void resolveInstance();
    /// 拉 OptiFine 版本列表（BMCLAPI；失败重试一次）
    void fetchList(int attempt = 1);
    void onListReply(QNetworkReply* reply, int attempt);
    /// 开始下载并安装
    void startInstall();
    /// 第 source 个源（0 = BMCLAPI，1 = 官网），attempt 从 1 开始
    void trySource(int source, int attempt);
    /// 官网第二步：adloadx 页面里找 downloadx 链接
    void onAdPage(QNetworkReply* reply, int attempt);
    void onDownloadReply(QNetworkReply* reply, int source, int attempt);

    void finishOk(const QString& file, const QString& via);
    void fail(const QString& message);
    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);
    QString modsDir() const;

    MinecraftInstance* m_instance = nullptr;
    QString m_mcVersion;
    QString m_loaderName;
    QNetworkAccessManager* m_nam = nullptr;

    QList<Build> m_builds;
    Build m_pending;      // 正在下载的那一个
    QByteArray m_payload;  // 下载缓冲（jar 只有几 MB，直接进内存比临时文件好收尾）

    QComboBox* m_buildCombo = nullptr;
    QLabel* m_loaderHint = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_closeButton = nullptr;
};

}  // namespace maps
