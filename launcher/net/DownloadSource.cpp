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

#include "DownloadSource.h"

#include <QCoreApplication>
#include <QNetworkProxy>

#include "Application.h"
// Application.h 只前置声明了 SettingsObject，这里要调 settings()->get()
#include "settings/SettingsObject.h"

namespace {

struct Rule {
    const char* host;
    /// 官方路径前缀（带前导斜杠，空串表示整站都映射）
    const char* pathPrefix;
    /// 镜像上的路径前缀。`pathPrefix` 为空时 rest 带前导斜杠，否则不带 —— 见下面注释
    const char* mirrorPrefix;
};

/// 取自 <https://bmclapidoc.bangbang93.com/> 的「使用」一节，顺序即匹配优先级。
///
/// `pathPrefix` 与 `mirrorPrefix` 的斜杠约定（照抄比"统一处理"更不容易出错）：
/// - 前缀为空：rest 保留原 path 的前导斜杠 → mirrorPrefix 不带结尾斜杠
///   （`libraries.minecraft.net/net/...` + `/maven` → `/maven/net/...`）
/// - 前缀非空：rest 是 cut 掉前缀后的剩余部分 → mirrorPrefix 带结尾斜杠
///   （`files.minecraftforge.net/maven/net/...` + `/maven/` → `/maven/net/...`）
constexpr Rule RULES[] = {
    // 资源对象：resources.download.minecraft.net/<xx>/<hash>
    { "resources.download.minecraft.net", "", "/assets" },
    // Mojang 库
    { "libraries.minecraft.net", "", "/maven" },
    // Forge / NeoForge / Fabric / Quilt 的 maven 都在镜像的 /maven 下
    { "files.minecraftforge.net", "/maven/", "/maven/" },
    { "maven.fabricmc.net", "", "/maven" },
    { "maven.neoforged.net", "/releases/", "/maven/" },
    { "maven.quiltmc.org", "/repository/release/", "/maven/" },
    // 加载器的 meta（版本清单），不是文件下载，但同样是官方域且被镜像
    { "meta.fabricmc.net", "", "/fabric-meta" },
    { "meta.quiltmc.org", "", "/quilt-meta" },
    // 版本清单 / 版本 json / 运行时清单：路径原样拼到镜像根
    { "launchermeta.mojang.com", "", "" },
    { "launcher.mojang.com", "", "" },
    { "piston-meta.mojang.com", "", "" },
    { "piston-data.mojang.com", "", "" },
};

}  // namespace

namespace Net {

bool DownloadSource::mirrorEnabled()
{
#ifdef LAUNCHER_APPLICATION
    // dynamic_cast 而不是 APPLICATION：没有 Application 的上下文（单测）里
    // APPLICATION（static_cast）是个假指针，取 settings() 会直接崩。
    auto* app = APPLICATION_DYN;
    if (app == nullptr || app->settings() == nullptr)
        return false;
    return app->settings()->get("DownloadSource").toString() == QLatin1String(BMCLAPI);
#else
    return false;
#endif
}

QUrl DownloadSource::rewrite(const QUrl& url)
{
    return rewriteFor(url, mirrorEnabled());
}

QUrl DownloadSource::rewriteFor(const QUrl& url, bool mirror)
{
    return mirror ? mapToMirror(url) : url;
}

QUrl DownloadSource::mapToMirror(const QUrl& url)
{
    if (url.isEmpty() || isMirror(url))
        return url;

    const QString host = url.host().toLower();
    const QString path = url.path();

    for (const auto& rule : RULES) {
        if (host != QLatin1String(rule.host))
            continue;

        const QString prefix = QString::fromLatin1(rule.pathPrefix);
        if (!prefix.isEmpty() && !path.startsWith(prefix, Qt::CaseSensitive)) {
            // 同一个域下还有不归镜像管的路径（例如 Forge 的 promotions），保持官方地址
            return url;
        }

        const QString rest = prefix.isEmpty() ? path : path.mid(prefix.size());
        QUrl mirrored = url;
        mirrored.setScheme(QStringLiteral("https"));
        mirrored.setHost(QString::fromLatin1("bmclapi2.bangbang93.com"));
        mirrored.setPath(QString::fromLatin1(rule.mirrorPrefix) + rest);
        return mirrored;
    }

    return url;
}

bool DownloadSource::isMirror(const QUrl& url)
{
    const QString host = url.host().toLower();
    // 302 之后会落到各 OpenBMCLAPI 节点，域名五花八门；这里只认入口域。
    // 跳转由 QNetworkReply 自己跟随，走的是同一条（无代理的）连接。
    return host == QLatin1String("bmclapi2.bangbang93.com") || host == QLatin1String("bmclapi.bangbang93.com") ||
           host.endsWith(QLatin1String(".bangbang93.com"));
}

QNetworkAccessManager* DownloadSource::directNetwork()
{
    // 懒建 + 挂在 QCoreApplication 上：退出时随应用一起销毁。
    // 明确设 NoProxy —— 镜像节点的 302 跳转最怕中间那层代理。
    static QNetworkAccessManager* manager = [] {
        auto* created = new QNetworkAccessManager(QCoreApplication::instance());
        created->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return created;
    }();
    return manager;
}

}  // namespace Net
