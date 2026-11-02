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
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

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
    const QString profileDir = QDir::tempPath() + QStringLiteral("/prism-hall-browser-%1-%2")
                                                        .arg(QCoreApplication::applicationPid())
                                                        .arg(++counter);
    QDir().mkpath(profileDir);

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
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-extensions"),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--mute-audio"),
        QStringLiteral("--user-data-dir=%1").arg(profileDir),
        // 给它一点时间去跑完 DNS/ECH 握手，但不能无限等
        QStringLiteral("--virtual-time-budget=6000"),
        QStringLiteral("--dump-dom"),
        m_url.toString(),
    });

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        timer->deleteLater();
        if (m_proc != nullptr && m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();  // 触发 finished，交给下面统一收尾
        }
    });
    timer->start(30000);

    connect(m_proc, &QProcess::finished, this, [this, remaining, timer, profileDir](int code, QProcess::ExitStatus status) {
        timer->stop();
        timer->deleteLater();
        if (m_proc == nullptr)
            return;

        const QByteArray out = m_proc->readAllStandardOutput();
        const QString err = QString::fromLocal8Bit(m_proc->readAllStandardError());
        cleanup();
        QDir(profileDir).removeRecursively();

        // 只要 DOM 里有 <pre> 就认：退出码不代表数据没拿到（页面加载完浏览器仍可能非 0 退出）
        const QByteArray body = extractPre(out);
        if (!body.isEmpty()) {
            emit finished(true, body, {});
            return;
        }

        // 收尾：还有次数就再试（实测这条链路会偶发 CONNECTION_RESET）
        QString reason;
        if (err.contains(QLatin1String("ERR_CONNECTION_RESET")))
            reason = tr("连接被重置（常见的间歇性阻断）");
        else if (err.contains(QLatin1String("ERR_QUIC_PROTOCOL_ERROR")))
            reason = tr("QUIC 握手失败（已关 QUIC，通常重试即可）");
        else if (err.contains(QLatin1String("ERR_INTERNET_DISCONNECTED")))
            reason = tr("系统提示没有网络连接");
        else if (status == QProcess::CrashExit)
            reason = tr("浏览器进程崩溃（退出码 %1）").arg(code);
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
