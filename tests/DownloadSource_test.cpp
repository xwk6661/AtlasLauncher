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

/**
 * 下载源映射表的回归测试。
 *
 * 为什么值得为"换个字符串"写测试：映射错一位（漏掉 `/maven/` 里的斜杠、
 * 把 `resources` 映射成 `/assets/` 而不是 `/assets`）在界面上完全看不出来，
 * 只会表现为"下载失败"，然后被当成网络问题排查半天。这里把文档里每一条映射
 * 都钉死，改规则时必须同步改测试。
 */

#include <QTest>

#include "net/DownloadSource.h"

class DownloadSourceTest : public QObject {
    Q_OBJECT

   private slots:
    void mapsAssets()
    {
        const QUrl official("https://resources.download.minecraft.net/b9/b9f15e2d6073ed2f28bea176dad96df73bc6200f");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(),
                 QString("https://bmclapi2.bangbang93.com/assets/b9/b9f15e2d6073ed2f28bea176dad96df73bc6200f"));
    }

    void mapsMojangLibraries()
    {
        const QUrl official("https://libraries.minecraft.net/com/mojang/brigadier/1.3.10/brigadier-1.3.10.jar");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(),
                 QString("https://bmclapi2.bangbang93.com/maven/com/mojang/brigadier/1.3.10/brigadier-1.3.10.jar"));
    }

    void mapsFabricMaven()
    {
        const QUrl official("https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.20.1/fabric-loader-0.20.1.jar");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(),
                 QString("https://bmclapi2.bangbang93.com/maven/net/fabricmc/fabric-loader/0.20.1/fabric-loader-0.20.1.jar"));
    }

    void mapsForgeMavenAndDropsItsPrefix()
    {
        // files.minecraftforge.net/maven/<path> → /maven/<path>：前缀要被吃掉，不能变成 /maven/maven/
        const QUrl official("https://files.minecraftforge.net/maven/net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0-installer.jar");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(),
                 QString("https://bmclapi2.bangbang93.com/maven/net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0-installer.jar"));
    }

    void mapsNeoForgeReleases()
    {
        const QUrl official("https://maven.neoforged.net/releases/net/neoforged/neoforge/21.1.77/neoforge-21.1.77-universal.jar");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(),
                 QString("https://bmclapi2.bangbang93.com/maven/net/neoforged/neoforge/21.1.77/neoforge-21.1.77-universal.jar"));
    }

    void mapsVersionManifests()
    {
        QCOMPARE(Net::DownloadSource::mapToMirror(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")).toString(),
                 QString("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json"));
        // 1.21.1 的版本 json 走的就是这个域，路径原样保留
        QCOMPARE(Net::DownloadSource::mapToMirror(QUrl("https://piston-meta.mojang.com/v1/packages/abc/1.21.1.json")).toString(),
                 QString("https://bmclapi2.bangbang93.com/v1/packages/abc/1.21.1.json"));
    }

    void keepsUnmappedHosts()
    {
        // 模组平台的业务 API 不在镜像范围内，必须原样保留
        const QStringList untouched = {
            "https://api.modrinth.com/v2/project/sodium/version",
            "https://api.curseforge.com/v1/mods/306612/files",
            "https://ctmrepository.com/api/maps",
            "https://github.com/Atlas/whatever/releases/download/v1/x.jar",
        };
        for (const auto& url : untouched) {
            QCOMPARE(Net::DownloadSource::mapToMirror(QUrl(url)).toString(), url);
        }
    }

    void keepsUnmappedPathsOnMappedHost()
    {
        // 同一个域下不归镜像管的路径（Forge 的 promotions）不能被硬塞进 /maven/
        const QUrl official("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
        QCOMPARE(Net::DownloadSource::mapToMirror(official).toString(), official.toString());
    }

    void isIdempotent()
    {
        const QUrl mirrored("https://bmclapi2.bangbang93.com/assets/b9/b9f15e2d6073ed2f28bea176dad96df73bc6200f");
        QCOMPARE(Net::DownloadSource::mapToMirror(mirrored).toString(), mirrored.toString());
        QVERIFY(Net::DownloadSource::isMirror(mirrored));
        QVERIFY(!Net::DownloadSource::isMirror(QUrl("https://libraries.minecraft.net/x")));
    }
};

QTEST_GUILESS_MAIN(DownloadSourceTest)

#include "DownloadSource_test.moc"
