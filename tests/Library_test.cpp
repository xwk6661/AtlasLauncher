// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include "Json.h"

#include <FileSystem.h>
#include <ResourcePaths.h>
#include <RuntimeContext.h>
#include <minecraft/Library.h>
#include <minecraft/MojangVersionFormat.h>
#include <minecraft/OneSixVersionFormat.h>
#include <net/HttpMetaCache.h>

class LibraryTest : public QObject {
    Q_OBJECT
   private:
    auto readMojangJson(const QString path)
    {
        return Json::requireDocument(path).and_then([&path](const auto& v) {
            ProblemContainer problems;
            return MojangVersionFormat::libraryFromJson(problems, v.object(), path);
        });
    }
    // get absolute path to expected storage, assuming default cache prefix
    QStringList getStorage(QString relative) { return { FS::PathCombine(cache->getBasePath("libraries"), relative) }; }

    RuntimeContext dummyContext(QString system = "linux", QString arch = "64", QString realArch = "amd64")
    {
        RuntimeContext r;
        r.javaArchitecture = arch;
        r.javaRealArchitecture = realArch;
        r.system = system;
        return r;
    }
   private slots:
    void initTestCase()
    {
        cache.reset(new HttpMetaCache());
        cache->addBase("libraries", QDir("libraries").absolutePath());
        dataDir = QDir(QFINDTESTDATA("testdata/Libraries")).absolutePath();
    }
    void test_defaultStoragePrefixIsResourcePath()
    {
        // classpath 里的 jar 路径必须等于库文件实际所在的目录。这里曾经返回相对的
        // "libraries/"（按进程工作目录解析），设了共享资源目录之后 classpath 仍指向
        // 旧目录，客户端启动即 `ClassNotFoundException: net.minecraft.client.main.Main`。
        const QString prefix = Library::defaultStoragePrefix();
        QVERIFY2(QDir::isAbsolutePath(prefix), qPrintable(prefix));
        QCOMPARE(QDir::cleanPath(prefix), QDir::cleanPath(resource::librariesDir()));
    }
    /// 目标是「别的启动器 / 换机器」那种目录：文件在、账本里没有。
    /// 内容对得上就该直接认下来，不能整份重下（本机实测 2.1 GB）。
    void test_adoptsExistingLibraryFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString rel = QStringLiteral("com/paulscode/codecwav/20101023/codecwav-20101023.jar");
        const QString target = FS::PathCombine(tmp.path(), rel);
        QVERIFY(FS::ensureFolderPathExists(QFileInfo(target).absolutePath()));
        QVERIFY(QFile::copy(QFINDTESTDATA("testdata/Libraries/codecwav-20101023.jar"), target));

        // 元数据里的 sha1 按这份文件现算（夹具是 17 字节的占位 jar，不是真库）
        QFile jar(target);
        QVERIFY(jar.open(QIODevice::ReadOnly));
        const QString sha1 = QString::fromLatin1(QCryptographicHash::hash(jar.readAll(), QCryptographicHash::Sha1).toHex());
        jar.close();

        const QString jsonPath = FS::PathCombine(tmp.path(), "lib.json");
        QFile jsonFile(jsonPath);
        QVERIFY(jsonFile.open(QIODevice::WriteOnly));
        const QString jsonText =
            QStringLiteral("{\n"
                           "  \"downloads\": { \"artifact\": {\n"
                           "    \"path\": \"%1\",\n"
                           "    \"sha1\": \"%2\",\n"
                           "    \"size\": 17,\n"
                           "    \"url\": \"https://libraries.minecraft.net/%1\"\n"
                           "  } },\n"
                           "  \"name\": \"com.paulscode:codecwav:20101023\"\n"
                           "}\n")
                .arg(rel, sha1);
        jsonFile.write(jsonText.toUtf8());
        jsonFile.close();

        HttpMetaCache cache;
        cache.addBase("libraries", tmp.path());

        auto lib = readMojangJson(jsonPath);
        QVERIFY2(lib.has_value(), "fixture must parse");

        QStringList failed;
        auto downloads = lib.value()->getDownloads(dummyContext(), &cache, failed, QString());
        QCOMPARE(failed, {});
        QCOMPARE(downloads.size(), 0);

        // 认领之后账本里要有一条有效记录，否则每一轮都要再哈希一遍
        auto entry = cache.resolveEntry("libraries", rel);
        QVERIFY(entry != nullptr);
        QVERIFY2(!entry->isStale(), "adopted entry should be valid");
    }

    /// 内容对不上（sha1 不匹配）时仍然要下载 —— 认领只认内容，不认文件名
    void test_doesNotAdoptMismatchedFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString rel = QStringLiteral("com/paulscode/codecwav/20101023/codecwav-20101023.jar");
        const QString target = FS::PathCombine(tmp.path(), rel);
        QVERIFY(FS::ensureFolderPathExists(QFileInfo(target).absolutePath()));
        // 同名但内容不同：另一个真实 jar 顶替上去
        QVERIFY(QFile::copy(QFINDTESTDATA("testdata/Libraries/testname-testversion-linux-32.jar"), target));

        HttpMetaCache cache;
        cache.addBase("libraries", tmp.path());

        auto lib = readMojangJson(QFINDTESTDATA("testdata/Libraries/lib-simple.json"));
        QVERIFY2(lib.has_value(), "fixture must parse");

        QStringList failed;
        auto downloads = lib.value()->getDownloads(dummyContext(), &cache, failed, QString());
        QCOMPARE(downloads.size(), 1);
    }

    void test_legacy()
    {
        RuntimeContext r = dummyContext();
        Library test("test.package:testname:testversion");
        QCOMPARE(test.artifactPrefix(), QString("test.package:testname"));
        QCOMPARE(test.isNative(), false);

        QStringList jar, native, native32, native64;
        test.getApplicableFiles(r, jar, native, native32, native64, QString());
        QCOMPARE(jar, getStorage("test/package/testname/testversion/testname-testversion.jar"));
        QCOMPARE(native, {});
        QCOMPARE(native32, {});
        QCOMPARE(native64, {});
    }
    void test_legacy_url()
    {
        RuntimeContext r = dummyContext();
        QStringList failedFiles;
        Library test("test.package:testname:testversion");
        test.setRepositoryURL("file://foo/bar");
        auto downloads = test.getDownloads(r, cache.get(), failedFiles, QString());
        QCOMPARE(downloads.size(), 1);
        QCOMPARE(failedFiles, {});
        Net::Request::Ptr dl = downloads[0];
        QCOMPARE(dl->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion.jar"));
    }
    void test_legacy_url_local_broken()
    {
        RuntimeContext r = dummyContext();
        Library test("test.package:testname:testversion");
        QCOMPARE(test.isNative(), false);
        QStringList failedFiles;
        test.setHint("local");
        auto downloads = test.getDownloads(r, cache.get(), failedFiles, QString());
        QCOMPARE(downloads.size(), 0);
        QCOMPARE(failedFiles, { "testname-testversion.jar" });
    }
    void test_legacy_url_local_override()
    {
        RuntimeContext r = dummyContext();
        Library test("com.paulscode:codecwav:20101023");
        QCOMPARE(test.isNative(), false);
        QStringList failedFiles;
        test.setHint("local");
        auto downloads = test.getDownloads(r, cache.get(), failedFiles, QFINDTESTDATA("testdata/Libraries"));
        QCOMPARE(downloads.size(), 0);
        qDebug() << failedFiles;
        QCOMPARE(failedFiles.size(), 0);

        QStringList jar, native, native32, native64;
        test.getApplicableFiles(r, jar, native, native32, native64, QFINDTESTDATA("testdata/Libraries"));
        QCOMPARE(jar, { QFileInfo(QFINDTESTDATA("testdata/Libraries/codecwav-20101023.jar")).absoluteFilePath() });
        QCOMPARE(native, {});
        QCOMPARE(native32, {});
        QCOMPARE(native64, {});
    }
    void test_legacy_native()
    {
        RuntimeContext r = dummyContext();
        Library test("test.package:testname:testversion");
        test.m_nativeClassifiers["linux"] = "linux";
        QCOMPARE(test.isNative(), true);
        test.setRepositoryURL("file://foo/bar");
        {
            QStringList jar, native, native32, native64;
            test.getApplicableFiles(r, jar, native, native32, native64, QString());
            QCOMPARE(jar, {});
            QCOMPARE(native, getStorage("test/package/testname/testversion/testname-testversion-linux.jar"));
            QCOMPARE(native32, {});
            QCOMPARE(native64, {});
            QStringList failedFiles;
            auto dls = test.getDownloads(r, cache.get(), failedFiles, QString());
            QCOMPARE(dls.size(), 1);
            QCOMPARE(failedFiles, {});
            auto dl = dls[0];
            QCOMPARE(dl->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-linux.jar"));
        }
    }
    void test_legacy_native_arch()
    {
        RuntimeContext r = dummyContext();
        Library test("test.package:testname:testversion");
        test.m_nativeClassifiers["linux"] = "linux-${arch}";
        test.m_nativeClassifiers["osx"] = "osx-${arch}";
        test.m_nativeClassifiers["windows"] = "windows-${arch}";
        QCOMPARE(test.isNative(), true);
        test.setRepositoryURL("file://foo/bar");
        {
            QStringList jar, native, native32, native64;
            test.getApplicableFiles(r, jar, native, native32, native64, QString());
            QCOMPARE(jar, {});
            QCOMPARE(native, {});
            QCOMPARE(native32, getStorage("test/package/testname/testversion/testname-testversion-linux-32.jar"));
            QCOMPARE(native64, getStorage("test/package/testname/testversion/testname-testversion-linux-64.jar"));
            QStringList failedFiles;
            auto dls = test.getDownloads(r, cache.get(), failedFiles, QString());
            QCOMPARE(dls.size(), 2);
            QCOMPARE(failedFiles, {});
            QCOMPARE(dls[0]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-linux-32.jar"));
            QCOMPARE(dls[1]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-linux-64.jar"));
        }
        r.system = "windows";
        {
            QStringList jar, native, native32, native64;
            test.getApplicableFiles(r, jar, native, native32, native64, QString());
            QCOMPARE(jar, {});
            QCOMPARE(native, {});
            QCOMPARE(native32, getStorage("test/package/testname/testversion/testname-testversion-windows-32.jar"));
            QCOMPARE(native64, getStorage("test/package/testname/testversion/testname-testversion-windows-64.jar"));
            QStringList failedFiles;
            auto dls = test.getDownloads(r, cache.get(), failedFiles, QString());
            QCOMPARE(dls.size(), 2);
            QCOMPARE(failedFiles, {});
            QCOMPARE(dls[0]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-windows-32.jar"));
            QCOMPARE(dls[1]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-windows-64.jar"));
        }
        r.system = "osx";
        {
            QStringList jar, native, native32, native64;
            test.getApplicableFiles(r, jar, native, native32, native64, QString());
            QCOMPARE(jar, {});
            QCOMPARE(native, {});
            QCOMPARE(native32, getStorage("test/package/testname/testversion/testname-testversion-osx-32.jar"));
            QCOMPARE(native64, getStorage("test/package/testname/testversion/testname-testversion-osx-64.jar"));
            QStringList failedFiles;
            auto dls = test.getDownloads(r, cache.get(), failedFiles, QString());
            QCOMPARE(dls.size(), 2);
            QCOMPARE(failedFiles, {});
            QCOMPARE(dls[0]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-osx-32.jar"));
            QCOMPARE(dls[1]->url(), QUrl("file://foo/bar/test/package/testname/testversion/testname-testversion-osx-64.jar"));
        }
    }
    void test_legacy_native_arch_local_override()
    {
        RuntimeContext r = dummyContext();
        Library test("test.package:testname:testversion");
        test.m_nativeClassifiers["linux"] = "linux-${arch}";
        test.setHint("local");
        QCOMPARE(test.isNative(), true);
        test.setRepositoryURL("file://foo/bar");
        {
            QStringList jar, native, native32, native64;
            test.getApplicableFiles(r, jar, native, native32, native64, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(jar, {});
            QCOMPARE(native, {});
            QCOMPARE(native32, { QFileInfo(QFINDTESTDATA("testdata/Libraries/testname-testversion-linux-32.jar")).absoluteFilePath() });
            QCOMPARE(native64,
                     { QFileInfo(QFINDTESTDATA("testdata/Libraries") + "/testname-testversion-linux-64.jar").absoluteFilePath() });
            QStringList failedFiles;
            auto dls = test.getDownloads(r, cache.get(), failedFiles, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(dls.size(), 0);
            QCOMPARE(failedFiles,
                     { QFileInfo(QFINDTESTDATA("testdata/Libraries") + "/testname-testversion-linux-64.jar").absoluteFilePath() });
        }
    }
    void test_onenine()
    {
        RuntimeContext r = dummyContext("osx");
        auto test = readMojangJson(QFINDTESTDATA("testdata/Libraries/lib-simple.json"));
        QVERIFY2(test, test.has_value() ? "" : qPrintable(test.error()));
        {
            QStringList jar, native, native32, native64;
            test.value()->getApplicableFiles(r, jar, native, native32, native64, QString());
            QCOMPARE(jar, getStorage("com/paulscode/codecwav/20101023/codecwav-20101023.jar"));
            QCOMPARE(native, {});
            QCOMPARE(native32, {});
            QCOMPARE(native64, {});
        }
        r.system = "linux";
        {
            QStringList failedFiles;
            auto dls = test.value()->getDownloads(r, cache.get(), failedFiles, QString());
            QCOMPARE(dls.size(), 1);
            QCOMPARE(failedFiles, {});
            QCOMPARE(dls[0]->url(), QUrl("https://libraries.minecraft.net/com/paulscode/codecwav/20101023/codecwav-20101023.jar"));
        }
        r.system = "osx";
        test.value()->setHint("local");
        {
            QStringList jar, native, native32, native64;
            test.value()->getApplicableFiles(r, jar, native, native32, native64, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(jar, { QFileInfo(QFINDTESTDATA("testdata/Libraries/codecwav-20101023.jar")).absoluteFilePath() });
            QCOMPARE(native, {});
            QCOMPARE(native32, {});
            QCOMPARE(native64, {});
        }
        r.system = "linux";
        {
            QStringList failedFiles;
            auto dls = test.value()->getDownloads(r, cache.get(), failedFiles, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(dls.size(), 0);
            QCOMPARE(failedFiles, {});
        }
    }
    void test_onenine_local_override()
    {
        RuntimeContext r = dummyContext("osx");
        auto test = readMojangJson(QFINDTESTDATA("testdata/Libraries/lib-simple.json"));
        QVERIFY2(test, test.has_value() ? "" : qPrintable(test.error()));
        test.value()->setHint("local");
        {
            QStringList jar, native, native32, native64;
            test.value()->getApplicableFiles(r, jar, native, native32, native64, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(jar, { QFileInfo(QFINDTESTDATA("testdata/Libraries/codecwav-20101023.jar")).absoluteFilePath() });
            QCOMPARE(native, {});
            QCOMPARE(native32, {});
            QCOMPARE(native64, {});
        }
        r.system = "linux";
        {
            QStringList failedFiles;
            auto dls = test.value()->getDownloads(r, cache.get(), failedFiles, QFINDTESTDATA("testdata/Libraries"));
            QCOMPARE(dls.size(), 0);
            QCOMPARE(failedFiles, {});
        }
    }
    void test_onenine_native()
    {
        RuntimeContext r = dummyContext("osx");
        auto test = readMojangJson(QFINDTESTDATA("testdata/Libraries/lib-native.json"));
        QVERIFY2(test, test.has_value() ? "" : qPrintable(test.error()));
        QStringList jar, native, native32, native64;
        test.value()->getApplicableFiles(r, jar, native, native32, native64, QString());
        QCOMPARE(jar, QStringList());
        QCOMPARE(native,
                 getStorage("org/lwjgl/lwjgl/lwjgl-platform/2.9.4-nightly-20150209/lwjgl-platform-2.9.4-nightly-20150209-natives-osx.jar"));
        QCOMPARE(native32, {});
        QCOMPARE(native64, {});
        QStringList failedFiles;
        auto dls = test.value()->getDownloads(r, cache.get(), failedFiles, QString());
        QCOMPARE(dls.size(), 1);
        QCOMPARE(failedFiles, {});
        QCOMPARE(dls[0]->url(), QUrl("https://libraries.minecraft.net/org/lwjgl/lwjgl/lwjgl-platform/2.9.4-nightly-20150209/"
                                     "lwjgl-platform-2.9.4-nightly-20150209-natives-osx.jar"));
    }
    void test_onenine_native_arch()
    {
        RuntimeContext r = dummyContext("windows");
        auto test = readMojangJson(QFINDTESTDATA("testdata/Libraries/lib-native-arch.json"));
        QVERIFY2(test, test.has_value() ? "" : qPrintable(test.error()));
        QStringList jar, native, native32, native64;
        test.value()->getApplicableFiles(r, jar, native, native32, native64, QString());
        QCOMPARE(jar, {});
        QCOMPARE(native, {});
        QCOMPARE(native32, getStorage("tv/twitch/twitch-platform/5.16/twitch-platform-5.16-natives-windows-32.jar"));
        QCOMPARE(native64, getStorage("tv/twitch/twitch-platform/5.16/twitch-platform-5.16-natives-windows-64.jar"));
        QStringList failedFiles;
        auto dls = test.value()->getDownloads(r, cache.get(), failedFiles, QString());
        QCOMPARE(dls.size(), 2);
        QCOMPARE(failedFiles, {});
        QCOMPARE(dls[0]->url(),
                 QUrl("https://libraries.minecraft.net/tv/twitch/twitch-platform/5.16/twitch-platform-5.16-natives-windows-32.jar"));
        QCOMPARE(dls[1]->url(),
                 QUrl("https://libraries.minecraft.net/tv/twitch/twitch-platform/5.16/twitch-platform-5.16-natives-windows-64.jar"));
    }

   private:
    std::unique_ptr<HttpMetaCache> cache;
    QString dataDir;
};

QTEST_GUILESS_MAIN(LibraryTest)

#include "Library_test.moc"
