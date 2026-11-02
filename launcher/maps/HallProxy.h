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

#include <QNetworkProxy>
#include <QString>

namespace maps {

/**
 * 大厅要走的出口。
 *
 * 为什么单独解析而不直接用 Prism 的全局代理（照 AIO `util/proxy.ts` 的结论）：
 *   · 启动器主体（meta / 下载）在国内直连往往比走代理快，玩家把全局代理关掉是对的；
 *   · 但 `*.supabase.co` 恰恰相反 —— 直连 TLS 会被重置，**必须**走代理；
 *   · 浏览器的系统代理 / 环境变量常常是别的程序留下的（编辑器、终端、安装包），
 *     照着一个没人监听的地址发请求比不用代理还糟，所以 auto 模式**先探活**再用。
 *
 * 三种模式（设置项 `MapHallProxyMode`）：
 *   auto   → 环境变量 → 探活 → Windows 系统代理 → 探活 → 直连
 *   manual → 只用手填的 `MapHallProxyUrl`
 *   off    → 永远直连
 */
struct HallProxyInfo {
    /// 实际生效的代理；直连时是 QNetworkProxy::NoProxy
    QNetworkProxy proxy;
    /// 生效的代理地址（`http://127.0.0.1:7890`），直连为空
    QString url;
    /// 这个地址从哪来：env / system / manual / none
    QString from;
    /// 给人看的一句说明（空态与状态栏都用它）
    QString note;
};

/// 解析一次出口（内部有 10 秒缓存：探活要建 TCP，不能每个请求都跑）
/// @param host 目标主机；本机 / 内网 / NO_PROXY 命中时强制直连（照 AIO `shouldBypassProxy`）
HallProxyInfo resolveHallProxy(const QString& host = {});

/**
 * 这个主机是不是"不该走代理"。
 *
 * AIO 踩过的坑：把 `127.0.0.1:4499` 交给代理，代理回 502（HTTP Tunneling 失败），
 * 本地服务根本不需要出网。顺带认 `NO_PROXY` 的逗号分隔项（只做后缀匹配，够用）。
 */
bool shouldBypassProxy(const QString& host);

/// 设置改完 / 用户点「重试」时调用，让下一次解析重新探活
void invalidateHallProxyCache();

}  // namespace maps
