// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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

#pragma once

#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <utility>
#include <variant>

#include "EnumWrapper.h"
#include "HeaderProxy.h"
#include "HttpMetaCache.h"
#include "QObjectPtr.h"
#include "Sink.h"
#include "Validator.h"
#include "tasks/Task.h"

class QIODevice;
class QHttpMultiPart;

namespace Net {

enum class HttpMethodValue : std::uint8_t {
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Head,
    Options,
    Connect,
    Trace,
};
struct HttpMethod : EnumWrapper<HttpMethod, HttpMethodValue> {
    static constexpr auto invalid() { return Get; };
    static constexpr auto mapping()
    {
        return std::array{ std::pair{ Get, "GET" },         std::pair{ Post, "POST" },       std::pair{ Put, "PUT" },
                           std::pair{ Patch, "PATCH" },     std::pair{ Delete, "DELETE" },   std::pair{ Head, "HEAD" },
                           std::pair{ Options, "OPTIONS" }, std::pair{ Connect, "CONNECT" }, std::pair{ Trace, "TRACE" } };
    };
    using enum HttpMethodValue;
    using Base = EnumWrapper<HttpMethod, HttpMethodValue>;
    using Base::Base; /* inherit ctor */
};

class Request : public Task {
    Q_OBJECT
   public:
    using Ptr = shared_qobject_ptr<class Request>;
    enum class Option : std::uint8_t {
        NoOptions = 0,
        AcceptLocalFiles = 1,
        MakeEternal = 2,
        AutoRetry = 4,
        AddAPIHeaders = 8,
        /// 不要按「下载源」改写 URL —— 调用方已经自己算好了最终地址（设置页的连通性测试）
        NoSourceRewrite = 16,
        /// 不要做「连接失败 / 网络停滞自动重试」—— 调用方自己管重试节奏（设置页的连通性测试）
        NoAutoRetry = 32,
    };
    Q_DECLARE_FLAGS(Options, Option)

    using DeviceFactory = std::function<std::expected<QIODevice*, QString>()>;
    using MultiPartFactory = std::function<std::expected<QHttpMultiPart*, QString>()>;
    using ByteArrayFactory = std::function<std::expected<QByteArray, QString>()>;
    using PostData = std::variant<std::monostate, QByteArray, DeviceFactory, MultiPartFactory, ByteArrayFactory>;
    using LogCatFunc = const QLoggingCategory& (*)();

    struct Spec {
        HttpMethod method = HttpMethod::Get;
        QUrl url{};
        Request::PostData data;
        Options options = Option::NoOptions;
        QString name{};
    };

   public:
    /// 连接失败 / 网络停滞时的重试上限（总尝试次数 = 1 + MAX_NETWORK_RETRIES）
    static constexpr int MAX_NETWORK_RETRIES = 3;

    /**
     * 这个网络错误值得再试一次吗？
     *
     * 只认连接层面没谈成的错误：BMCLAPI 会 302 到随机节点，个别节点不可达（实测单次
     * 成功率仅 30–50%），换一次 302 就可能落到好节点上；官方 CDN 在弱网下也会
     * `Connection closed`。反过来，只要服务器给出过 4xx/5xx 状态码，就说明对面答了话 ——
     * 那是内容问题，重试不解决。
     *
     * 纯函数、不碰成员：判定表是这段逻辑最容易写错的地方，单测直接钉住。
     */
    static auto isRetryableNetworkError(QNetworkReply::NetworkError error, int httpStatus) -> bool;

    /**
     * 镜像的节点抽完签还是烂签时，要不要给官方源一次机会？
     *
     * 用户选镜像的初衷是"快"，不是"宁可不下载"；而镜像的 302 目标是随机的，
     * 弱网下抽到不可达节点的概率很高（实测这条线路上 8 个节点里 6 个被 TLS 重置）。
     * 判定条件写成纯函数，方便单测钉住"不会来回绕圈"。
     */
    static auto shouldFallBackToOfficial(const QUrl& retryBase, const QUrl& official, bool alreadyFellBack) -> bool;

#if defined(LAUNCHER_APPLICATION)
    static auto makeCached(const QUrl& url, MetaEntryPtr entry, Options options = Option::NoOptions) -> Request::Ptr;
#endif

    /**
     * Creates a request downloading to the returned QByteArray,.
     * The QByteArray will live as long as the Download object.
     */
    static auto makeByteArray(const QUrl& url, Options options = Option::NoOptions) -> std::pair<Request::Ptr, QByteArray*>;
    static auto makeByteArray(const QUrl& url, QByteArray postData, Options options = Option::NoOptions)
        -> std::pair<Request::Ptr, QByteArray*>;
    static auto makeFile(const QUrl& url, const QString& path, Options options = Option::NoOptions) -> Request::Ptr;
    static auto makeCustomRequest(const Spec& spec) -> Request::Ptr;

   public:
    ~Request() override = default;
    void addValidator(Validator* v);
    auto abort() -> bool override;
    auto canAbort() const -> bool override { return true; }

    void setNetwork(QNetworkAccessManager* network) { m_network = network; }
    void addHeaderProxy(std::unique_ptr<Net::HeaderProxy> proxy) { m_headerProxies.push_back(std::move(proxy)); }
    void setSink(std::unique_ptr<Sink> sink) { m_sink = std::move(sink); }
    void setLogCat(LogCatFunc logCat) { m_logCat = logCat; }

    // automatically handle HTTP 429 Too Many Requests errors and retry
    void enableAutoRetry(bool enable);

    QUrl url() const;
    void setUrl(QUrl url) { m_url = std::move(url); }
    int replyStatusCode() const;
    QNetworkReply::NetworkError error() const;
    QString errorString() const;

   private:
    auto handleRedirect() -> bool;
    void handleAutoRetry(int64_t delay);
    /**
     * 连接失败 / 网络停滞时回到**本轮请求的起点**再试一次。
     *
     * 对镜像来说起点就是入口（302 之前的地址）：接着打刚失败的那个节点等于确定性失败，
     * 回入口才会重新挑节点。对官方源来说起点就是原地址，换一次 DNS/边缘 IP 往往就好了。
     *
     * 返回 true 表示已经排好重试（调用方不要再走失败分支）。重试前会 abort sink，
     * 把上一轮写了半截的数据丢掉 —— 安全性见 FileSink::init() 的注释与单测。
     */
    auto tryNetworkRetry(QNetworkReply::NetworkError error) -> bool;
    virtual QNetworkReply* getReply(QNetworkRequest&);

   protected slots:
    void onProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadError(QNetworkReply::NetworkError error);
    void sslErrors(const QList<QSslError>& errors);
    void downloadFinished();
    void downloadReadyRead();
    void executeTask() override;

   protected:
    // ToDo: Remove this constructor and use the Spec constructor instead. This is a temporary workaround to avoid breaking existing code.
    explicit Request();
    explicit Request(const QUrl& url, Options options = Option::NoOptions, const QString& name = QString());
    Request(const QUrl& url, QByteArray postData, Options options);
    explicit Request(const Spec& spec);

    std::unique_ptr<Sink> m_sink;
    Options m_options;

    LogCatFunc m_logCat = nullptr;

    std::chrono::time_point<std::chrono::steady_clock> m_lastProgressTime;
    qint64 m_lastProgressBytes = 0;

    QNetworkAccessManager* m_network = nullptr;

    /// the network reply
    std::unique_ptr<QNetworkReply> m_reply;
    QByteArray m_errorResponse;

    /// source URL
    QUrl m_url;
    std::vector<std::unique_ptr<Net::HeaderProxy>> m_headerProxies;

    int m_retryCount = 0;
    QTimer m_retryTimer;

    /// 本轮请求的起点（302 之前的地址）。重试必须回到这里，镜像才会重新挑节点
    QUrl m_retryBaseUrl;
    /// 改写之前用户真正想要的地址。镜像彻底不可用时用它回落（见 tryNetworkRetry）
    QUrl m_officialUrl;
    bool m_fellBackToOfficial = false;
    int m_networkRetryCount = 0;
    QTimer m_networkRetryTimer;
    /// 本轮是不是被 Qt 的传输超时掐掉的（`RequestTimeout` 到期 → OperationCanceledError）。
    /// 这不是用户取消，必须和"用户点了中止"区分开，否则慢盘上的大文件永远装不完。
    bool m_stalled = false;

    int m_redirectCount = 0;

    HttpMethod m_httpMethod = HttpMethod::Get;
    PostData m_postData;
};

}  // namespace Net

Q_DECLARE_OPERATORS_FOR_FLAGS(Net::Request::Options)
