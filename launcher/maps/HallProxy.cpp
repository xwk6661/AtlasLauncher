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
#include "HallProxy.h"

#include <QDateTime>
#include <QDir>
#include <QList>
#include <QSettings>
#include <QTcpSocket>
#include <QUrl>

#include <iterator>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace maps {
namespace {

/// 环境变量里的代理（大小写都认，ALL_PROXY 兜底）
QString fromEnvironment()
{
    static const char* keys[] = { "HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy" };
    for (const char* key : keys) {
        const QString value = qEnvironmentVariable(key).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

/// `host:port` 补成 `http://host:port`；空串返回空
QString normalizeProxyUrl(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty())
        return {};
    if (value.contains(QStringLiteral("://")))
        return value;
    return QStringLiteral("http://%1").arg(value);
}

/// 这个代理端口真的有人监听吗（900ms 超时，探不通就当没有）
bool proxyAlive(const QString& url)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;
    const int port = parsed.port(parsed.scheme() == QLatin1String("https") ? 443 : 80);

    QTcpSocket socket;
    socket.connectToHost(parsed.host(), static_cast<quint16>(port));
    const bool ok = socket.waitForConnected(900);
    socket.abort();
    return ok;
}

/// Windows「Internet 选项 → 局域网设置」里那个代理（浏览器用的就是它）
QString fromWindowsSystemProxy()
{
#ifdef Q_OS_WIN
    QSettings ie(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
                 QSettings::NativeFormat);
    if (ie.value(QStringLiteral("ProxyEnable")).toInt() != 1)
        return {};

    const QString raw = ie.value(QStringLiteral("ProxyServer")).toString().trimmed();
    if (raw.isEmpty())
        return {};

    // 可能是 "a=b;c=d" 的分协议写法，也可能只是 "host:port"
    const auto parts = raw.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        const QString entry = part.trimmed();
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString scheme = entry.left(eq).trimmed().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
            return entry.mid(eq + 1).trimmed();
    }
    return parts.isEmpty() ? QString() : parts.first().trimmed();
#else
    return {};
#endif
}

/**
 * 扫一遍本机常见的代理端口。
 *
 * 为什么需要：Clash / v2rayN / sing-box 默认**不开**「系统代理」，也不写环境变量，
 * 而它们确实在 127.0.0.1 上开着 HTTP 入口。只读环境变量 + 注册表的话，
 * 用户明明开着代理，启动器却报「没有检测到代理」。
 *
 * 实现上并发探测：先全部 connectToHost，再逐个 waitForConnected(600)，
 * 最坏情况总耗时约 600ms（不是 10 × 600ms）。
 */
QString fromLocalPorts()
{
    static const int kPorts[] = { 7890, 7897, 7891, 10808, 10809, 1080, 2080, 2081, 8889, 4780 };

    QList<QTcpSocket*> sockets;
    sockets.reserve(std::size(kPorts));
    for (int port : kPorts) {
        auto* socket = new QTcpSocket();
        socket->connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port));
        sockets.append(socket);
    }

    QString found;
    for (int i = 0; i < sockets.size() && found.isEmpty(); ++i) {
        if (sockets.at(i)->waitForConnected(600))
            found = QStringLiteral("http://127.0.0.1:%1").arg(kPorts[i]);
    }
    qDeleteAll(sockets);
    return found;
}

QNetworkProxy makeProxy(const QString& url)
{
    const QUrl parsed(url);
    QNetworkProxy proxy(QNetworkProxy::HttpProxy, parsed.host(), static_cast<quint16>(parsed.port(8080)));
    if (!parsed.userName().isEmpty())
        proxy.setUser(parsed.userName());
    if (!parsed.password().isEmpty())
        proxy.setPassword(parsed.password());
    return proxy;
}

/// 解析结果缓存：探活要建 TCP，不能每个请求都跑（AIO 的 INFO_TTL 也是 10s）
HallProxyInfo g_cachedProxy;
qint64 g_cachedAt = 0;

HallProxyInfo direct(const QString& from, const QString& note)
{
    HallProxyInfo info;
    info.proxy = QNetworkProxy(QNetworkProxy::NoProxy);
    info.from = from;
    info.note = note;
    return info;
}

HallProxyInfo usingProxy(const QString& url, const QString& from, const QString& note)
{
    HallProxyInfo info;
    info.url = url;
    info.proxy = makeProxy(url);
    info.from = from;
    info.note = note;
    return info;
}

}  // namespace

bool shouldBypassProxy(const QString& host)
{
    const QString lower = host.trimmed().toLower();
    if (lower.isEmpty())
        return false;
    if (lower == QLatin1String("localhost") || lower == QLatin1String("::1"))
        return true;
    if (lower.startsWith(QLatin1String("127.")) || lower.startsWith(QLatin1String("10.")) ||
        lower.startsWith(QLatin1String("192.168.")))
        return true;
    // 172.16.0.0/12
    if (lower.startsWith(QLatin1String("172."))) {
        const QStringList parts = lower.split(QLatin1Char('.'));
        if (parts.size() >= 2) {
            bool ok = false;
            const int second = parts.at(1).toInt(&ok);
            if (ok && second >= 16 && second <= 31)
                return true;
        }
    }

    const QString noProxy = qEnvironmentVariable("NO_PROXY").isEmpty() ? qEnvironmentVariable("no_proxy")
                                                                      : qEnvironmentVariable("NO_PROXY");
    if (noProxy.trimmed().isEmpty())
        return false;
    for (const auto& raw : noProxy.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString entry = raw.trimmed().toLower();
        if (entry.startsWith(QLatin1Char('.')))
            entry = entry.mid(1);
        if (entry.isEmpty())
            continue;
        if (lower == entry || lower.endsWith(QLatin1String(".") + entry))
            return true;
    }
    return false;
}

void invalidateHallProxyCache()
{
    g_cachedAt = 0;
}

HallProxyInfo resolveHallProxy(const QString& host)
{
    // 本机 / 内网 / NO_PROXY：一律直连（代理接这种目标只会 502）
    if (shouldBypassProxy(host))
        return direct(QStringLiteral("none"), QStringLiteral("本机/内网地址，不走代理"));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (g_cachedAt != 0 && now - g_cachedAt < 10000)
        return g_cachedProxy;

    const auto remember = [now](const HallProxyInfo& info) {
        g_cachedProxy = info;
        g_cachedAt = now;
        return info;
    };

    auto* settings = APPLICATION->settings();
    const QString mode = settings->get("MapHallProxyMode").toString();

    if (mode == QLatin1String("off"))
        return remember(direct(QStringLiteral("none"), QStringLiteral("已关闭大厅代理（直连）")));

    if (mode == QLatin1String("manual")) {
        const QString url = normalizeProxyUrl(settings->get("MapHallProxyUrl").toString());
        if (url.isEmpty())
            return remember(direct(QStringLiteral("none"), QStringLiteral("手动模式但没填地址，按直连处理")));
        return remember(usingProxy(url, QStringLiteral("manual"), QStringLiteral("手动代理 %1").arg(url)));
    }

    // auto：环境变量优先，但必须探活（用户说"我没开代理"时，那个地址往往是别的程序留下的）
    const QString env = normalizeProxyUrl(fromEnvironment());
    if (!env.isEmpty() && proxyAlive(env))
        return remember(usingProxy(env, QStringLiteral("env"), QStringLiteral("环境变量代理 %1").arg(env)));

    const QString system = normalizeProxyUrl(fromWindowsSystemProxy());
    if (!system.isEmpty() && system != env && proxyAlive(system))
        return remember(usingProxy(system, QStringLiteral("system"), QStringLiteral("系统代理 %1").arg(system)));

    // 最后扫本机端口：Clash/v2rayN 这类默认不写系统代理，玩家常以为"我已经开了"
    const QString local = fromLocalPorts();
    if (!local.isEmpty() && local != env && local != system && proxyAlive(local))
        return remember(usingProxy(local, QStringLiteral("local-port"),
                                   QStringLiteral("自动发现的本地代理 %1").arg(local)));

    const QString stale = env.isEmpty() ? system : env;
    return remember(direct(QStringLiteral("none"),
                           stale.isEmpty() ? QStringLiteral("没有检测到代理（直连）")
                                           : QStringLiteral("检测到 %1 但连不上它，按直连处理").arg(stale)));
}

}  // namespace maps
