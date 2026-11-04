// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas Launcher - Minecraft launcher fork with a map-first workflow
 *  Copyright (C) 2026 Atlas contributors
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

#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

namespace Net {

/**
 * 下载源：Mojang / Fabric / Forge 这些**公共文件**域的可选镜像（BMCLAPI）。
 *
 * 只做一件事 —— 把官方域名的 URL 换成镜像前缀，规则逐条照抄
 * <https://bmclapidoc.bangbang93.com/> 的映射表。**不碰** Modrinth / CurseForge /
 * GitHub / 地图站的业务 API：那些是数据接口，不是"文件下载"，镜像里也没有。
 *
 * 三条设计约束：
 * 1. **默认关**。`DownloadSource` 设置默认 `official`，值不为 `bmclapi` 时 rewrite()
 *    原样返回，官方源的路径与行为一个字都不变。
 * 2. **映射表之外不猜**。没列出来的域（例如 `piston-data` 以外的对象存储、模组站）
 *    一律原样返回，宁可慢也不要想当然地改。
 * 3. **镜像走直连**。启动器自己的代理设置对镜像节点往往是帮倒忙（实测：代理会掐掉
 *    BMCLAPI 的 302 跳转），所以镜像请求用一条不设代理的 QNetworkAccessManager。
 */
class DownloadSource {
   public:
    /// 设置项 `DownloadSource` 的取值
    static constexpr const char* OFFICIAL = "official";
    static constexpr const char* BMCLAPI = "bmclapi";

    /// 镜像根（不带结尾斜杠）
    static constexpr const char* MIRROR_BASE = "https://bmclapi2.bangbang93.com";

    /// 当前是否启用了镜像
    static bool mirrorEnabled();

    /// 官方 → 镜像。设置没开启镜像、或不在映射表里、或本来就是镜像，一律原样返回。
    static QUrl rewrite(const QUrl& url);

    /**
     * 只做映射、不看设置 —— 供单测直接验证映射表。
     *
     * 单独拆出来的原因：rewrite() 依赖运行时设置，单测里没有 Application 就拿不到；
     * 而"域名和路径有没有映射对"恰恰是这层最容易写错的地方，必须能离线钉住。
     */
    static QUrl mapToMirror(const QUrl& url);

    /// 这个地址是否落在镜像域上（决定要不要绕开代理）
    static bool isMirror(const QUrl& url);

    /// 镜像专用、不走任何代理的网络管理器（单例，所有权归 QCoreApplication）
    static QNetworkAccessManager* directNetwork();
};

}  // namespace Net
