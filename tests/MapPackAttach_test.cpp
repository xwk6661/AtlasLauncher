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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"
#include "maps/MapPackAttach.h"

/// 资源包形态的汉化：不能建实例，只能挂到已装实例上
class MapPackAttachTest : public QObject {
    Q_OBJECT

   private slots:
    void addToOptionsAppends()
    {
        QStringList lines{ QStringLiteral("version:3465"),
                           QStringLiteral("resourcePacks:[\"vanilla\"]"),
                           QStringLiteral("lang:zh_cn") };

        QVERIFY(maps::MapPackAttach::addToOptionsLines(lines, QStringLiteral("file/zh_cn.zip")));
        QCOMPARE(lines.at(1), QStringLiteral("resourcePacks:[\"vanilla\",\"file/zh_cn.zip\"]"));
        // 其余行一个字节不动
        QCOMPARE(lines.at(0), QStringLiteral("version:3465"));
        QCOMPARE(lines.at(2), QStringLiteral("lang:zh_cn"));
    }

    void addToOptionsIsIdempotent()
    {
        QStringList lines{ QStringLiteral("resourcePacks:[\"vanilla\",\"file/a.zip\"]") };
        QVERIFY(!maps::MapPackAttach::addToOptionsLines(lines, QStringLiteral("file/a.zip")));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("resourcePacks:[\"vanilla\",\"file/a.zip\"]"));
    }

    void addToOptionsCreatesLine()
    {
        QStringList lines{ QStringLiteral("version:3465") };
        QVERIFY(maps::MapPackAttach::addToOptionsLines(lines, QStringLiteral("file/b.zip")));
        QCOMPARE(lines.size(), 2);
        // vanilla 必须在，否则资源包链断了
        QCOMPARE(lines.at(1), QStringLiteral("resourcePacks:[\"vanilla\",\"file/b.zip\"]"));
    }

    void addToOptionsLeavesUnknownFormatAlone()
    {
        QStringList lines{ QStringLiteral("resourcePacks:vanilla") };
        QVERIFY(!maps::MapPackAttach::addToOptionsLines(lines, QStringLiteral("file/c.zip")));
        QCOMPARE(lines.first(), QStringLiteral("resourcePacks:vanilla"));
    }

    void inspectRootPack()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("zh_cn_pack.zip"));
        QVERIFY(makeZip(zip, { { QStringLiteral("pack.mcmeta"), QByteArray("{}") }, { QStringLiteral("assets/x.txt"), QByteArray("x") } }));

        const auto probe = maps::MapPackAttach::inspect(zip);
        QVERIFY2(probe.valid, qPrintable(probe.reason));
        QVERIFY(probe.rootFolder.isEmpty());
        QCOMPARE(probe.packId, QStringLiteral("zh_cn_pack"));
    }

    void inspectNestedPack()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("nested.zip"));
        QVERIFY(makeZip(zip, { { QStringLiteral("MyPack/pack.mcmeta"), QByteArray("{}") }, { QStringLiteral("MyPack/assets/x.txt"), QByteArray("x") } }));

        const auto probe = maps::MapPackAttach::inspect(zip);
        QVERIFY2(probe.valid, qPrintable(probe.reason));
        QCOMPARE(probe.rootFolder, QStringLiteral("MyPack"));
        QCOMPARE(probe.packId, QStringLiteral("MyPack"));
    }

    void inspectRejectsMapArchive()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("map.zip"));
        // 有 pack.mcmeta 也有 level.dat → 还是地图包，该走导入
        QVERIFY(makeZip(zip, { { QStringLiteral("pack.mcmeta"), QByteArray("{}") }, { QStringLiteral("world/level.dat"), QByteArray("nbt") } }));

        const auto probe = maps::MapPackAttach::inspect(zip);
        QVERIFY(!probe.valid);
        QVERIFY2(probe.reason.contains(QStringLiteral("地图包")), qPrintable(probe.reason));
    }

    void inspectRejectsOrdinaryZip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("plain.zip"));
        QVERIFY(makeZip(zip, { { QStringLiteral("readme.txt"), QByteArray("hi") } }));

        const auto probe = maps::MapPackAttach::inspect(zip);
        QVERIFY(!probe.valid);
        QVERIFY2(probe.reason.contains(QStringLiteral("pack.mcmeta")), qPrintable(probe.reason));
    }

    void attachCopiesAndEnables()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString instanceRoot = FS::PathCombine(tmp.path(), QStringLiteral("inst"));
        const QString gameDir = FS::PathCombine(instanceRoot, QStringLiteral(".minecraft"));
        QVERIFY(FS::ensureFolderPathExists(gameDir));

        QFile options(FS::PathCombine(gameDir, QStringLiteral("options.txt")));
        QVERIFY(options.open(QIODevice::WriteOnly));
        options.write("version:3465\nresourcePacks:[\"vanilla\"]\nlang:zh_cn\n");
        options.close();

        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("汉化包.zip"));
        QVERIFY(makeZip(zip, { { QStringLiteral("pack.mcmeta"), QByteArray("{}") }, { QStringLiteral("assets/zh_cn.lang"), QByteArray("x") } }));

        const auto attached = maps::MapPackAttach::attach(zip, instanceRoot);
        QVERIFY2(attached.has_value(), attached.has_value() ? "" : qPrintable(attached.error()));
        QCOMPARE(*attached, QStringLiteral("file/汉化包.zip"));

        const QString copied = FS::PathCombine(gameDir, QStringLiteral("resourcepacks/汉化包.zip"));
        QVERIFY2(QFileInfo::exists(copied), qPrintable(copied));

        const QString optionsPath = FS::PathCombine(gameDir, QStringLiteral("options.txt"));
        QString text;
        {
            QFile written(optionsPath);
            QVERIFY(written.open(QIODevice::ReadOnly));
            text = QString::fromUtf8(written.readAll());
        }
        QVERIFY2(text.contains(QStringLiteral("file/汉化包.zip")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("version:3465")));
        QVERIFY(text.contains(QStringLiteral("lang:zh_cn")));

        // 再挂一次：不重复、也不报错（文件必须先关掉，Windows 上开着改不了）
        const auto again = maps::MapPackAttach::attach(zip, instanceRoot);
        QVERIFY2(again.has_value(), again.has_value() ? "" : qPrintable(again.error()));
        QString second;
        {
            QFile reread(optionsPath);
            QVERIFY(reread.open(QIODevice::ReadOnly));
            second = QString::fromUtf8(reread.readAll());
        }
        QCOMPARE(second.count(QStringLiteral("file/汉化包.zip")), 1);
    }

    void attachRequiresOptionsTxt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString instanceRoot = FS::PathCombine(tmp.path(), QStringLiteral("inst"));
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(instanceRoot, QStringLiteral(".minecraft"))));

        const QString zip = FS::PathCombine(tmp.path(), QStringLiteral("pack.zip"));
        QVERIFY(makeZip(zip, { { QStringLiteral("pack.mcmeta"), QByteArray("{}") } }));

        const auto attached = maps::MapPackAttach::attach(zip, instanceRoot);
        QVERIFY(!attached.has_value());
        QVERIFY2(attached.error().contains(QStringLiteral("options.txt")), qPrintable(attached.error()));
    }

   private:
    static bool makeZip(const QString& path, const QList<QPair<QString, QByteArray>>& files)
    {
        MMCZip::ArchiveWriter writer(path);
        if (!writer.open())
            return false;
        for (const auto& file : files) {
            if (!writer.addFile(file.first, file.second))
                return false;
        }
        return writer.close().has_value();
    }
};

QTEST_GUILESS_MAIN(MapPackAttachTest)

#include "MapPackAttach_test.moc"
