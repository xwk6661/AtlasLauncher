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
#include "BrowserFetch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <atomic>
#include <QTimer>

namespace maps {
namespace {

/// 常见的 Chromium 系浏览器安装位置（Windows 优先；其它平台只在 PATH 里找）
QStringList candidateBrowsers()
{
    QStringList paths;
#ifdef Q_OS_WIN
    const QStringList roots = { qEnvironmentVariable("ProgramFiles(x86)"), qEnvironmentVariable("ProgramFiles"),
                                qEnvironmentVariable("LOCALAPPDATA") };
    for (const auto& root : roots) {
        if (root.isEmpty())
            continue;
        paths << root + QStringLiteral("/Microsoft/Edge/Application/msedge.exe");
        paths << root + QStringLiteral("/Google/Chrome/Application/chrome.exe");
    }
    // Edge WebView2 运行时也带一个 Chromium，但没有 --dump-dom 的 headless 入口，跳过
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge");
    paths << QStringLiteral("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
#else
    for (const auto& name : { "microsoft-edge", "google-chrome", "chromium", "chromium-browser" })
        paths << QStandardPaths::findExecutable(QString::fromLatin1(name));
#endif
    return paths;
}

/// `&amp;` 这类实体在 dump 出来的 DOM 里是转义过的
QByteArray unescapeHtml(QByteArray text)
{
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    text.replace("&#39;", "'");
    text.replace("&amp;", "&");
    return text;
}

/// 从 dump 出来的 HTML 里取 `<pre>` 的内容（Chromium 把纯文本/JSON 响应渲染在这里）
QByteArray extractPre(const QByteArray& html)
{
    const int start = html.indexOf("<pre");
    if (start < 0)
        return {};
    const int contentStart = html.indexOf('>', start);
    const int end = html.indexOf("</pre>", contentStart);
    if (contentStart < 0 || end < 0)
        return {};
    return unescapeHtml(html.mid(contentStart + 1, end - contentStart - 1));
}

}  // namespace

QString BrowserFetch::browserPath()
{
    for (const auto& path : candidateBrowsers()) {
        if (!path.isEmpty() && QFileInfo::exists(path))
            return path;
    }
    return {};
}

bool BrowserFetch::available()
{
    return !browserPath().isEmpty();
}

BrowserFetch::BrowserFetch(QObject* parent) : QObject(parent) {}

BrowserFetch::~BrowserFetch()
{
    cleanup();

}

void BrowserFetch::cleanup()
{
    if (m_proc != nullptr) {
        m_proc->disconnect(this);
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(3000);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void BrowserFetch::abort()
{
    cleanup();
}

void BrowserFetch::get(const QUrl& url, int attempts)
{
    cleanup();
    m_url = url;
    m_attempts = std::max(1, attempts);
    attemptOnce(m_attempts);
}

void BrowserFetch::attemptOnce(int remaining)
{
    const QString browser = browserPath();
    if (browser.isEmpty()) {
        emit finished(false, {}, tr("本机没有找到 Edge / Chrome，无法使用浏览器通道"));
        return;
    }

    /**
     * **每次尝试都用全新的 profile 目录**。
     *
     * 踩过的坑（2026-11-01）：一开始整个对象共用一个 profile 目录，第二次请求时
     * 上一次的浏览器进程还没退干净，新进程判定"这个 profile 已被占用"，直接把工作
     * 交接给老进程然后**自己退出码 21、stdout 为空** —— 界面上就是
     * 「浏览器没有返回内容（退出码 21）」。改成一次一个目录就再没出现过。
     */
    static std::atomic<int> counter{ 0 };
    const QString profileDir = QDir::tempPath() + QStringLiteral("/atlas-hall-browser-%1-%2")
                                                        .arg(QCoreApplication::applicationPid())
                                                        .arg(++counter);
    QDir().mkpath(profileDir);

    /**
     * 页面里用 XHR 取，而不是让浏览器直接打开 API 地址。
     *
     * 原因（2026-11-02 实测）：直接 `--dump-dom <API URL>` 是"一锤子买卖"——
     * 这条链路约 1/3 概率 `ERR_CONNECTION_RESET`，一旦撞上就得整体重开浏览器进程，
     * 用户看到的就是「更新数据」失败；更糟的是重试过程中进程被杀，会被报成
     * 「浏览器进程崩溃（退出码 62097）」这种看不懂的话。
     *
     * 改成页面内 XHR 之后：一次进程内可以退避重试、能读到真实 HTTP 状态码、
     * 4xx 不必重试（鉴权/参数问题重试也没用），失败原因也能说人话。
     *
     * 注意**不能**加 `?_r=N` 之类的防缓存参数：PostgREST 对未知查询参数直接
     * 返回 `PGRST100 / unexpected param`（400），重试反而全灭。
     */
    // URL 以 JSON 字符串字面量塞进页面（顺带处理引号/反斜杠）；"</" 转义防止截断脚本
    auto urlLiteral = QString::fromUtf8(QJsonDocument(QJsonArray{ m_url.toString() }).toJson(QJsonDocument::Compact));
    urlLiteral = urlLiteral.mid(1, urlLiteral.size() - 2);
    urlLiteral.replace(QLatin1String("</"), QLatin1String("<\\/"));

    QString page = QStringLiteral(R"(<!doctype html>
<html><head><meta charset="utf-8"><title>Atlas fetch</title></head>
<body><pre id="bf">pending</pre>
<script>
const URL = %1;
const MAX = 3;
let tries = 0;
function finish(o) { document.getElementById('bf').textContent = JSON.stringify(o); }
function go() {
  tries++;
  const x = new XMLHttpRequest();
  x.open('GET', URL);
  x.timeout = 20000;
  x.onload = function () {
    const body = x.responseText || '';
    const enc = btoa(unescape(encodeURIComponent(body)));
    if (x.status >= 200 && x.status < 300) return finish({ ok: true, status: x.status, tries: tries, body: enc });
    // 4xx 是"路通了但请求不对"（鉴权/参数），重试没有意义
    if (x.status >= 400 && x.status < 500) return finish({ ok: false, status: x.status, tries: tries, body: enc });
    retry();
  };
  x.onerror = x.ontimeout = retry;
  function retry() {
    if (tries < MAX) { setTimeout(go, 300 * tries); return; }
    finish({ ok: false, status: 0, tries: tries, body: '' });
  }
  x.send();
}
go();
</script></body></html>)")
                       .arg(urlLiteral);

    const QString pagePath = QDir(profileDir).filePath(QStringLiteral("fetch.html"));
    QFile pageFile(pagePath);
    if (!pageFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(profileDir).removeRecursively();
        emit finished(false, {}, tr("无法写入浏览器通道的临时页面"));
        return;
    }
    pageFile.write(page.toUtf8());
    pageFile.close();

    m_proc = new QProcess(this);
    m_proc->setProgram(browser);
    m_proc->setArguments({
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        // 关掉 QUIC：这台网络到目标主机的 QUIC 会报 ERR_QUIC_PROTOCOL_ERROR，
        // 走 TCP + ECH 才是能通的那条；关掉省一次失败往返
        QStringLiteral("--disable-quic"),
        // 明确不走任何代理：这条通道的意义就是"绕开需要代理的那条路"
        QStringLiteral("--no-proxy-server"),
        // 图床/接口不一定都带 Access-Control-Allow-Origin；一次性空白 profile，关掉无副作用
        QStringLiteral("--disable-web-security"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-extensions"),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--mute-audio"),
        QStringLiteral("--user-data-dir=%1").arg(profileDir),
        // 留给 XHR 三次尝试（含退避）和 2.5 MB 级响应体
        QStringLiteral("--virtual-time-budget=25000"),
        QStringLiteral("--dump-dom"),
        QUrl::fromLocalFile(pagePath).toString(),
    });

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        timer->deleteLater();
        if (m_proc != nullptr && m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();  // 触发 finished，交给下面统一收尾
        }
    });
    timer->start(60000);

    connect(m_proc, &QProcess::finished, this, [this, remaining, timer, profileDir](int code, QProcess::ExitStatus status) {
        timer->stop();
        timer->deleteLater();
        if (m_proc == nullptr)
            return;

        const QByteArray out = m_proc->readAllStandardOutput();
        const QString err = QString::fromLocal8Bit(m_proc->readAllStandardError());
        cleanup();
        QDir(profileDir).removeRecursively();

        const auto payload = extractPre(out);
        const auto document = QJsonDocument::fromJson(payload);
        if (document.isObject()) {
            const auto object = document.object();
            const int httpStatus = object.value(QStringLiteral("status")).toInt();
            const auto body = QByteArray::fromBase64(object.value(QStringLiteral("body")).toString().toLatin1());

            if (object.value(QStringLiteral("ok")).toBool()) {
                emit finished(true, body, {});
                return;
            }

            if (httpStatus >= 400 && httpStatus < 500) {
                // 鉴权/参数问题：把服务端的话原样带出去，别让用户去猜
                QString detail = QString::fromUtf8(body);
                if (const auto parsed = QJsonDocument::fromJson(body); parsed.isObject())
                    detail = parsed.object().value(QStringLiteral("message")).toString(detail);
                emit finished(false, {},
                              tr("HTTP %1：%2").arg(httpStatus).arg(detail.isEmpty() ? tr("请求被拒绝") : detail));
                return;
            }

            // 5xx / 连不上：还是整段重开一次（页面内已经试过 %1 次）
            const QString reason = httpStatus == 0
                                       ? tr("连接被重置或超时（页面内已重试 %1 次）")
                                             .arg(object.value(QStringLiteral("tries")).toInt())
                                       : tr("服务端返回 HTTP %1").arg(httpStatus);
            if (remaining > 1) {
                QTimer::singleShot(400, this, [this, remaining] { attemptOnce(remaining - 1); });
                return;
            }
            emit finished(false, {}, reason);
            return;
        }

        // 连页面都没跑起来：退回进程层面的诊断
        QString reason;
        if (err.contains(QLatin1String("ERR_CONNECTION_RESET")))
            reason = tr("连接被重置（常见的间歇性阻断）");
        else if (err.contains(QLatin1String("ERR_QUIC_PROTOCOL_ERROR")))
            reason = tr("QUIC 握手失败（已关 QUIC，通常重试即可）");
        else if (err.contains(QLatin1String("ERR_INTERNET_DISCONNECTED")))
            reason = tr("系统提示没有网络连接");
        else if (status == QProcess::CrashExit)
            reason = tr("浏览器进程被系统终止（退出码 %1）").arg(code);
        else
            reason = tr("浏览器没有返回内容（退出码 %1）").arg(code);

        // 把浏览器的 stderr 尾巴带上：退出码 21 这种没有上下文的数字，光看没用
        QString tail = err;
        const int keep = 240;
        if (tail.size() > keep)
            tail = QStringLiteral("…") + tail.right(keep);
        tail = tail.simplified();
        if (!tail.isEmpty())
            reason += QStringLiteral("｜%1").arg(tail);

        if (remaining > 1) {
            QTimer::singleShot(400, this, [this, remaining] { attemptOnce(remaining - 1); });
            return;
        }
        emit finished(false, {}, reason);
    });

    connect(m_proc, &QProcess::errorOccurred, this, [this, timer](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;  // 其它错误由 finished 统一处理
        timer->stop();
        timer->deleteLater();
        cleanup();
        emit finished(false, {}, tr("浏览器启动失败：%1").arg(m_url.toString()));
    });

    m_proc->start();
}

}  // namespace maps
