// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
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

#include "Request.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QIODevice>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

#ifdef LAUNCHER_APPLICATION
#include "Application.h"
#include "net/ApiHeaderProxy.h"
#include "net/ChecksumValidator.h"
#include "net/DownloadSource.h"
#include "net/MetaCacheSink.h"
#include "settings/SettingsObject.h"
#else
#include "BuildConfig.h"
#endif
#include "net/ByteArraySink.h"
#include "net/FileSink.h"
#include "net/Logging.h"
#include "tasks/Task.h"

#include "MMCTime.h"
#include "StringUtils.h"

namespace Net {

namespace {
auto logCatForMethod(HttpMethod method) -> Request::LogCatFunc
{
    switch (method.value()) {
        case HttpMethod::Get:
            return taskDownloadLogC;
        case HttpMethod::Post:
            return taskUploadLogC;
        default:
            break;
    }
    return taskNetLogC;
}
}  // namespace

Request::Request() : Request(Spec{}) {}

Request::Request(const QUrl& url, Options options, const QString& name)
    : Request(Spec{ .method = HttpMethod::Get, .url = url, .data = std::monostate{}, .options = options, .name = name })
{}

Request::Request(const QUrl& url, QByteArray postData, Options options)
    : Request(Spec{ .method = HttpMethod::Post, .url = url, .data = std::move(postData), .options = options })
{}

Request::Request(const Spec& spec) : m_options(spec.options), m_url(spec.url), m_httpMethod(spec.method), m_postData(spec.data)
{
    connect(&m_retryTimer, &QTimer::timeout, this, &Request::executeTask);
    connect(&m_networkRetryTimer, &QTimer::timeout, this, &Request::executeTask);

    if (spec.name.isEmpty()) {
        setObjectName(QString("BYTES:") + m_url.toString());
    } else {
        setObjectName(spec.name);
    }
    m_logCat = logCatForMethod(m_httpMethod);
#ifdef LAUNCHER_APPLICATION
    if (spec.options.testFlag(Option::AddAPIHeaders)) {
        addHeaderProxy(std::make_unique<ApiHeaderProxy>());
    }
#endif
}

void Request::addValidator(Validator* v)
{
    m_sink->addValidator(v);
}

void Request::executeTask()
{
    // 记下"用户真正想要的地址"，供镜像彻底不可用时回落官方源使用。
    // 只在第一轮记：重定向之后 m_url 已经是节点地址，不能拿它当回落目标。
    if (m_officialUrl.isEmpty())
        m_officialUrl = m_url;

    // 下载源改写：设置里选了镜像时，把官方域换成镜像前缀（映射表见 Net::DownloadSource）。
    // 做在这里而不是构造函数里，是因为重定向也会回到 executeTask —— 镜像自己返回 302 时，
    // 地址已经不在映射表里，rewrite 对它是空操作，不会二次改写。
    if (!m_options.testFlag(Option::NoSourceRewrite))
        m_url = Net::DownloadSource::rewrite(m_url);

    // 只在第一次记下本轮起点。302 之后的节点地址不在映射表里，重试要回到起点，
    // 镜像才会重新挑一个节点 —— 否则重试就是在砸同一个坏节点。
    // 放在 rewrite 之外：连通性测试自己算好的镜像地址同样算"镜像请求"。
    if (m_retryBaseUrl.isEmpty())
        m_retryBaseUrl = m_url;

    m_stalled = false;

    setStatus(tr("Requesting %1").arg(StringUtils::truncateUrlHumanFriendly(m_url, 80)));

    if (Net::DownloadSource::isMirror(m_url)) {
        // 镜像走直连：启动器的代理设置对 BMCLAPI 的 302 节点通常是帮倒忙
        m_network = Net::DownloadSource::directNetwork();
    } else if (m_network == nullptr) {
#ifdef LAUNCHER_APPLICATION
        // 用 dynamic_cast 而不是 APPLICATION（static_cast）：单测里 qApp 是个普通
        // QCoreApplication，静态转换出来的指针不是 Application，取 settings() 会炸。
        if (auto* app = APPLICATION_DYN) {
            m_network = app->network();
        } else {
            qCCritical(m_logCat) << getUid().toString() << "No network manager set for request:" << m_url.toString();
            emitFailed("No network manager set for request");
            return;
        }
#else
        qCCritical(m_logCat) << getUid().toString() << "No network manager set for request:" << m_url.toString();
        emitFailed("No network manager set for request");
        return;
#endif
    }
    if (getState() == Task::State::AbortedByUser) {
        qCWarning(m_logCat) << getUid().toString() << "Attempt to start an aborted Request:" << m_url.toString();
        emitAborted();
        return;
    }

    QNetworkRequest request(m_url);

    // 文件下载走 HTTP/1.1，不用 HTTP/2。
    //
    // 原因不是"h2 慢"（实测大文件 6 并发：h2 1.86 MB/s / h1.1 1.86 MB/s，没差别），
    // 而是**一条 h2 连接上的流会互相饿死**：Qt 6.8 + Azure CDN，6 个并发下载共用一个连接时，
    // 出现过"同一条连接上一条流 75 秒下完、另一条整整 60 秒零字节"（09-日志见 06 §30），
    // 零字节那条被传输超时掐掉，任务就整体失败。
    // h1.1 下每个文件一条独立连接，一个连接被掐不会连累其他文件。
    // 只对"写文件"的请求生效：JSON / API 请求继续用 h2，多路复用对它们是赚的。
    if (m_sink && dynamic_cast<FileSink*>(m_sink.get()) != nullptr)
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    auto result = m_sink->init(request);
    if (!result) {
        emitFailed(result.error());
        return;
    }
    switch (*result) {
        case Sink::InitType::Ok:
            qCDebug(m_logCat) << getUid().toString() << "Running" << m_url.toString();
            break;
        case Sink::InitType::CacheHit:
            qCDebug(m_logCat) << getUid().toString() << "Request cache hit" << m_url.toString();
            emitSucceeded();
            return;
    }

    // 同上：没有 Application（单测）时不要崩，退化成不加 UA / 不设超时。
    QString userAgent;
#ifdef LAUNCHER_APPLICATION
    if (auto* app = APPLICATION_DYN)
        userAgent = app->getUserAgent();
#else
    userAgent = BuildConfig.USER_AGENT;
#endif
    if (!userAgent.isEmpty())
        request.setHeader(QNetworkRequest::UserAgentHeader, userAgent.toUtf8());
    for (auto& headerProxy : m_headerProxies) {
        headerProxy->writeHeaders(request);
    }

#ifdef LAUNCHER_APPLICATION
    if (auto* app = APPLICATION_DYN)
        request.setTransferTimeout(app->settings()->get("RequestTimeout").toInt() * 1000);
    else
        request.setTransferTimeout();
#else
    request.setTransferTimeout();
#endif

    m_lastProgressTime = std::chrono::steady_clock::now();
    m_lastProgressBytes = 0;

    auto* rep = getReply(request);
    if (rep == nullptr) {  // it failed
        return;
    }
    m_reply.reset(rep);
    connect(rep, &QNetworkReply::uploadProgress, this, &Request::onProgress);
    connect(rep, &QNetworkReply::downloadProgress, this, &Request::onProgress);
    connect(rep, &QNetworkReply::finished, this, &Request::downloadFinished);
    connect(rep, &QNetworkReply::errorOccurred, this, &Request::downloadError);
    connect(rep, &QNetworkReply::sslErrors, this, &Request::sslErrors);
    connect(rep, &QNetworkReply::readyRead, this, &Request::downloadReadyRead);
}

void Request::onProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastProgressTime;

    // use milliseconds for speed precision
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    auto bytesReceivedSince = bytesReceived - m_lastProgressBytes;
    auto dlSpeedBps = static_cast<double>(bytesReceivedSince) / static_cast<double>(elapsedMs.count()) * 1000;
    auto remainingTimeS = static_cast<double>(bytesTotal - bytesReceived) / dlSpeedBps;

    //: Current amount of bytes downloaded, out of the total amount of bytes in the download
    QString dlProgress = tr("%1 / %2")
                             .arg(StringUtils::humanReadableFileSize(static_cast<double>(bytesReceived)))
                             .arg(StringUtils::humanReadableFileSize(static_cast<double>(bytesTotal)));

    QString dlSpeedStr;
    if (elapsedMs.count() > 0) {
        auto strEta = bytesTotal > 0 ? Time::humanReadableDuration(remainingTimeS) : tr("unknown");
        //: Download speed, in bytes per second (remaining download time in parenthesis)
        dlSpeedStr = tr("%1 /s (%2)").arg(StringUtils::humanReadableFileSize(dlSpeedBps)).arg(strEta);
    } else {
        //: Download speed at 0 bytes per second
        dlSpeedStr = tr("0 B/s");
    }

    setDetails(dlProgress + "\n" + dlSpeedStr);

    setProgress(bytesReceived, bytesTotal);
}

void Request::downloadError(QNetworkReply::NetworkError error)
{
    if (error == QNetworkReply::OperationCanceledError) {
        // 用户主动取消走的是 Request::abort()（先断开 errorOccurred 再 abort，进不到这里）。
        // 能走到这里的取消只有一个来源：Qt 的传输超时（Request.setTransferTimeout，
        // 取值是设置里的 RequestTimeout）到期，Qt 内部把 reply abort 掉。
        // 这是"网络停滞"，不是"用户不要了" —— 必须当成可重试的网络故障，
        // 否则弱网/被限速时每个大文件都会在 60 秒整点被杀，任务整体失败（见 06 §30）。
        m_stalled = true;
        qCWarning(m_logCat) << getUid().toString() << "Network stalled (no data for the transfer timeout):" << m_url.toString();
        m_state = State::Failed;
    } else if (replyStatusCode() == 429 /* HTTP Too Many Requests*/ && m_options.testFlag(Option::AutoRetry)) {
        qCDebug(m_logCat) << getUid().toString() << "Rate Limited!";
        auto delay = static_cast<int64_t>(10 * std::pow(2, m_retryCount));
        if (m_reply->hasRawHeader("Retry-After")) {
            auto retryAfter = m_reply->rawHeader("Retry-After");
            if (retryAfter.trimmed().endsWith("GMT")) /* HTTP Date format */ {
                auto afterTimestamp = QDateTime::fromString(QString::fromUtf8(retryAfter.trimmed()), "ddd, dd MMM yyyy HH:mm:ss 'GMT'");
                if (afterTimestamp.isValid()) {
                    auto seconds = QDateTime::currentDateTime().secsTo(afterTimestamp);
                    if (seconds > 0) {
                        delay = seconds;
                    }
                }
            } else {
                bool ok = false;
                auto seconds = retryAfter.toLongLong(&ok);
                if (ok && seconds > 0) {
                    delay = seconds;
                }
            }
        }
        handleAutoRetry(delay);
    } else {
        if (m_options.testFlag(Option::AcceptLocalFiles)) {
            if (m_sink->hasLocalData()) {
                m_state = State::Succeeded;
                return;
            }
        }
        // error happened during download.
        qCCritical(m_logCat) << getUid().toString() << "Failed" << m_url.toString() << "with error" << error;
        if (m_reply) {
            qCCritical(m_logCat) << getUid().toString() << "HTTP status:" << replyStatusCode() << errorString();
        }
        if (m_errorResponse.size() > 0) {
            qCCritical(m_logCat) << getUid().toString() << "Response from server:" << m_errorResponse;
        }
        m_state = State::Failed;
    }
}

void Request::sslErrors(const QList<QSslError>& errors)
{
    int i = 1;
    for (const auto& error : errors) {
        qCCritical(m_logCat).nospace() << getUid().toString() << " Request " << m_url.toString() << " SSL Error #" << i << ": "
                                       << error.errorString();
        auto cert = error.certificate();
        qCCritical(m_logCat) << getUid().toString() << "Certificate in question:\n" << cert.toText();
        i++;
    }
}

auto Request::handleRedirect() -> bool
{
    QUrl redirect = m_reply->header(QNetworkRequest::LocationHeader).toUrl();
    if (!redirect.isValid()) {
        if (!m_reply->hasRawHeader("Location")) {
            // no redirect -> it's fine to continue
            return false;
        }
        // there is a Location header, but it's not correct. we need to apply some workarounds...
        QByteArray redirectBA = m_reply->rawHeader("Location");
        if (redirectBA.size() == 0) {
            // empty, yet present redirect header? WTF?
            return false;
        }
        QString redirectStr = QString::fromUtf8(redirectBA);

        if (redirectStr.startsWith("//")) {
            /*
             * IF the URL begins with //, we need to insert the URL scheme.
             * See: https://bugreports.qt.io/browse/QTBUG-41061
             * See: http://tools.ietf.org/html/rfc3986#section-4.2
             */
            redirectStr = m_reply->url().scheme() + ":" + redirectStr;
        } else if (redirectStr.startsWith("/")) {
            /*
             * IF the URL begins with /, we need to process it as a relative URL
             */
            auto url = m_reply->url();
            url.setPath(redirectStr, QUrl::TolerantMode);
            redirectStr = url.toString();
        }

        /*
         * Next, make sure the URL is parsed in tolerant mode. Qt doesn't parse the location header in tolerant mode, which causes
         * issues.
         * FIXME: report Qt bug for this
         */
        redirect = QUrl(redirectStr, QUrl::TolerantMode);
        if (!redirect.isValid()) {
            qCWarning(m_logCat) << getUid().toString() << "Failed to parse redirect URL:" << redirectStr;
            downloadError(QNetworkReply::ProtocolFailure);
            return false;
        }
        qCDebug(m_logCat) << getUid().toString() << "Fixed location header:" << redirect;
    } else {
        qCDebug(m_logCat) << getUid().toString() << "Location header:" << redirect;
    }

    m_url = QUrl(redirect.toString());
    qCDebug(m_logCat) << getUid().toString() << "Following redirect to" << m_url.toString();
    if (++m_redirectCount > 10) {
        qCWarning(m_logCat) << getUid().toString() << "Too many redirects for" << m_url.toString();
        m_sink->abort();
        emitFailed(tr("Too many redirects"));
        return true;
    }
    executeTask();

    return true;
}

void Request::handleAutoRetry(int64_t delay)
{
    m_retryCount++;
    if (delay > 60 || m_retryCount > 4) {
        /* 1 minute is too long to wait for retry, fail for now */
        m_state = State::Failed;
        auto retryAfter = QDateTime::currentDateTime().addSecs(delay);
        emitFailed(tr("Request Rate Limited for %n second(s): Retry After %1", "seconds", static_cast<int>(delay))
                       .arg(retryAfter.toLocalTime().toString(QLocale::system().dateTimeFormat(QLocale::ShortFormat))));
        return;
    }
    qCDebug(m_logCat) << getUid().toString() << "Retyring Request in" << delay << "seconds";
    setStatus(tr("Rate Limited: Waiting %n second(s)", "seconds", static_cast<int>(delay)));
    m_retryTimer.setTimerType(Qt::VeryCoarseTimer);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(static_cast<int>(delay) * 1000);
    m_retryTimer.start();
}

auto Request::isRetryableNetworkError(QNetworkReply::NetworkError error, int httpStatus) -> bool
{
    // 对面给过 4xx/5xx：服务器已经答话了，换节点不会变好（404 换了还是 404）
    if (httpStatus >= 400)
        return false;

    switch (error) {
        // BMCLAPI 302 之后那一段最常见的三种：节点握手被重置、节点域名解析不了、连不上
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
            return true;
        default:
            return false;
    }
}

auto Request::shouldFallBackToOfficial(const QUrl& retryBase, const QUrl& official, bool alreadyFellBack) -> bool
{
    if (alreadyFellBack || retryBase.isEmpty() || official.isEmpty())
        return false;
    // 只有"现在打的是镜像、用户本来要的是官方"才回落，方向反过来不成立：
    // 官方失败不该自动切到镜像（那条路上文件更可能根本下不下来）。
    return Net::DownloadSource::isMirror(retryBase) && !Net::DownloadSource::isMirror(official);
}

auto Request::tryNetworkRetry(QNetworkReply::NetworkError error) -> bool
{
    // 只重试幂等的 GET：重发 POST 可能造成重复副作用。
    if (m_httpMethod != HttpMethod::Get)
        return false;
    if (m_retryBaseUrl.isEmpty())
        return false;
    // 调用方自己管重试节奏的不插手（Option::NoAutoRetry，目前只有连通性测试）
    if (m_options.testFlag(Option::NoAutoRetry))
        return false;

    const bool retryable = m_stalled || isRetryableNetworkError(error, replyStatusCode());
    if (!retryable)
        return false;

    const bool budgetLeft = m_networkRetryCount < MAX_NETWORK_RETRIES;
    const bool canFallBack = !budgetLeft && shouldFallBackToOfficial(m_retryBaseUrl, m_officialUrl, m_fellBackToOfficial);

    if (!budgetLeft && !canFallBack)
        return false;

    m_networkRetryCount++;

    if (!budgetLeft && canFallBack) {
        m_fellBackToOfficial = true;
        m_retryBaseUrl = m_officialUrl;
        qCWarning(m_logCat) << getUid().toString() << "Mirror gave up after" << MAX_NETWORK_RETRIES
                            << "attempts - falling back to the official source:" << m_retryBaseUrl.toString();
    } else {
        qCWarning(m_logCat) << getUid().toString() << "Request failed:" << m_url.toString() << "(" << errorString() << ")"
                            << (m_stalled ? "- network stalled, retrying" : "- retrying") << "from" << m_retryBaseUrl.toString()
                            << QString("(%1/%2)").arg(m_networkRetryCount).arg(MAX_NETWORK_RETRIES);
    }

    // 上一轮可能写了半截文件。abort() 会 cancelWriting() 掉临时文件、作废校验器；
    // 下一轮 init() 重新建 QSaveFile（先写临时文件，commit 才落盘），所以是截断重写，
    // 既不会 append 出坏文件，也不会碰目标位置上原有的好文件。
    m_sink->abort();

    m_state = State::Running;
    m_url = m_retryBaseUrl;
    m_redirectCount = 0;
    m_errorResponse.clear();
    m_stalled = false;
    setProgress(0, 0);
    if (m_fellBackToOfficial)
        setStatus(tr("下载源不可用，已改用官方源重试"));
    else if (m_stalled)
        setStatus(tr("网络停滞，正在重试（%1/%2）").arg(m_networkRetryCount).arg(MAX_NETWORK_RETRIES));
    else
        setStatus(tr("连接中断，正在重试（%1/%2）").arg(m_networkRetryCount).arg(MAX_NETWORK_RETRIES));

    // 用定时器而不是立刻 executeTask()：现在还在 QNetworkReply::finished 的槽里，
    // 立刻重置 m_reply 等于在信号派发过程中析构发信号的对象。
    m_networkRetryTimer.setTimerType(Qt::CoarseTimer);
    m_networkRetryTimer.setSingleShot(true);
    m_networkRetryTimer.start(400 * m_networkRetryCount);
    return true;
}

void Request::downloadFinished()
{
    // currently waiting for retry
    if (m_retryTimer.isActive()) {
        return;
    }

    // handle HTTP redirection first
    if (handleRedirect()) {
        qCDebug(m_logCat) << getUid().toString() << "Request redirected:" << m_url.toString();
        return;
    }

    // if the download failed before this point ...
    if (m_state == State::Succeeded)  // pretend to succeed so we continue processing :)
    {
        qCDebug(m_logCat) << getUid().toString() << "Request failed but we are allowed to proceed:" << m_url.toString();
        m_sink->abort();
        emit succeeded();
        emit finished();
        return;
    }
    if (m_state == State::Failed) {
        if (tryNetworkRetry(m_reply->error()))
            return;
        qCDebug(m_logCat) << getUid().toString() << "Request failed in previous step:" << m_url.toString();
        m_sink->abort();
        // 给用户一句能行动的话：Qt 把传输超时报成 "Operation canceled"，
        // 和"用户点了中止"长得一模一样，直接抛出去没人看得懂（06 §30）。
        if (m_stalled) {
            m_failReason = tr("网络停滞：长时间收不到数据（已重试 %1 次）").arg(m_networkRetryCount);
        } else {
            m_failReason = m_reply->errorString();
        }
        emit failed(m_failReason);
        emit finished();
        return;
    }
    if (m_state == State::AbortedByUser) {
        qCDebug(m_logCat) << getUid().toString() << "Request aborted in previous step:" << m_url.toString();
        m_sink->abort();
        emit aborted();
        emit finished();
        return;
    }

    // make sure we got all the remaining data, if any
    auto data = m_reply->readAll();
    if (!data.isEmpty()) {
        qCDebug(m_logCat) << getUid().toString() << "Writing extra" << data.size() << "bytes";
        auto result = m_sink->write(data);
        if (!result) {
            qCDebug(m_logCat) << getUid().toString() << "Request failed to write:" << m_url.toString();
            m_sink->abort();
            emitFailed(result.error());
            return;
        }
    }

    // otherwise, finalize the whole graph
    auto result = m_sink->finalize(*m_reply);
    if (!result) {
        qCDebug(m_logCat) << getUid().toString() << "Request failed to finalize:" << m_url.toString();
        m_sink->abort();
        emitFailed(result.error());
        return;
    }

    qCDebug(m_logCat) << getUid().toString() << "Request succeeded:" << m_url.toString();
    emitSucceeded();
}

void Request::downloadReadyRead()
{
    if (m_state == State::Running) {
        auto data = m_reply->readAll();
        auto result = m_sink->write(data);
        if (replyStatusCode() >= 400) {
            m_errorResponse.append(data);
        }
        if (!result) {
            m_state = Task::State::Failed;
            m_failReason = result.error();
            qCCritical(m_logCat) << getUid().toString() << "Failed to process response chunk:" << m_failReason;
        }
        // qDebug() << "Request" << m_url.toString() << "gained" << data.size() << "bytes";
    } else {
        qCCritical(m_logCat) << getUid().toString() << "Cannot write download data! illegal status" << m_status;
    }
}

auto Request::abort() -> bool
{
    m_retryTimer.stop();
    m_networkRetryTimer.stop();
    m_state = State::AbortedByUser;
    if (m_reply && !m_reply->isFinished()) {
        disconnect(m_reply.get(), &QNetworkReply::errorOccurred, nullptr, nullptr);
        m_reply->abort();
    } else {
        emit aborted();
        emit finished();
    }
    return true;
}

int Request::replyStatusCode() const
{
    return m_reply ? m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : -1;
}

QNetworkReply::NetworkError Request::error() const
{
    return m_reply ? m_reply->error() : QNetworkReply::NoError;
}

QUrl Request::url() const
{
    return m_url;
}

QString Request::errorString() const
{
    return m_reply ? m_reply->errorString() : "";
}

void Request::enableAutoRetry(bool enable)
{
    if (enable) {
        m_options |= Option::AutoRetry;
    } else {
        m_options &= ~static_cast<std::uint8_t>(Option::AutoRetry);
    }
}

QNetworkReply* Request::getReply(QNetworkRequest& request)
{
    if (m_httpMethod == HttpMethod::Get) {
        Q_ASSERT(std::holds_alternative<std::monostate>(m_postData));
        return m_network->get(request);
    }
    return std::visit(
        [this, &request](const auto& data) -> QNetworkReply* {
            using T = std::remove_cvref_t<decltype(data)>;
            const auto verb = m_httpMethod.toString().toUtf8();
            if constexpr (std::is_same_v<T, QByteArray>) {
                if (m_httpMethod == HttpMethod::Post && !request.hasRawHeader("Content-Type")) {
                    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                }
                return m_network->sendCustomRequest(request, verb, data);
            } else if constexpr (std::is_same_v<T, ByteArrayFactory>) {
                if (m_httpMethod == HttpMethod::Post && !request.hasRawHeader("Content-Type")) {
                    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                }
                auto body = data();
                if (!body) {
                    emitFailed(body.error());
                    return nullptr;
                }
                return m_network->sendCustomRequest(request, verb, body.value());

            } else if constexpr (std::is_same_v<T, std::monostate>) {
                return m_network->sendCustomRequest(request, verb);
            } else if constexpr (std::is_same_v<T, DeviceFactory>) {
                auto payload = data();
                if (!payload) {
                    emitFailed(payload.error());
                    return nullptr;
                }
                auto device = payload.value();
                if (device != nullptr) {
                    device->setParent(this);
                }
                return m_network->sendCustomRequest(request, verb, device);
            } else if constexpr (std::is_same_v<T, MultiPartFactory>) {
                auto payload = data();
                if (!payload) {
                    emitFailed(payload.error());
                    return nullptr;
                }
                auto multiPart = payload.value();
                if (multiPart != nullptr) {
                    multiPart->setParent(this);
                }
                return m_network->sendCustomRequest(request, verb, multiPart);
            }
        },
        m_postData);
}

#ifdef LAUNCHER_APPLICATION
auto Request::makeCached(const QUrl& url, MetaEntryPtr entry, Options options) -> Ptr
{
    auto dl = Ptr(new Request(url, options, (QString("CACHE:") + url.toString())));
    auto* md5Node = new ChecksumValidator(QCryptographicHash::Md5);
    auto* cachedNode = new MetaCacheSink(std::move(entry), md5Node, options.testFlag(Option::MakeEternal));
    dl->m_sink.reset(cachedNode);
    return dl;
}
#endif

auto Request::makeByteArray(const QUrl& url, QByteArray postData, Options options) -> std::pair<Ptr, QByteArray*>
{
    auto dl = Ptr(new Request(url, std::move(postData), options));

    auto sink = std::make_unique<ByteArraySink>();
    auto* response = sink->output();
    dl->m_sink = std::move(sink);

    return { dl, response };
}

auto Request::makeByteArray(const QUrl& url, Options options) -> std::pair<Ptr, QByteArray*>
{
    auto dl = Ptr(new Request(url, options));

    auto sink = std::make_unique<ByteArraySink>();
    auto* response = sink->output();
    dl->m_sink = std::move(sink);

    return { dl, response };
}

auto Request::makeFile(const QUrl& url, const QString& path, Options options) -> Ptr
{
    auto dl = Ptr(new Request(url, options, QString("FILE:") + url.toString()));
    dl->m_sink = std::make_unique<FileSink>(path);

    return dl;
}

auto Request::makeCustomRequest(const Spec& spec) -> Ptr
{
    return Ptr(new Request(spec));
}

}  // namespace Net
