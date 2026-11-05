// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas Launcher - Minecraft launcher fork with a map-first workflow
 *  Copyright (C) 2026 Atlas contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * 镜像换节点重试的两半：**判不判**（错误分类）与**敢不敢**（写了一半的文件怎么处理）。
 *
 * 后者是这次改动真正的风险点，也是文档里一直挂着"验证前不动网络层"的原因：
 * 连接错误往往发生在写了几百 KB 之后，如果重入 init() 是 append 语义，重试会拼出一个
 * 校验不过的坏 jar，而且看起来像"镜像给了坏文件"。这里用真实的 FileSink + QSaveFile
 * 把三种时序钉死：重入 init、abort 后重来、失败不碰目标文件。
 */

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstring>

#include "net/ByteArraySink.h"
#include "net/ChecksumValidator.h"
#include "net/FileSink.h"
#include "net/Request.h"

namespace {

/// 只为 finalize() 提供状态码的最小 QNetworkReply
class StubReply : public QNetworkReply {
   public:
    explicit StubReply(int statusCode, QObject* parent = nullptr) : QNetworkReply(parent)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        open(QIODevice::ReadOnly);
    }

    void abort() override {}
    auto readData(char*, qint64) -> qint64 override { return -1; }

    /// 206 的长度校验要靠它：FileSink 用 Content-Length 判断"这一段收全了没"
    void setContentLength(qint64 length) { setHeader(QNetworkRequest::ContentLengthHeader, length); }
};

auto readAll(const QString& path) -> QByteArray
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
    file.close();
}

/**
 * 假 reply：可以在"给一个 302"、"连接被重置"、"正常给数据"三种表现之间选一种。
 *
 * 信号要等 Request 把 errorOccurred / finished 连上之后再发，所以统一走事件循环。
 */
class FakeReply : public QNetworkReply {
   public:
    enum class Mode { Redirect, Fail, Stall, Ok };

    FakeReply(Mode mode, QByteArray payload = {}, QByteArray location = {}, QObject* parent = nullptr)
        : QNetworkReply(parent), m_mode(mode), m_payload(std::move(payload))
    {
        // 只有在"要跳转"这一种模式下才带 Location —— handleRedirect 只认这个头，
        // 顺手带上的话，一次连接失败会被当成无限重定向。
        if (mode == Mode::Redirect) {
            setRawHeader("Location", location);
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 302);
        } else if (mode == Mode::Ok)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
    }

    void abort() override {}

    void kick()
    {
        QTimer::singleShot(0, this, [this] {
            switch (m_mode) {
                case Mode::Redirect:
                    break;
                case Mode::Fail:
                    setError(QNetworkReply::RemoteHostClosedError, "connection closed");
                    emit errorOccurred(QNetworkReply::RemoteHostClosedError);
                    break;
                case Mode::Stall:
                    // Qt 的传输超时到期时就是这么干的：abort 掉 reply，报 OperationCanceledError
                    setError(QNetworkReply::OperationCanceledError, "Operation canceled");
                    emit errorOccurred(QNetworkReply::OperationCanceledError);
                    break;
                case Mode::Ok:
                    emit readyRead();
                    break;
            }
            emit finished();
        });
    }

   protected:
    auto readData(char* data, qint64 maxSize) -> qint64 override
    {
        const qint64 left = m_payload.size() - m_offset;
        if (left <= 0)
            return -1;
        const qint64 n = std::min(maxSize, left);
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(n));
        m_offset += n;
        return n;
    }

   private:
    Mode m_mode;
    QByteArray m_payload;
    qint64 m_offset = 0;
};

/// 按脚本应答的 Request：记录每一次实际打出去的地址
class ScriptedRequest : public Net::Request {
   public:
    ScriptedRequest(const QUrl& url,
                    QList<QPair<FakeReply::Mode, QByteArray>> script,
                    Net::Request::Options options = Net::Request::Option::NoSourceRewrite)
        : Net::Request(url, options), m_script(std::move(script))
    {
        auto sink = std::make_unique<Net::ByteArraySink>();
        m_output = sink->output();
        setSink(std::move(sink));
        // 官方地址不会走 directNetwork()，得自己给一个（getReply 被覆写，实际不会用到它）。
        // 单测里没有 Application，不设的话 Request 会以 "No network manager" 失败。
        setNetwork(&m_network);
    }

    QList<QUrl> requestedUrls;
    QByteArray* output() { return m_output; }

   protected:
    auto getReply(QNetworkRequest& request) -> QNetworkReply* override
    {
        requestedUrls.append(request.url());
        const int index = std::min<int>(requestedUrls.size() - 1, m_script.size() - 1);
        const auto [mode, payload] = m_script.at(index);
        // 302 的目标固定指向一个"不可达节点"的地址，用来验证重试是否回到入口
        auto* reply = new FakeReply(mode, payload, QByteArray("https://node-7.example.invalid/file.jar"));
        reply->kick();
        return reply;
    }

   private:
    QList<QPair<FakeReply::Mode, QByteArray>> m_script;
    QByteArray* m_output = nullptr;
    QNetworkAccessManager m_network;
};

}  // namespace

class RequestRetryTest : public QObject {
    Q_OBJECT

   private slots:
    void retriesOnlyConnectionLevelErrors()
    {
        using Net::Request;
        // BMCLAPI 302 落到坏节点的三种典型症状
        QVERIFY(Request::isRetryableNetworkError(QNetworkReply::RemoteHostClosedError, -1));
        QVERIFY(Request::isRetryableNetworkError(QNetworkReply::ConnectionRefusedError, -1));
        QVERIFY(Request::isRetryableNetworkError(QNetworkReply::HostNotFoundError, -1));
        QVERIFY(Request::isRetryableNetworkError(QNetworkReply::TimeoutError, -1));
        // 下载到一半连接断了：状态码已经是 200，仍然值得换节点重来
        QVERIFY(Request::isRetryableNetworkError(QNetworkReply::RemoteHostClosedError, 200));

        // 对面答过话的都不是网络抖动，重试只会浪费时间
        QVERIFY(!Request::isRetryableNetworkError(QNetworkReply::ContentNotFoundError, 404));
        QVERIFY(!Request::isRetryableNetworkError(QNetworkReply::RemoteHostClosedError, 503));
        QVERIFY(!Request::isRetryableNetworkError(QNetworkReply::NoError, 200));
        // 用户自己取消的，绝不能"重试"回来
        QVERIFY(!Request::isRetryableNetworkError(QNetworkReply::OperationCanceledError, -1));
        // 磁盘/权限这类本地故障，换节点也没用
        QVERIFY(!Request::isRetryableNetworkError(QNetworkReply::ProtocolFailure, -1));
    }

    /// 重入 init() 是"从零写"，不是 append —— 这是重试能不能上线的前提
    /// 重试必须**续传**，不是重来：CDN 会在传到一半时关连接，从头下永远下不完（06 §36）
    void fileSinkResumesFromPartialFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("lib.jar");

        Net::FileSink sink(path);
        QNetworkRequest first{ QUrl("https://libraries.minecraft.net/maven/x.jar") };

        auto result = sink.init(first);
        QVERIFY(result.has_value());
        QVERIFY(*result == Net::Sink::InitType::Ok);
        QVERIFY(!sink.requestedRange());
        QVERIFY(sink.write(QByteArray("HALF-A-FILE")).has_value());

        // 不 abort、直接再来一轮 —— 这正是 Request 重试的时序
        QNetworkRequest second{ QUrl("https://libraries.minecraft.net/maven/x.jar") };
        QVERIFY(sink.init(second).has_value());
        QVERIFY(sink.requestedRange());
        QCOMPARE(second.rawHeader("Range"), QByteArray("bytes=11-"));
        QCOMPARE(sink.bytesWritten(), qint64(11));

        QVERIFY(sink.write(QByteArray("-GOOD")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("HALF-A-FILE-GOOD"));
        QVERIFY2(!QFile::exists(path + ".part"), "收尾后不该留下 .part");
    }

    /// 分片模式的预分配文件（满尺寸 + 空洞）绝不能被顺序下载当成断点
    void parallelScratchIsNotUsedAsResumePrefix()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("big.jar");

        Net::FileSink sink(path);
        QNetworkRequest request{ QUrl("https://libraries.minecraft.net/maven/big.jar") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.beginParallel(4096).has_value());
        QVERIFY(sink.writeAt(0, QByteArray("abc")).has_value());

        // 模拟分片失败后回到顺序下载（Request 会重入 init）
        QNetworkRequest again{ QUrl("https://libraries.minecraft.net/maven/big.jar") };
        QVERIFY(sink.init(again).has_value());
        QVERIFY2(!sink.requestedRange(), "预分配的空洞文件不能当断点，否则会 Range 到文件末尾");
        QCOMPARE(again.rawHeader("Range"), QByteArray());
        QVERIFY(sink.write(QByteArray("fresh")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("fresh"));
        QVERIFY(!QFile::exists(path + ".part.parallel"));
    }

    /// 服务器无视 Range（回了 200 全量）时必须丢掉半截重写，否则文件会重复一倍
    void restartFromZeroAfterServerIgnoresRange()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("asset.bin");

        Net::FileSink sink(path);
        QNetworkRequest request{ QUrl("https://resources.download.minecraft.net/xx/hash") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("OLD-HALF")).has_value());

        QNetworkRequest again{ request };
        QVERIFY(sink.init(again).has_value());
        QVERIFY(sink.requestedRange());

        // Request 在 downloadReadyRead 里做的就是这一下
        sink.restartFromZero();
        QVERIFY(sink.write(QByteArray("WHOLE")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("WHOLE"));
    }

    /// 续传前必须把已有字节补喂给校验器，否则 sha1 永远对不上
    void resumeFeedsValidatorsWithExistingBytes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("checked.jar");
        const QByteArray payload("HELLO-WORLD");

        Net::FileSink sink(path);
        auto* checksum = new Net::ChecksumValidator(QCryptographicHash::Sha1);
        checksum->setExpected(QCryptographicHash::hash(payload, QCryptographicHash::Sha1));
        sink.addValidator(checksum);

        QNetworkRequest request{ QUrl("https://libraries.minecraft.net/maven/checked.jar") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(payload.left(6)).has_value());

        QNetworkRequest again{ request };
        QVERIFY(sink.init(again).has_value());
        QVERIFY(sink.write(payload.mid(6)).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), payload);
    }

    /// 206 只收到一段、长度不够：保留 .part 等下一轮续，不能落到目标路径
    void incompleteResumeKeepsPartFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("partial.jar");

        Net::FileSink sink(path);
        QNetworkRequest request{ QUrl("https://libraries.minecraft.net/maven/partial.jar") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("ABC")).has_value());

        StubReply reply(206);
        reply.setContentLength(10);  // 说好还有 10 字节，实际只写了 3
        QVERIFY(!sink.finalize(reply).has_value());
        QVERIFY2(!QFile::exists(path), "不完整的数据不能落到目标路径");
        QVERIFY2(QFile::exists(path + ".part"), "半截文件要留着给下一轮续传");
    }

    /// Request 里真实走的那条路：abort() 掉上一轮，再 init()
    void fileSinkRestartsAfterAbort()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("asset");
        writeFile(path, QByteArray("OLD-BUT-VALID"));

        Net::FileSink sink(path);
        QNetworkRequest request{ QUrl("https://bmclapi2.bangbang93.com/assets/ab/abcdef") };

        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("garbage")).has_value());
        sink.abort();

        // 没 commit 过，目标文件必须原封不动
        QCOMPARE(readAll(path), QByteArray("OLD-BUT-VALID"));

        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("NEW")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("NEW"));
    }

    /// 校验器也必须跟着重来：否则第二轮会接着上一轮的哈希，算出永远不对的 md5
    void abortAndReinitResetsValidators()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("checked.jar");

        Net::FileSink sink(path);
        auto* checksum = new Net::ChecksumValidator(QCryptographicHash::Md5);
        checksum->setExpected(QCryptographicHash::hash(QByteArray("GOOD"), QCryptographicHash::Md5));
        sink.addValidator(checksum);

        QNetworkRequest request{ QUrl("https://bmclapi2.bangbang93.com/maven/checked.jar") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("HALF")).has_value());
        sink.abort();

        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.write(QByteArray("GOOD")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("GOOD"));
    }

    /// 端到端：302 → 节点断连 → 必须回到**入口**再要一次（而不是继续砸那个坏节点）
    void retriesFromMirrorEntryAfterNodeFailure()
    {
        const QUrl entrance("https://bmclapi2.bangbang93.com/maven/x.jar");
        ScriptedRequest request(
            entrance, { { FakeReply::Mode::Redirect, {} }, { FakeReply::Mode::Fail, {} }, { FakeReply::Mode::Ok, QByteArray("PAYLOAD") } });

        QSignalSpy succeeded(&request, &Task::succeeded);
        QSignalSpy failed(&request, &Task::failed);
        request.start();

        QVERIFY(succeeded.wait(8000));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(*request.output(), QByteArray("PAYLOAD"));

        QCOMPARE(request.requestedUrls.size(), 3);
        QCOMPARE(request.requestedUrls.at(0), entrance);                                                       // 入口
        QCOMPARE(request.requestedUrls.at(1).toString(), QString("https://node-7.example.invalid/file.jar"));  // 302 节点
        QCOMPARE(request.requestedUrls.at(2), entrance);                                                       // 回到入口重新 302
    }

    /// 一直连不上：试满 MAX_NETWORK_RETRIES 次就老实失败，不要无限重试
    void givesUpAfterRetryBudget()
    {
        const QUrl entrance("https://bmclapi2.bangbang93.com/maven/y.jar");
        ScriptedRequest request(entrance, { { FakeReply::Mode::Fail, {} } });

        QSignalSpy succeeded(&request, &Task::succeeded);
        QSignalSpy failed(&request, &Task::failed);
        request.start();

        QVERIFY(failed.wait(8000));
        QCOMPARE(succeeded.count(), 0);
        QCOMPARE(request.requestedUrls.size(), 1 + Net::Request::MAX_NETWORK_RETRIES);
        for (const auto& url : request.requestedUrls)
            QCOMPARE(url, entrance);
    }

    /// 自己管重试的调用方（连通性测试）开关必须真的生效，否则会被叠成 20 次请求
    void noAutoRetryOptionDisablesRetry()
    {
        const QUrl entrance("https://bmclapi2.bangbang93.com/maven/z.jar");
        ScriptedRequest request(entrance, { { FakeReply::Mode::Fail, {} } },
                                Net::Request::Option::NoSourceRewrite | Net::Request::Option::NoAutoRetry);

        QSignalSpy failed(&request, &Task::failed);
        request.start();

        QVERIFY(failed.wait(3000));
        QCOMPARE(request.requestedUrls.size(), 1);
    }

    /// 官方源一样要重试：Azure CDN 也会 `Connection closed`（实测弱网下每轮都有几个）
    void retriesOfficialSourceToo()
    {
        const QUrl official("https://libraries.minecraft.net/io/netty/netty-handler/4.2.7.Final/netty-handler-4.2.7.Final.jar");
        ScriptedRequest request(official, { { FakeReply::Mode::Fail, {} }, { FakeReply::Mode::Ok, QByteArray("JAR") } });

        QSignalSpy succeeded(&request, &Task::succeeded);
        request.start();

        QVERIFY(succeeded.wait(8000));
        QCOMPARE(request.requestedUrls.size(), 2);
        QCOMPARE(*request.output(), QByteArray("JAR"));
    }

    /**
     * 60 秒收不到数据被 Qt 掐掉（OperationCanceledError）不能当成"用户中止"。
     *
     * 这条是 06 §30 的病根：原来这种取消被记成 Aborted，既不重试也不给可读的错，
     * 任务直接失败，界面上只剩一串 "Operation canceled"。
     */
    void retriesAfterTransferTimeout()
    {
        const QUrl official("https://libraries.minecraft.net/it/unimi/dsi/fastutil/8.5.18/fastutil-8.5.18.jar");
        ScriptedRequest request(official, { { FakeReply::Mode::Stall, {} }, { FakeReply::Mode::Ok, QByteArray("OK") } });

        QSignalSpy succeeded(&request, &Task::succeeded);
        request.start();

        QVERIFY(succeeded.wait(8000));
        QCOMPARE(request.requestedUrls.size(), 2);
    }

    /// 一直停滞：重试预算用完要给出人话，而不是 "Operation canceled"
    void stallFailureIsReadable()
    {
        const QUrl official("https://libraries.minecraft.net/org/apache/logging/log4j/log4j-api/2.25.2/log4j-api-2.25.2.jar");
        ScriptedRequest request(official, { { FakeReply::Mode::Stall, {} } });

        QSignalSpy failed(&request, &Task::failed);
        request.start();

        QVERIFY(failed.wait(8000));
        QCOMPARE(request.requestedUrls.size(), 1 + Net::Request::MAX_NETWORK_RETRIES);
        const auto reason = failed.at(0).at(0).toString();
        QVERIFY2(reason.contains("网络停滞"), qPrintable(reason));
        QVERIFY2(!reason.contains("Operation canceled"), qPrintable(reason));
    }

    /// 切分必须无缝无重叠地覆盖 [0, total-1]：漏一个字节就是坏文件
    void splitsIntoContiguousRanges()
    {
        for (qint64 total : { qint64(1), qint64(1023), qint64(1024 * 1024), qint64(23965563) }) {
            for (int count : { 1, 2, 3, 8, 32 }) {
                const auto ranges = Net::Request::splitChunks(total, count);
                QVERIFY(!ranges.isEmpty());
                QCOMPARE(ranges.first().first, qint64(0));
                QCOMPARE(ranges.last().second, total - 1);
                for (int i = 0; i < ranges.size(); ++i) {
                    QVERIFY(ranges.at(i).first <= ranges.at(i).second);
                    if (i > 0)
                        QCOMPARE(ranges.at(i).first, ranges.at(i - 1).second + 1);
                }
            }
        }
        // 段数比字节数还多：不能切出空段
        const auto tiny = Net::Request::splitChunks(3, 8);
        QCOMPARE(tiny.size(), 3);
        for (const auto& range : tiny)
            QCOMPARE(range.second - range.first + 1, qint64(1));
        QVERIFY(Net::Request::splitChunks(0, 8).isEmpty());
    }

    /// 分段模式按偏移写：乱序写入后落盘内容必须正确
    void fileSinkWritesAtOffsets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("chunked.bin");

        Net::FileSink sink(path);
        QNetworkRequest request{ QUrl("https://libraries.minecraft.net/x.jar") };
        QVERIFY(sink.init(request).has_value());
        QVERIFY(sink.beginParallel(10).has_value());

        // 故意倒着写，模拟分片乱序到达
        QVERIFY(sink.writeAt(5, QByteArray("FGHIJ")).has_value());
        QVERIFY(sink.writeAt(0, QByteArray("ABCDE")).has_value());

        StubReply reply(200);
        QVERIFY(sink.finalize(reply).has_value());
        QCOMPARE(readAll(path), QByteArray("ABCDEFGHIJ"));
    }

    /// 分段写入没经过校验器，finalize 要按顺序补一遍；校验不过不能落盘
    void parallelDownloadHonoursChecksum()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString good = dir.filePath("good.bin");
        const QString bad = dir.filePath("bad.bin");
        const QByteArray payload("ABCDEFGHIJ");

        for (const auto& [path, expected] :
             { qMakePair(good, QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex())),
               qMakePair(bad, QString("00000000000000000000000000000000")) }) {
            Net::FileSink sink(path);
            auto* checksum = new Net::ChecksumValidator(QCryptographicHash::Md5, expected);
            sink.addValidator(checksum);

            QNetworkRequest request{ QUrl("https://libraries.minecraft.net/y.jar") };
            QVERIFY(sink.init(request).has_value());
            QVERIFY(sink.beginParallel(payload.size()).has_value());
            QVERIFY(sink.writeAt(5, payload.mid(5)).has_value());
            QVERIFY(sink.writeAt(0, payload.left(5)).has_value());

            StubReply reply(200);
            const auto result = sink.finalize(reply);
            if (path == good) {
                QVERIFY(result.has_value());
                QCOMPARE(readAll(path), payload);
            } else {
                QVERIFY(!result.has_value());
                QVERIFY2(!QFile::exists(path), "校验失败时不能把坏文件落到目标位置");
            }
        }
    }

    /// 回落只允许"镜像 → 官方"单向，且只回一次：不能来回绕圈
    void fallBackRuleIsOneWay()
    {
        using Net::Request;
        const QUrl mirror("https://bmclapi2.bangbang93.com/maven/x.jar");
        const QUrl official("https://libraries.minecraft.net/x.jar");

        QVERIFY(Request::shouldFallBackToOfficial(mirror, official, false));
        QVERIFY(!Request::shouldFallBackToOfficial(mirror, official, true));     // 已经回落过
        QVERIFY(!Request::shouldFallBackToOfficial(official, official, false));  // 本来就在官方
        QVERIFY(!Request::shouldFallBackToOfficial(mirror, mirror, false));      // 回落目标还是镜像
        QVERIFY(!Request::shouldFallBackToOfficial(mirror, QUrl(), false));      // 没有可回落目标
    }
};

QTEST_GUILESS_MAIN(RequestRetryTest)

#include "RequestRetry_test.moc"
