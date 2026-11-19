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
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "minecraft/AssetsUtils.h"

namespace {

QString sha1Of(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

}  // namespace

/// 「本地这份素材清单还能不能用」——只看内容，不看时间戳、不看账本。
/// 2026-11-19 的 1.21.1 就是被这条判据挡住的：上游重发了 `17.json`，语言文件 hash 全换，
/// 本地那份（sha1 对不上）必须重下；而其它 33 份索引每次都命中，一个字节都不该再要。
class AssetIndexCacheTest : public QObject {
    Q_OBJECT

   private slots:
    void matchingIndexIsReused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("17.json"));
        const QByteArray data = R"({"objects":{}})";
        QVERIFY(writeFile(path, data));

        QVERIFY(AssetsUtils::indexFileMatches(path, sha1Of(data), data.size()));
        // sha1 大小写不该影响结论（元数据里的写法来自上游 json）
        QVERIFY(AssetsUtils::indexFileMatches(path, sha1Of(data).toUpper(), data.size()));
    }

    void sameSizeButChangedContentIsNotReused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("17.json"));
        const QByteArray oldData = R"({"objects":{"a":1}})";
        const QByteArray newData = R"({"objects":{"a":2}})";
        QCOMPARE(oldData.size(), newData.size());
        QVERIFY(writeFile(path, oldData));

        QVERIFY(!AssetsUtils::indexFileMatches(path, sha1Of(newData), newData.size()));
    }

    void truncatedIndexIsNotReused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("17.json"));
        const QByteArray data = R"({"objects":{"a":1,"b":2}})";
        QVERIFY(writeFile(path, data.left(data.size() / 2)));

        // 半截文件：大小先挡住，不必算哈希
        QVERIFY(!AssetsUtils::indexFileMatches(path, sha1Of(data), data.size()));
        // 元数据没给大小时只剩哈希这道闸
        QVERIFY(!AssetsUtils::indexFileMatches(path, sha1Of(data), 0));
    }

    void missingIndexIsNotReused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("17.json"));

        QVERIFY(!AssetsUtils::indexFileMatches(path, sha1Of("x"), 1));
    }

    void unknownSha1IsNeverReused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("17.json"));
        const QByteArray data = R"({"objects":{}})";
        QVERIFY(writeFile(path, data));

        // 版本 json 没给 sha1（自定义 meta）：判不出就不敢省这一步
        QVERIFY(!AssetsUtils::indexFileMatches(path, QString(), data.size()));
    }
};

QTEST_GUILESS_MAIN(AssetIndexCacheTest)

#include "AssetIndexCache_test.moc"
