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
#include "HallAPI.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

#include "Application.h"
#include "settings/SettingsObject.h"
#include "BrowserFetch.h"
#include "HallCache.h"
#include "HallProxy.h"

namespace maps {
namespace {

/// ctmmaps.com 前端包里自带的公开 anon key（受 RLS 约束，只能读公开表）
constexpr const char* ANON_KEY =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InF0eGpidWtqeXV5bHFqZ2lwaHhzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzU2NTYzNzMsImV4cCI6"
    "MjA5MTIzMjM3M30.JBgF0FaS4JkzsKvxAQdC2tQSVhoStIosH5l6vmnkirY";

constexpr const char* DEFAULT_ENDPOINT = "https://qtxjbukjyuylqjgiphxs.supabase.co";

/// 单次请求的超时。站点在部分网络下只握手不回包，没有这个值对话框会一直转圈。
constexpr int REQUEST_TIMEOUT_MS = 15000;

QStringList csv(const QJsonValue& value)
{
    QStringList out;
    if (value.isArray()) {
        for (const auto& item : value.toArray()) {
            const QString text = item.toVariant().toString().trimmed();
            if (!text.isEmpty())
                out.append(text);
        }
        return out;
    }
    if (value.isString()) {
        for (const auto& part : value.toString().split(QLatin1Char(','))) {
            const QString text = part.trimmed();
            if (!text.isEmpty())
                out.append(text);
        }
    }
    return out;
}

double numberOr(const QJsonValue& value, double fallback)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

QString textOr(const QJsonValue& value, const QString& fallback = {})
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isDouble())
        return QString::number(value.toDouble());
    return fallback;
}

}  // namespace

QString hallErrorToString(HallError error)
{
    switch (error) {
        case HallError::None:
            return QStringLiteral("none");
        case HallError::Dns:
            return QStringLiteral("dns");
        case HallError::Tls:
            return QStringLiteral("tls");
        case HallError::Timeout:
            return QStringLiteral("timeout");
        case HallError::Http:
            return QStringLiteral("http");
        case HallError::Empty:
            return QStringLiteral("empty");
        case HallError::Parse:
            break;
    }
    return QStringLiteral("parse");
}

HallAPI::HallAPI(QObject* parent) : QObject(parent), m_endpoint(QString::fromLatin1(DEFAULT_ENDPOINT))
{
    // 大厅走自己的出口：启动器全局代理常常被设成"直连"（meta/下载更快），
    // 而 *.supabase.co 反过来必须走代理 —— 两边需求相反，不能共用一个 NAM。
    m_nam = new QNetworkAccessManager(this);
    applyProxy();

    /**
     * 浏览器通道：不引入代理，也不自己实现 ECH —— 直接借本机的 Edge/Chrome。
     * 详见 BrowserFetch.h 的注释（实测：被封的是 SNI，不是 IP）。
     */
    m_browser = new BrowserFetch(this);
    connect(m_browser, &BrowserFetch::finished, this, [this](bool ok, const QByteArray& body, const QString& error) {
        m_browserBusy = false;
        if (!ok) {
            emit failed(HallError::Blocked, tr("浏览器通道也没拿到数据：%1").arg(error), m_lastUrl.toString());
            return;
        }
        m_browserPreferred = true;
        // 浏览器通道拿不到 Content-Range：评论总数由界面显示为"未知"，不编数字
        handlePayload(body, {});
    });

    // 本地快照：有的话，搜索/详情/评论全部走内存，不再发请求
    applySnapshot(HallCache::load());

    // 传输方式来自设置：auto（默认）/ network / browser
    const QString mode = APPLICATION->settings()->get("MapHallTransport").toString();
    if (mode == QLatin1String("network"))
        m_transport = Transport::Network;
    else if (mode == QLatin1String("browser"))
        m_transport = Transport::Browser;
    else
        m_transport = Transport::Auto;
}

qint64 HallAPI::libraryBytes() const
{
    return HallCache::sizeBytes();
}

void HallAPI::applySnapshot(const HallSnapshot& snapshot)
{
    m_snapshot = snapshot;
    m_detailBySlug.clear();
    m_userById.clear();
    m_reviewRowsByMap.clear();
    m_items.clear();
    if (!m_snapshot.isValid())
        return;

    m_libraryIncomplete = !HallCache::detailsCoverList(m_snapshot);

    for (const auto& value : m_snapshot.listRows) {
        const auto row = value.toObject();
        const HallItem item = itemFromJson(row);
        if (!item.slug.isEmpty())
            m_items.append(item);
    }
    for (const auto& value : m_snapshot.detailRows) {
        const auto row = value.toObject();
        const QString slug = textOr(row.value(QStringLiteral("slug")));
        if (!slug.isEmpty())
            m_detailBySlug.insert(slug, row);
    }
    for (const auto& value : m_snapshot.userRows) {
        const auto row = value.toObject();
        const QString uuid = textOr(row.value(QStringLiteral("user_uuid")));
        if (uuid.isEmpty())
            continue;
        const QString name = textOr(row.value(QStringLiteral("username")),
                                    textOr(row.value(QStringLiteral("name")),
                                           textOr(row.value(QStringLiteral("display_name")))));
        if (!name.isEmpty())
            m_userById.insert(uuid, name);
    }
    for (const auto& value : m_snapshot.reviewRows) {
        const auto row = value.toObject();
        const QString mapUuid = textOr(row.value(QStringLiteral("map_uuid")));
        if (!mapUuid.isEmpty())
            m_reviewRowsByMap[mapUuid].append(row);
    }
}

QList<HallItem> HallAPI::filterLocal(const HallQuery& query) const
{
    const QString text = query.text.trimmed().toLower();
    QList<HallItem> matched;
    matched.reserve(m_items.size());

    for (const auto& item : m_items) {
        if (!query.series.isEmpty() && item.series.compare(query.series, Qt::CaseInsensitive) != 0)
            continue;
        if (!query.difficulties.isEmpty() && !query.difficulties.contains(item.difficulty))
            continue;
        if (!query.lengths.isEmpty() && !query.lengths.contains(item.length))
            continue;
        if (!query.edition.isEmpty() && item.edition.compare(query.edition, Qt::CaseInsensitive) != 0)
            continue;
        if (query.ratingMin > 0 && item.rating < query.ratingMin)
            continue;
        if (query.playtimeMax > 0 && item.minutes > query.playtimeMax)
            continue;
        if (query.year.size() == 4 && !item.releaseDate.startsWith(query.year))
            continue;

        if (!text.isEmpty()) {
            // 服务端查的是 name / series / event，本地再补上作者，命中更多但不会更少
            const QString haystack =
                (item.title + QLatin1Char(' ') + item.series + QLatin1Char(' ') + item.author).toLower();
            if (!haystack.contains(text))
                continue;
        }
        matched.append(item);
    }

    // 排序：null（空串 / -1）一律垫底，和 PostgREST 的 nullslast 对齐
    const auto byString = [](const QString& a, const QString& b) {
        if (a.isEmpty() != b.isEmpty())
            return a.isEmpty() ? -1 : 1;
        return a.compare(b);
    };
    const QString sort = query.sort;
    std::stable_sort(matched.begin(), matched.end(), [&sort](const HallItem& a, const HallItem& b) {
        if (sort == QLatin1String("release_asc"))
            return a.releaseDate < b.releaseDate;
        if (sort == QLatin1String("rating_desc") || sort == QLatin1String("rating_asc"))
            return sort.endsWith(QLatin1String("_desc")) ? a.rating > b.rating : a.rating < b.rating;
        if (sort.startsWith(QLatin1String("popularity")))
            return sort.endsWith(QLatin1String("_desc")) ? a.plays > b.plays : a.plays < b.plays;
        if (sort.startsWith(QLatin1String("playtime")))
            return sort.endsWith(QLatin1String("_desc")) ? a.minutes > b.minutes : a.minutes < b.minutes;
        // release_desc（默认）
        return a.releaseDate > b.releaseDate;
    });
    Q_UNUSED(byString)

    const int limit = std::clamp(query.limit <= 0 ? 20 : query.limit, 1, 200);
    const int offset = std::max(0, query.offset);
    if (offset >= matched.size())
        return {};
    return matched.mid(offset, limit);
}

HallDetail HallAPI::buildDetailFromCache(const QString& slug) const
{
    HallDetail detail;
    for (const auto& item : m_items) {
        if (item.slug == slug) {
            detail.item = item;
            break;
        }
    }
    if (detail.item.slug.isEmpty())
        return detail;

    detail.item.pageUrl = QStringLiteral("https://www.ctmmaps.com/map/%1").arg(slug);
    const auto row = m_detailBySlug.value(slug);

    QStringList description;
    for (const auto* key : { "description_1", "description_2", "description_3" }) {
        const QString part = textOr(row.value(QLatin1String(key)));
        if (!part.isEmpty())
            description.append(part);
    }
    detail.description = description.join(QStringLiteral("\n\n"));
    detail.trailerUrl = textOr(row.value(QStringLiteral("trailer")));

    const QString direct = textOr(row.value(QStringLiteral("download")));
    if (!direct.isEmpty())
        detail.downloadLinks.append(direct);
    const auto links = row.value(QStringLiteral("download_links")).toObject();
    for (auto it = links.constBegin(); it != links.constEnd(); ++it) {
        const QString url = it.value().toString();
        if (!url.isEmpty())
            detail.downloadLinks.append(url);
    }
    if (!detail.downloadLinks.isEmpty())
        detail.item.downloadUrl = detail.downloadLinks.first();
    return detail;
}

void HallAPI::syncLibrary()
{
    m_pending = HallSnapshot{};
    m_syncStep = 0;
    m_syncDetailOffset = 0;
    startSyncStep(0);
}

void HallAPI::startSyncStep(int step)
{
    constexpr int kTotal = 4;
    m_syncStep = step;

    QUrlQuery params;
    QUrl url;

    switch (step) {
        case 0:
            emit syncProgress(1, kTotal, tr("同步地图列表…"));
            m_kind = Request::SyncList;
            params.addQueryItem(QStringLiteral("select"), QStringLiteral("*"));
            params.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
            params.addQueryItem(QStringLiteral("order"), QStringLiteral("sort_date.desc.nullslast"));
            url = QUrl(QStringLiteral("%1/rest/v1/maps_search_view").arg(m_endpoint));
            break;
        case 1:
            emit syncProgress(2, kTotal, tr("同步地图详情…"));
            m_kind = Request::SyncDetails;
            // 只要详情页用得上的列：三列简介 + 下载链接，避免整表拉全字段
            params.addQueryItem(QStringLiteral("select"),
                                QStringLiteral("slug,description_1,description_2,description_3,download,"
                                               "download_links,trailer"));
            /**
             * 按 slug 排序 + offset 分页。踩过的坑：maps 表默认顺序和 maps_search_view
             * 的 sort_date 顺序**不一样**，两边各取前 1000 得到的 slug 集合差很多 ——
             * 结果就是「列表里有、详情没有」，点开一张图只能回落网络（又慢又常失败）。
             */
            params.addQueryItem(QStringLiteral("order"), QStringLiteral("slug.asc"));
            params.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
            if (m_syncDetailOffset > 0)
                params.addQueryItem(QStringLiteral("offset"), QString::number(m_syncDetailOffset));
            url = QUrl(QStringLiteral("%1/rest/v1/maps").arg(m_endpoint));
            break;
        case 2:
            emit syncProgress(3, kTotal, tr("同步玩家名字…"));
            m_kind = Request::SyncUsers;
            params.addQueryItem(QStringLiteral("select"), QStringLiteral("user_uuid,username"));
            params.addQueryItem(QStringLiteral("limit"), QStringLiteral("3000"));
            url = QUrl(QStringLiteral("%1/rest/v1/users").arg(m_endpoint));
            break;
        case 3:
            emit syncProgress(4, kTotal, tr("同步评论…"));
            m_kind = Request::SyncReviews;
            params.addQueryItem(QStringLiteral("select"),
                                QStringLiteral("ledger_uuid,map_uuid,user_uuid,rating,review,played_date,"
                                               "playtime_total_minutes"));
            params.addQueryItem(QStringLiteral("review"), QStringLiteral("not.is.null"));
            params.addQueryItem(QStringLiteral("order"), QStringLiteral("played_date.desc"));
            params.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
            url = QUrl(QStringLiteral("%1/rest/v1/user_maps").arg(m_endpoint));
            break;
        default:
            m_pending.fetchedAt = QDateTime::currentDateTimeUtc();
            if (m_pending.listRows.isEmpty()) {
                finishSync(false, tr("同步到的地图列表是空的。"));
                return;
            }
            {
                QString error;
                const bool ok = HallCache::save(m_pending, &error);
                applySnapshot(m_pending);
                finishSync(ok, ok ? tr("已更新：%1 张地图、%2 条评论（缓存 %3 MB）")
                                        .arg(m_items.size())
                                        .arg(m_reviewRowsByMap.size())
                                        .arg(HallCache::sizeBytes() / 1024.0 / 1024.0, 0, 'f', 1)
                                  : tr("数据已取到，但写入缓存失败：%1").arg(error));
            }
            return;
    }

    url.setQuery(params);
    get(url);
}

void HallAPI::finishSync(bool ok, const QString& message)
{
    m_pending = HallSnapshot{};
    m_syncStep = 0;
    emit syncDone(ok, message);
}

void HallAPI::clearLibrary()
{
    HallCache::clear();
    applySnapshot(HallSnapshot{});
    emit syncDone(true, tr("本地缓存已清除。"));
}

void HallAPI::setTransport(Transport mode)
{
    m_transport = mode;
    if (mode == Transport::Network)
        m_browserPreferred = false;
}

QString HallAPI::transportNote() const
{
    if (m_transport == Transport::Browser || m_browserPreferred)
        return tr("浏览器通道");
    return proxyNote();
}

void HallAPI::applyProxy()
{
    const QString host = QUrl(m_endpoint).host();
    const auto info = resolveHallProxy(host);
    m_nam->setProxy(info.proxy);
    m_proxyNote = info.note;
    m_proxyActive = !info.url.isEmpty();
}

QString HallAPI::proxyNote() const
{
    if (!m_fallbackNote.isEmpty())
        return QStringLiteral("%1（%2）").arg(m_proxyNote, m_fallbackNote);
    return m_proxyNote;
}

void HallAPI::invalidateProxyCache()
{
    maps::invalidateHallProxyCache();
}

HallAPI::~HallAPI()
{
    abort();
}

void HallAPI::setEndpoint(const QString& base)
{
    const QString trimmed = base.trimmed();
    m_endpoint = trimmed.isEmpty() ? QString::fromLatin1(DEFAULT_ENDPOINT) : trimmed;
    while (m_endpoint.endsWith(QLatin1Char('/')))
        m_endpoint.chop(1);
}

QString HallAPI::endpoint() const
{
    return m_endpoint;
}

QString HallAPI::inList(const QStringList& values)
{
    QStringList cleaned;
    for (auto value : values) {
        value.remove(QLatin1Char('('));
        value.remove(QLatin1Char(')'));
        value.remove(QLatin1Char(','));
        cleaned.append(value);
    }
    return QStringLiteral("in.(%1)").arg(cleaned.join(QLatin1Char(',')));
}

void HallAPI::abort()
{
    if (m_browser != nullptr && m_browser->isRunning())
        m_browser->abort();
    m_browserBusy = false;
    if (m_reply != nullptr) {
        auto* reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

void HallAPI::probe()
{
    const auto info = resolveHallProxy(QUrl(m_endpoint).host());
    sendProbe(!info.url.isEmpty(), QDateTime::currentMSecsSinceEpoch());
}

void HallAPI::sendProbe(bool useProxy, qint64 startedAt)
{
    // 用真查询而不是 /rest/v1/ 根路径：能同时证明"连得上"和"anon key + RLS 还能读"
    QNetworkRequest request(
        QUrl(m_endpoint + QStringLiteral("/rest/v1/maps_search_view?select=name&limit=1")));
    request.setRawHeader("apikey", QByteArray(ANON_KEY));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray(ANON_KEY));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(8000);

    QNetworkAccessManager* nam = m_nam;
    if (!useProxy) {
        if (m_directNam == nullptr) {
            m_directNam = new QNetworkAccessManager(this);
            m_directNam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        nam = m_directNam;
    }

    auto* reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, useProxy, startedAt] {
        const int elapsed = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - startedAt);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;

        // 代理这条路不通就换直连再试：诊断要回答"到底哪条通"，不是"第一条通不通"
        if (!ok && useProxy) {
            reply->deleteLater();
            sendProbe(false, startedAt);
            return;
        }

        QString detail;
        if (ok)
            detail = QStringLiteral("HTTP %1").arg(status > 0 ? QString::number(status) : QStringLiteral("—"));
        else
            detail = reply->errorString() + (status > 0 ? QStringLiteral("（HTTP %1）").arg(status) : QString());

        const auto info = resolveHallProxy(QUrl(m_endpoint).host());
        detail += QStringLiteral(" · 出口：%1").arg(info.url.isEmpty() ? QStringLiteral("直连") : info.url);
        if (useProxy)
            detail += QStringLiteral("（代理不通，换直连也不行）");

        reply->deleteLater();
        emit probeDone(ok, elapsed, detail);
    });
}

QNetworkReply* HallAPI::get(const QUrl& url)
{
    abort();
    m_fallbackNote.clear();
    m_lastUrl = url;

    // 每次请求前重读一次通道设置：设置在对话框里改完立刻生效，不用重开大厅页
    const QString mode = APPLICATION->settings()->get("MapHallTransport").toString();
    if (mode == QLatin1String("network"))
        m_transport = Transport::Network;
    else if (mode == QLatin1String("browser"))
        m_transport = Transport::Browser;
    else if (m_transport == Transport::Network || m_transport == Transport::Browser)
        m_transport = Transport::Auto;  // 从"只用 X"切回自动时清掉之前的选择

    if (m_transport == Transport::Browser || m_browserPreferred) {
        sendViaBrowser(url);
        return nullptr;
    }
    return send(url, true);
}

void HallAPI::sendViaBrowser(const QUrl& url)
{
    if (m_browser == nullptr || !BrowserFetch::available()) {
        emit failed(HallError::Blocked, tr("这张网络下只能用浏览器通道，但本机没找到 Edge / Chrome。"), {});
        return;
    }

    // 浏览器只能发"简单 GET"：apikey 改走查询参数（Kong 认这个写法，实测可过鉴权）
    QUrl target(url);
    QUrlQuery query(target);
    if (!query.hasQueryItem(QStringLiteral("apikey")))
        query.addQueryItem(QStringLiteral("apikey"), QString::fromLatin1(ANON_KEY));
    target.setQuery(query);

    m_browserBusy = true;
    m_browser->get(target, 4);
}

QNetworkReply* HallAPI::send(const QUrl& url, bool allowProxy)
{
    QNetworkRequest request(url);
    request.setRawHeader("apikey", QByteArray(ANON_KEY));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray(ANON_KEY));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    // 走代理失败时直连再试一次（AIO 的 fetchWithFallback）：
    // 代理挂掉 / 换规则 / 对某个域名特别慢时，裸连往往还是通的；
    // 只在失败分支付出代价，正常路径零开销。
    QNetworkAccessManager* nam = m_nam;
    if (!allowProxy || !m_proxyActive) {
        if (m_directNam == nullptr) {
            m_directNam = new QNetworkAccessManager(this);
            m_directNam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        nam = m_directNam;
    }

    auto* reply = nam->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, url, allowProxy, reply] {
        // 已经被新请求顶掉 / 被 abort() 取消：直接丢掉，别拿取消当"失败"去重试
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        // 只对"连 HTTP 响应都没拿到"的错误兜底：4xx/5xx 说明路是通的，重试没意义
        const bool noResponse = reply->error() != QNetworkReply::NoError &&
                                reply->error() != QNetworkReply::OperationCanceledError &&
                                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0;
        if (allowProxy && m_proxyActive && noResponse) {
            reply->deleteLater();
            m_fallbackNote = tr("经代理失败，已改直连重试");
            send(url, false);
            return;
        }
        finish(reply);
    });
    return reply;
}

HallError HallAPI::classify(QNetworkReply* reply, QString* humanReason)
{
    const auto error = reply->error();
    if (error == QNetworkReply::NoError) {
        return HallError::None;
    }

    // 说清楚是"哪个主机"出了问题：同一句话在换了反代地址之后还成立，才叫有用
    const QString host = reply->url().host();

    switch (error) {
        case QNetworkReply::HostNotFoundError:
            if (humanReason != nullptr)
                *humanReason = QStringLiteral("%1 解析不出来（DNS 失败），请检查网络、DNS 或代理设置。").arg(host);
            return HallError::Dns;
        case QNetworkReply::SslHandshakeFailedError:
        case QNetworkReply::ProtocolFailure:
            if (humanReason != nullptr)
                *humanReason = QStringLiteral("和 %1 的 TLS 握手被重置（网络干扰），换个网络或代理再试。").arg(host);
            return HallError::Tls;
        // RemoteHostClosedError：连上了（或假装连上了），对端一声不响把连接关掉。
        // 2026-11-01 实测：*.supabase.co 在本机就是这种——TCP 建连成功，随后被 RST，
        // 端口换 8443/2053、ALPN 换 http/1.1、明文 :80 全都一样，所以是主机级阻断。
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::ConnectionRefusedError:
            if (humanReason != nullptr)
                *humanReason = QStringLiteral("%1 把连接断掉了（没有返回任何 HTTP 响应），通常是这条线路到该主机被阻断。").arg(host);
            return HallError::Blocked;
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
            if (humanReason != nullptr)
                *humanReason = QStringLiteral("连接 %1 超时，稍后重试。").arg(host);
            return HallError::Timeout;
        default:
            break;
    }

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
        if (humanReason != nullptr)
            *humanReason = QStringLiteral("%1 返回了错误（HTTP %2），可能是站点改了接口或 key 轮换了。")
                               .arg(host)
                               .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        return HallError::Http;
    }

    if (humanReason != nullptr)
        *humanReason = QStringLiteral("访问 %1 失败：%2").arg(host, reply->errorString());
    return HallError::Http;
}

void HallAPI::finish(QNetworkReply* reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;

    const QByteArray body = reply->readAll();
    const QByteArray contentRange = reply->rawHeader("content-range");

    QString humanReason;
    const auto error = classify(reply, &humanReason);
    const QString errorString = reply->errorString();
    reply->deleteLater();

    if (error == HallError::Blocked && m_transport == Transport::Auto) {
        // "连接被掐断" = 这条线路的 SNI 被拦，换浏览器通道（它走 ECH）重发这一条。
        // 成功一次之后 m_browserPreferred 保持为真，同一会话不再先撞一次墙。
        sendViaBrowser(m_lastUrl);
        return;
    }
    if (error != HallError::None) {
        // 同步过程中的失败要回到「更新数据」按钮，不是大厅失败面板
        if (m_kind == Request::SyncList || m_kind == Request::SyncDetails || m_kind == Request::SyncUsers ||
            m_kind == Request::SyncReviews) {
            /**
             * 同步阶段某一步失败：只要列表已经拿到了，就别整批作废 —— 用已有数据落盘，
             * 缺的详情/评论按需回落网络。四步里任何一步都失败时才报告失败。
             */
            if (!m_pending.listRows.isEmpty()) {
                startSyncStep(4);
                return;
            }
            finishSync(false, humanReason.isEmpty() ? errorString : humanReason);
            return;
        }
        emit failed(error, humanReason, errorString);
        return;
    }

    handlePayload(body, contentRange);
}

void HallAPI::handlePayload(const QByteArray& payload, const QByteArray& contentRange)
{
    m_reply = nullptr;

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit failed(HallError::Parse, QStringLiteral("地图站点返回的数据看不懂，可能是接口变了。"),
                    parseError.errorString());
        return;
    }

    /**
     * PostgREST 的错误体是 `{"code":"42703","message":"column ... does not exist"}`，
     * 它**是合法 JSON**，但形状不对。不在这里拦，就会一路报"格式不对"，
     * 把真正的原因（列名变了 / 权限变了）藏起来。
     */
    if (document.isObject()) {
        const auto object = document.object();
        if (object.contains(QStringLiteral("message")) && object.contains(QStringLiteral("code"))) {
            if (m_kind == Request::SyncList || m_kind == Request::SyncDetails || m_kind == Request::SyncUsers ||
                m_kind == Request::SyncReviews) {
                if (!m_pending.listRows.isEmpty()) {
                    startSyncStep(4);
                    return;
                }
                finishSync(false, tr("地图站点返回了错误：%1").arg(object.value(QStringLiteral("message")).toString()));
                return;
            }
            emit failed(HallError::Http,
                        tr("地图站点返回了错误：%1").arg(object.value(QStringLiteral("message")).toString()),
                        QString::fromUtf8(payload.left(240)));
            return;
        }
    }

    switch (m_kind) {
        case Request::SyncList:
            m_pending.listRows = document.array();
            startSyncStep(1);
            return;
        case Request::SyncDetails: {
            const auto page = document.array();
            for (const auto& value : page)
                m_pending.detailRows.append(value);
            m_syncDetailOffset += page.size();

            // 覆盖检查：列表里所有 slug 都有详情了才收工（上限 3 页，防止异常数据死循环）
            const bool coversAll = [this] {
                for (const auto& value : m_pending.listRows) {
                    const QString slug = textOr(value.toObject().value(QStringLiteral("slug")));
                    if (slug.isEmpty())
                        continue;
                    bool found = false;
                    for (const auto& detail : m_pending.detailRows) {
                        if (textOr(detail.toObject().value(QStringLiteral("slug"))) == slug) {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return false;
                }
                return true;
            }();

            if (!coversAll && page.size() >= 1000 && m_syncDetailOffset < 3000) {
                startSyncStep(1);  // 同一步再翻一页
                return;
            }
            startSyncStep(2);
            return;
        }
        case Request::SyncUsers:
            m_pending.userRows = document.array();
            startSyncStep(3);
            return;
        case Request::SyncReviews:
            m_pending.reviewRows = document.array();
            startSyncStep(4);
            return;

        case Request::Search: {
            if (!document.isArray()) {
                emit failed(HallError::Parse, QStringLiteral("搜索结果格式不对。"), document.toJson(QJsonDocument::Compact));
                return;
            }
            QList<HallItem> items;
            for (const auto& value : document.array()) {
                if (value.isObject())
                    items.append(itemFromJson(value.toObject()));
            }
            if (items.isEmpty())
                emit failed(HallError::Empty, QStringLiteral("没有符合条件的地图，换个关键词或放宽筛选试试。"), {});
            else
                emit searchDone(items);
            return;
        }

        case Request::Detail: {
            const auto rows = document.array();
            if (rows.isEmpty()) {
                emit failed(HallError::Empty, QStringLiteral("找不到这张地图的详情（可能已下架）。"), {});
                return;
            }
            const auto row = rows.first().toObject();
            HallDetail detail;
            detail.item = itemFromJson(row);
            detail.item.pageUrl = QStringLiteral("https://www.ctmmaps.com/map/%1").arg(detail.item.slug);

            QStringList description;
            for (const auto* key : { "description_1", "description_2", "description_3" }) {
                const QString part = textOr(row.value(QLatin1String(key)));
                if (!part.isEmpty())
                    description.append(part);
            }
            detail.description = description.join(QStringLiteral("\n\n"));
            detail.trailerUrl = textOr(row.value(QStringLiteral("trailer")));

            const auto direct = textOr(row.value(QStringLiteral("download")));
            if (!direct.isEmpty())
                detail.downloadLinks.append(direct);
            const auto links = row.value(QStringLiteral("download_links")).toObject();
            for (auto it = links.constBegin(); it != links.constEnd(); ++it) {
                const QString url = it.value().toString();
                if (!url.isEmpty())
                    detail.downloadLinks.append(url);
            }
            if (!detail.downloadLinks.isEmpty())
                detail.item.downloadUrl = detail.downloadLinks.first();

            emit detailDone(detail);
            return;
        }

        case Request::Reviews: {
            if (!document.isArray()) {
                emit failed(HallError::Parse, QStringLiteral("评论数据格式不对。"), {});
                return;
            }
            QList<HallReview> rows;
            QList<QJsonObject> rawRows;
            for (const auto& value : document.array()) {
                if (!value.isObject())
                    continue;
                rawRows.append(value.toObject());
                rows.append(reviewFromJson(value.toObject()));
            }

            /**
             * 把这次拉到的并进按图索引：同一张图翻第二页时就命中本地，不再起浏览器。
             * 按 ledger_uuid 去重，避免翻页重复。
             */
            if (!m_lastReviewsMapUuid.isEmpty() && !rawRows.isEmpty()) {
                auto& bucket = m_reviewRowsByMap[m_lastReviewsMapUuid];
                for (const auto& row : rawRows) {
                    const QString id = textOr(row.value(QStringLiteral("ledger_uuid")));
                    bool exists = false;
                    for (const auto& known : bucket) {
                        if (textOr(known.value(QStringLiteral("ledger_uuid"))) == id) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists)
                        bucket.append(row);
                }
            }

            const int total = totalFromContentRange(contentRange);
            emit reviewsDone(rows, total, total >= 0, m_reviewOffset);
            return;
        }

        case Request::Users: {
            if (!document.isArray()) {
                // 不是数组说明接口变了；给空名字让界面继续显示「匿名玩家」，别整个失败
                emit userNamesDone({});
                return;
            }
            QHash<QString, QString> names;
            for (const auto& value : document.array()) {
                const auto row = value.toObject();
                const QString uuid = textOr(row.value(QStringLiteral("user_uuid")));
                const QString name = textOr(row.value(QStringLiteral("username")),
                                            textOr(row.value(QStringLiteral("name")), textOr(row.value(QStringLiteral("display_name")))));
                if (!uuid.isEmpty() && !name.isEmpty())
                    names.insert(uuid, name);
            }
            emit userNamesDone(names);
            return;
        }

        case Request::Facets: {
            HallFacets facets;
            for (const auto& value : document.array()) {
                const auto row = value.toObject();
                const auto add = [](QStringList& list, const QString& item) {
                    if (!item.isEmpty() && !list.contains(item))
                        list.append(item);
                };
                add(facets.series, textOr(row.value(QStringLiteral("series"))));
                add(facets.difficulties, textOr(row.value(QStringLiteral("difficulty"))));
                add(facets.lengths, textOr(row.value(QStringLiteral("length"))));
                add(facets.editions, textOr(row.value(QStringLiteral("edition"))));
                const QString date = textOr(row.value(QStringLiteral("sort_date")));
                if (date.size() >= 4)
                    add(facets.years, date.left(4));
            }
            emit facetsDone(facets);
            return;
        }
    }
}

void HallAPI::search(const HallQuery& query)
{
    m_kind = Request::Search;
    m_lastQuery = query;

    // 有本地快照：筛选/排序/翻页全在内存里，几毫秒出结果
    if (m_snapshot.isValid()) {
        const QList<HallItem> items = filterLocal(query);
        QMetaObject::invokeMethod(
            this,
            [this, items] {
                if (items.isEmpty())
                    emit failed(HallError::Empty, tr("没有符合条件的地图，换个关键词或放宽筛选试试。"), {});
                else
                    emit searchDone(items);
            },
            Qt::QueuedConnection);
        return;
    }

    const int limit = std::clamp(query.limit <= 0 ? 20 : query.limit, 1, 60);
    const int offset = std::max(0, query.offset);

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("select"), QStringLiteral("*"));
    params.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    params.addQueryItem(QStringLiteral("offset"), QString::number(offset));

    const QString text = query.text.trimmed();
    if (!text.isEmpty()) {
        QString pattern = text;
        for (const auto* forbidden : { "%", ",", "(", ")", "*" })
            pattern.remove(QLatin1String(forbidden));
        const QString like = QStringLiteral("*%1*").arg(pattern);
        params.addQueryItem(QStringLiteral("or"),
                            QStringLiteral("(name.ilike.%1,series.ilike.%1,event.ilike.%1)").arg(like));
    }
    if (!query.series.isEmpty())
        params.addQueryItem(QStringLiteral("series"), QStringLiteral("eq.%1").arg(query.series));
    if (!query.difficulties.isEmpty())
        params.addQueryItem(QStringLiteral("difficulty"), inList(query.difficulties));
    if (!query.lengths.isEmpty())
        params.addQueryItem(QStringLiteral("length"), inList(query.lengths));
    if (!query.edition.isEmpty())
        params.addQueryItem(QStringLiteral("edition"), QStringLiteral("eq.%1").arg(query.edition));
    if (query.year.size() == 4 && query.year.toInt() > 0) {
        // PostgREST 允许同一个键出现两次来表示区间
        params.addQueryItem(QStringLiteral("sort_date"), QStringLiteral("gte.%1-01-01").arg(query.year));
        params.addQueryItem(QStringLiteral("sort_date"), QStringLiteral("lte.%1-12-31").arg(query.year));
    }
    if (query.ratingMin > 0)
        params.addQueryItem(QStringLiteral("avg_rating"), QStringLiteral("gte.%1").arg(query.ratingMin));
    if (query.playtimeMax > 0)
        params.addQueryItem(QStringLiteral("average_playtime_minutes"), QStringLiteral("lte.%1").arg(query.playtimeMax));

    static const QHash<QString, QStringList> sortOrders{
        { QStringLiteral("release_desc"), { QStringLiteral("sort_date.desc.nullslast") } },
        { QStringLiteral("release_asc"), { QStringLiteral("sort_date.asc.nullslast") } },
        { QStringLiteral("rating_desc"), { QStringLiteral("avg_rating.desc.nullslast") } },
        { QStringLiteral("rating_asc"), { QStringLiteral("avg_rating.asc.nullslast") } },
        { QStringLiteral("popularity_desc"),
          { QStringLiteral("total_unique_plays.desc.nullslast"), QStringLiteral("avg_rating.desc.nullslast") } },
        { QStringLiteral("popularity_asc"), { QStringLiteral("total_unique_plays.asc.nullslast") } },
        { QStringLiteral("playtime_desc"), { QStringLiteral("average_playtime_minutes.desc.nullslast") } },
        { QStringLiteral("playtime_asc"), { QStringLiteral("average_playtime_minutes.asc.nullslast") } },
    };
    const auto order = sortOrders.value(query.sort, sortOrders.value(QStringLiteral("release_desc")));
    for (const auto& clause : order)
        params.addQueryItem(QStringLiteral("order"), clause);

    QUrl url(QStringLiteral("%1/rest/v1/maps_search_view").arg(m_endpoint));
    url.setQuery(params);
    get(url);
}

void HallAPI::detail(const QString& slug)
{
    m_kind = Request::Detail;

    /**
     * 先查本地快照。同步只拉了前 1000 张图的详情，老图可能没有 ——
     * 这种情况仍然回落网络，别让"没有简介"冒充"这张图没写简介"。
     */
    if (m_snapshot.isValid() && m_detailBySlug.contains(slug)) {
        const HallDetail detail = buildDetailFromCache(slug);
        QMetaObject::invokeMethod(this, [this, detail] { emit detailDone(detail); }, Qt::QueuedConnection);
        return;
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("select"), QStringLiteral("*"));
    params.addQueryItem(QStringLiteral("slug"), QStringLiteral("eq.%1").arg(slug));
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));

    QUrl url(QStringLiteral("%1/rest/v1/maps").arg(m_endpoint));
    url.setQuery(params);
    get(url);
}

void HallAPI::reviews(const QString& mapUuid, int limit, int offset)
{
    m_kind = Request::Reviews;
    const int lim = std::clamp(limit, 1, 30);
    m_reviewOffset = std::max(0, offset);

    /**
     * 本地快照里有这张图的评论 → 直接分页返回（连同总数），不再发请求。
     * 例外：快照的评论上限是 1000 条，正好拉满说明可能被截断，此时缺评论的图仍走网络。
     */
    if (m_snapshot.isValid()) {
        const auto rows = m_reviewRowsByMap.value(mapUuid);
        /**
         * 整库同步只拉最近 1000 条评论（跨所有地图），所以**不能**拿它当某张图的全部：
         * 老图的评论大多落在这个窗口之外，界面就会显示成「评论（2）」，而站上有 6 条以上。
         * 只有"这一批没拉满 1000"时才敢说缓存是全量的。
         */
        const bool complete = m_snapshot.reviewRows.size() < 1000;
        if (complete && (!rows.isEmpty() || rows.isEmpty())) {
            QList<HallReview> page;
            for (int i = m_reviewOffset; i < rows.size() && page.size() < lim; ++i)
                page.append(reviewFromJson(rows.at(i)));
            const int total = rows.size();
            m_reply = nullptr;
            QMetaObject::invokeMethod(
                this, [this, page, total, offset] { emit reviewsDone(page, total, true, offset); }, Qt::QueuedConnection);
            return;
        }
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("select"),
                        QStringLiteral("ledger_uuid,user_uuid,rating,review,played_date,playtime_total_minutes,"
                                       "deaths,bonus_objectives,hearts,speedrun"));
    params.addQueryItem(QStringLiteral("map_uuid"), QStringLiteral("eq.%1").arg(mapUuid));
    params.addQueryItem(QStringLiteral("review"), QStringLiteral("not.is.null"));
    params.addQueryItem(QStringLiteral("order"), QStringLiteral("played_date.desc"));
    params.addQueryItem(QStringLiteral("limit"), QString::number(lim));
    // offset=0 时不写：PostgREST 对两者等价，少一个参数少一处出错
    if (m_reviewOffset > 0)
        params.addQueryItem(QStringLiteral("offset"), QString::number(m_reviewOffset));

    QUrl url(QStringLiteral("%1/rest/v1/user_maps").arg(m_endpoint));
    url.setQuery(params);

    /**
     * 评论这条要带 `Prefer: count=exact`（总数只能从 Content-Range 读），
     * 所以不能复用 get() —— 但浏览器通道仍要能接上：记住 URL，并直接分流。
     * 浏览器发不了自定义头，走那条路时 totalKnown=false，界面显示"已加载 N 条"。
     */
    m_lastUrl = url;
    m_lastReviewsMapUuid = mapUuid;
    abort();
    if (m_transport == Transport::Browser || m_browserPreferred) {
        sendViaBrowser(url);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("apikey", QByteArray(ANON_KEY));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray(ANON_KEY));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Prefer", "count=exact");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    auto* reply = m_nam->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
}

void HallAPI::userNames(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        emit userNamesDone({});
        return;
    }

    if (m_snapshot.isValid()) {
        QHash<QString, QString> names;
        for (const auto& uuid : uuids) {
            const QString name = m_userById.value(uuid);
            if (!name.isEmpty())
                names.insert(uuid, name);
        }
        // 全都查到了，或者 users 表本来就没拉满（说明确实没这个人）→ 不联网
        if (names.size() == uuids.size() || m_snapshot.userRows.size() < 3000) {
            QMetaObject::invokeMethod(this, [this, names] { emit userNamesDone(names); }, Qt::QueuedConnection);
            return;
        }
    }

    m_kind = Request::Users;

    QStringList escaped;
    for (auto uuid : uuids) {
        uuid.remove(QLatin1Char('('));
        uuid.remove(QLatin1Char(')'));
        uuid.remove(QLatin1Char(','));
        escaped.append(uuid);
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("select"), QStringLiteral("*"));
    params.addQueryItem(QStringLiteral("user_uuid"), QStringLiteral("in.(%1)").arg(escaped.join(QLatin1Char(','))));

    QUrl url(QStringLiteral("%1/rest/v1/users").arg(m_endpoint));
    url.setQuery(params);
    get(url);
}

void HallAPI::facets()
{
    m_kind = Request::Facets;

    if (m_snapshot.isValid()) {
        HallFacets facets;
        const auto add = [](QStringList& list, const QString& value) {
            if (!value.isEmpty() && !list.contains(value))
                list.append(value);
        };
        for (const auto& item : m_items) {
            add(facets.series, item.series);
            add(facets.difficulties, item.difficulty);
            add(facets.lengths, item.length);
            add(facets.editions, item.edition);
            if (item.releaseDate.size() >= 4)
                add(facets.years, item.releaseDate.left(4));
        }
        std::sort(facets.series.begin(), facets.series.end());
        std::sort(facets.years.begin(), facets.years.end(), std::greater<QString>());
        QMetaObject::invokeMethod(this, [this, facets] { emit facetsDone(facets); }, Qt::QueuedConnection);
        return;
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("select"),
                        QStringLiteral("series,difficulty,length,edition,sort_date"));
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("2000"));

    QUrl url(QStringLiteral("%1/rest/v1/maps_search_view").arg(m_endpoint));
    url.setQuery(params);
    get(url);
}

HallReview HallAPI::reviewFromJson(const QJsonObject& row) const
{
    HallReview review;
    review.id = textOr(row.value(QStringLiteral("ledger_uuid")));
    review.mapUuid = textOr(row.value(QStringLiteral("map_uuid")));
    review.userUuid = textOr(row.value(QStringLiteral("user_uuid")));
    review.rating = static_cast<int>(numberOr(row.value(QStringLiteral("rating")), -1));
    review.text = textOr(row.value(QStringLiteral("review")));
    review.playedDate = textOr(row.value(QStringLiteral("played_date"))).left(10);
    review.playtimeMinutes = static_cast<int>(numberOr(row.value(QStringLiteral("playtime_total_minutes")), -1));
    review.deaths = static_cast<int>(numberOr(row.value(QStringLiteral("deaths")), -1));
    review.bonusObjectives = static_cast<int>(numberOr(row.value(QStringLiteral("bonus_objectives")), -1));
    review.hearts = static_cast<int>(numberOr(row.value(QStringLiteral("hearts")), -1));
    review.speedrun = row.value(QStringLiteral("speedrun")).toBool(false);
    /**
     * 名字在解析时就地补：users 表 220/239 有 username（其余是真没填），
     * 不补的话每个界面都得自己再查一遍 —— 之前信息页就是这样写死了「匿名玩家」。
     */
    if (!review.userUuid.isEmpty())
        review.userName = m_userById.value(review.userUuid);
    return review;
}

int HallAPI::totalFromContentRange(const QByteArray& header)
{
    // `0-7/123` -> 123；`0-7/*` 或没有这个头 -> -1（不知道，别编 0）
    const int slash = header.lastIndexOf('/');
    if (slash < 0)
        return -1;
    bool ok = false;
    const int total = header.mid(slash + 1).trimmed().toInt(&ok);
    return ok ? total : -1;
}

HallItem HallAPI::itemFromJson(const QJsonObject& row)
{
    HallItem item;
    item.mapUuid = textOr(row.value(QStringLiteral("map_uuid")));
    item.slug = textOr(row.value(QStringLiteral("slug")), item.mapUuid);
    item.id = item.slug;
    item.title = textOr(row.value(QStringLiteral("name")));
    if (item.title.isEmpty())
        item.title = QStringLiteral("(未命名)");

    item.creators = csv(row.value(QStringLiteral("creators_array")));
    if (item.creators.isEmpty()) {
        const QString single = textOr(row.value(QStringLiteral("creator")));
        if (!single.isEmpty())
            item.creators.append(single);
    }
    item.author = item.creators.join(QStringLiteral(", "));
    item.series = textOr(row.value(QStringLiteral("series")));
    QStringList subtitleParts;
    if (!item.series.isEmpty())
        subtitleParts.append(item.series);
    if (!item.author.isEmpty())
        subtitleParts.append(item.author);
    item.subtitle = subtitleParts.join(QStringLiteral(" · "));

    item.edition = textOr(row.value(QStringLiteral("edition")));
    item.version = textOr(row.value(QStringLiteral("version")));
    item.difficulty = textOr(row.value(QStringLiteral("difficulty")));
    item.length = textOr(row.value(QStringLiteral("length")));
    for (const auto& tag : { item.difficulty, item.length, item.edition }) {
        if (!tag.isEmpty())
            item.tags.append(tag);
    }

    item.coverUrl = textOr(row.value(QStringLiteral("image_main")));
    item.pageUrl = QStringLiteral("https://www.ctmmaps.com/map/%1").arg(item.slug);
    item.downloadUrl = textOr(row.value(QStringLiteral("download")));

    item.seriesNum = static_cast<int>(numberOr(row.value(QStringLiteral("series_num")), -1));
    item.objectives = static_cast<int>(numberOr(row.value(QStringLiteral("objectives")), -1));
    item.bonuses = static_cast<int>(numberOr(row.value(QStringLiteral("bonus_objectives")), -1));
    item.minutes = numberOr(row.value(QStringLiteral("average_playtime_minutes")), -1);
    item.plays = static_cast<int>(numberOr(row.value(QStringLiteral("total_unique_plays")), -1));

    const double rating = numberOr(row.value(QStringLiteral("avg_rating")), -1);
    // 站方给的是 1.75000000000000000000 这种高精度值，界面只要一位小数
    item.rating = rating < 0 ? -1 : std::round(rating * 10) / 10;

    const QString date = textOr(row.value(QStringLiteral("release_date")), textOr(row.value(QStringLiteral("sort_date"))));
    item.releaseDate = date.left(10);
    item.lostMedia = row.value(QStringLiteral("lost_media")).toBool(false);

    return item;
}

}  // namespace maps
