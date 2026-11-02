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
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace maps {

/**
 * 大厅封面加载器（列表缩略图 + 详情小图共用）。
 *
 * 为什么不能只靠 `QNetworkAccessManager`：
 * `*.supabase.co`（874/1000 张封面）在部分网络里被**按 SNI 阻断**——DNS 能解析到
 * Cloudflare，TCP 能连上，TLS 握手却收不到 ServerHello。本机 Schannel（Qt 在 Windows
 * 上的后端）不支持 ECH，所以 Qt 栈**无论如何都过不去**；而 Chromium 支持 ECH，
 * 同一个 URL 在浏览器里能正常出图（2026-11-02 实测 1920×1080）。
 *
 * 于是策略是「先直连、失败换浏览器」：
 *   1. 直连（Qt）：有代理/没被阻断的网络里这是最快的路，命中后照常落盘；
 *   2. 浏览器通道：起一个**一次性**的无头 Chromium（Edge/Chrome），页面里用
 *      `<img>` 取图 → canvas 缩到 480px → `toDataURL('image/jpeg')` → 写进 DOM，
 *      宿主从 `--dump-dom` 的 `<pre>` 里把 base64 收回来。
 *      一次进程可以顺带取 8 张（页面内并发 6），实测 8 张 4.4 秒。
 *   3. 磁盘缓存：`<数据目录>/maps/covers/<sha1(url)>.jpg`，命中就不再出网。
 *
 * 注意 `--disable-web-security`：图片是跨域资源，不关掉 Web 安全的话 canvas 会被
 * 标记为 tainted，`toDataURL` 直接抛异常。这个进程用的是**临时空白 profile**、
 * 只加载我们自己生成的一页 HTML，跑完即杀，不碰用户任何数据。
 */
class MapCoverLoader : public QObject {
    Q_OBJECT

   public:
    explicit MapCoverLoader(QObject* parent = nullptr);
    ~MapCoverLoader() override;

    static MapCoverLoader* instance();

    /// 缓存目录（默认 `<dataRoot>/maps/covers`；测试里指到临时目录）
    void setCacheDir(const QString& dir);
    QString cacheDir() const;

    /// 直连用的网络管理器（默认自建一个；大厅会把它换成按代理设置解析过的那个）
    void setNetwork(QNetworkAccessManager* nam);

    /// 跟随设置项 MapHallTransport：auto / network / browser
    void setTransportMode(const QString& mode);

    /// 内存或磁盘里现成的图（没有就返回空）
    QImage cached(const QString& url);

    /// 确认过拿不到的 URL，别再排队（跨页面共享，避免每次开大厅都重试一遍）
    bool isKnownBad(const QString& url) const { return m_bad.contains(url); }

    /// 排队加载；结果通过 coverReady / coverFailed 回来
    void request(const QString& url);

    /// 待办数量（状态栏/测试用）
    int pendingCount() const { return m_queue.size() + (m_busy ? 1 : 0); }

   signals:
    void coverReady(const QString& url, const QImage& image);
    void coverFailed(const QString& url, const QString& reason);
    /// 一批跑完（ok/failed 是本批计数），界面可以据此收掉「正在取封面」的提示
    void batchFinished(int ok, int failed);

   private:
    enum class HostMode { Unknown, Direct, Browser };

    void schedule();
    void dispatch();
    void tryDirect();
    void startBrowser();
    void finishBatch();
    void acceptImage(const QString& url, const QByteArray& bytes, bool alreadyThumbnail);
    void failUrl(const QString& url, const QString& reason);
    void rememberImage(const QString& url, const QImage& image);
    QImage loadFromDisk(const QString& url) const;
    void saveToDisk(const QString& url, const QByteArray& jpeg);
    QString cachePath(const QString& url) const;
    HostMode hostMode(const QString& host) const;
    void setHostMode(const QString& host, HostMode mode);
    QStringList takeBatch();
    QString generatePage(const QStringList& urls) const;

    QPointer<QNetworkAccessManager> m_nam;
    bool m_ownNam = false;
    QString m_cacheDir;
    QString m_transport = QStringLiteral("auto");

    QList<QString> m_queue;
    QSet<QString> m_queued;
    QSet<QString> m_bad;
    QHash<QString, int> m_attempts;
    QHash<QString, HostMode> m_hostModes;

    // 内存缩略图（LRU，避免 1000 张 × 0.5 MB 常驻）
    QHash<QString, QImage> m_memory;
    QList<QString> m_lru;
    int m_memoryCap = 96;

    QTimer* m_timer = nullptr;
    bool m_busy = false;
    QList<QString> m_batch;
    QStringList m_directQueue;
    QStringList m_browserQueue;
    int m_pendingReplies = 0;
    int m_batchOk = 0;
    int m_batchFailed = 0;

    QProcess* m_browser = nullptr;
    QString m_browserProfile;
};

}  // namespace maps
