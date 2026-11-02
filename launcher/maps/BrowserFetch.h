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

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QProcess;
class QTimer;

namespace maps {

/**
 * 「浏览器通道」：借本机的 Edge / Chrome（无头模式）去取一个 URL。
 *
 * ## 为什么要这条通道
 *
 * 2026-11-01 实测（都是本机直连，`curl --noproxy`）：
 *   · `https://<ref>.supabase.co/...` → `Connection was reset`；
 *   · 同一个 IP、只把 SNI 换成 `api.modrinth.com` → **200**；
 *   · 当前解析出的两个 Cloudflare IP 换着连、换端口、换 TLS 版本、明文 :80 → 全 reset；
 *   · 浏览器（Edge）打开同样的地址 → **能拿到数据**。
 *
 * 也就是说：**被封的是 SNI 这一个字符串**，不是 IP、不是端口、也不是证书问题。
 * 浏览器之所以能过，是因为 Chromium 实现了 ECH（加密 ClientHello）/GREASE，
 * 握手内容在链路上看不出目标域名；而 Qt 的 `QNetworkAccessManager`（Schannel/OpenSSL）
 * 只能老老实实把 SNI 写在明文里，发出去就被 RST。
 *
 * 所以这条通道不是"再写一个 HTTP 客户端"，而是**换一个客户端**：让系统里已经装好的
 * Edge/Chrome 去打这个请求，我们只取它渲染出来的文本。这是 Qt 桌面端在不引入代理、
 * 不自己实现 ECH 的前提下，唯一能走通的路。
 *
 * ## 代价（必须让调用方知道）
 *
 *   · 每次请求要起一个无头浏览器进程，约 1.5–3 秒；
 *   · 只能发**简单 GET**：自定义头（`apikey` / `Prefer: count=exact`）不能带，
 *     apikey 改走查询参数（Kong 认），所以拿不到 `Content-Range` → 评论总数未知；
 *   · 链路本身仍会偶发 `ERR_CONNECTION_RESET`（实测约 1/3 概率），所以**重试放在页面里**
 *     （XHR + 退避），比"每失败一次就重开一个浏览器进程"稳得多，也快得多。
 */
class BrowserFetch : public QObject {
    Q_OBJECT

   public:
    explicit BrowserFetch(QObject* parent = nullptr);
    ~BrowserFetch() override;

    /// 本机有没有可用的浏览器（Edge 优先，其次 Chrome）
    static QString browserPath();
    static bool available();

    /// 取一个 URL（内部最多 attempts 次，失败之间短暂间隔）
    void get(const QUrl& url, int attempts = 3);

    /// 正在跑
    bool isRunning() const { return m_proc != nullptr; }

    /// 立刻取消（不再回调 finished）
    void abort();

   signals:
    /// ok=false 时 body 为空、error 是可读原因
    void finished(bool ok, const QByteArray& body, const QString& error);

   private:
    void attemptOnce(int remaining);
    void cleanup();

    QProcess* m_proc = nullptr;
    QUrl m_url;
    int m_attempts = 3;
};

}  // namespace maps
