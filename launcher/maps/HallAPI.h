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
#pragma once

#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QObject>
#include <QUrl>

#include "HallCache.h"
#include "HallTypes.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace maps {

class BrowserFetch;

/**
 * 地图大厅：把「外面有哪些地图」搬进启动器。
 *
 * 数据来自 ctmmaps.com 的 Supabase 公开读接口（PostgREST）。三条铁律：
 *   1. 查询一律 `select=*`，**不写死列名** —— 列是站方的，改列是常态（AIO 为此吃过 400）；
 *   2. 不做启动器内下载：`downloadUrl` 交给系统浏览器（Cloudflare 只认可见浏览器）；
 *   3. 失败要给人话：分类（DNS / TLS / 超时 / HTTP / 空结果）+ 技术细节 + 重试。
 *
 * 每条请求互斥：新的搜索会丢弃旧的（大厅是「最后一次输入为准」的交互）。
 */
class HallAPI : public QObject {
    Q_OBJECT

   public:
    explicit HallAPI(QObject* parent = nullptr);
    ~HallAPI() override;

    /// 覆盖基地址（设置项 MapHallEndpoint）；空 = 用默认
    void setEndpoint(const QString& base);
    QString endpoint() const;

    /**
     * 传输方式（设置项 MapHallTransport）：
     *   · Auto    —— 先走网络；一旦被判"连接被掐断"就自动切浏览器通道（默认）
     *   · Network —— 只用 Qt 网络（有代理时用这个，快）
     *   · Browser —— 只用本机浏览器的无头模式（见 BrowserFetch 的注释）
     */
    enum class Transport { Auto, Network, Browser };
    void setTransport(Transport mode);
    Transport transport() const { return m_transport; }
    /// 当前实际在用哪条通道（给状态栏写人话）
    QString transportNote() const;

    // ── 本地快照（大厅数据整体缓存） ──────────────────────────────────────
    /// 本地有没有可用快照（有就说明"不用联网也能浏览"）
    bool hasLibrary() const { return m_snapshot.isValid(); }
    /// 有库但详情覆盖不全（旧版同步留下的）：界面据此自动重同步
    bool libraryIncomplete() const { return m_libraryIncomplete; }
    QDateTime libraryFetchedAt() const { return m_snapshot.fetchedAt; }
    int libraryItemCount() const { return m_items.size(); }
    qint64 libraryBytes() const;
    /**
     * 全量同步：4 个请求（列表 / 详情 / 用户 / 评论）依次拉下来存本地。
     *
     * 为什么要"整体拉"而不是每次搜索都请求：这条线路上每个请求都要借无头浏览器，
     * 一次 3–10 秒；而整库只有几百张图。同步一次之后，搜索/排序/翻页/看详情/看评论
     * 全部本地算，几毫秒出结果；发现新图时点一次「更新数据」即可。
     */
    void syncLibrary();
    void clearLibrary();
    /// 从本地库按标题找一张图（信息页用来做"可能关联"建议与手动挑选）
    QList<HallItem> libraryItems() const { return m_items; }
    /// 从磁盘重新载入（后台更新写完之后，界面下次打开就能看到新数据）
    void reloadLibrary() { applySnapshot(HallCache::load()); }

    void search(const HallQuery& query);
    void detail(const QString& slug);
    void facets();
    /// 评论分页：总数只能从 Content-Range 读，读不到时 totalKnown=false（界面别编数字）
    void reviews(const QString& mapUuid, int limit = 6, int offset = 0);
    /// 按 uuid 批量补用户名（两张表没有外键，PostgREST 的嵌套 select 会报 PGRST200）
    void userNames(const QStringList& uuids);

    /// 从 `content-range: 0-7/123` 读总数；`0-7/*` 或没有这个头返回 -1（不知道，别编 0）
    static int totalFromContentRange(const QByteArray& header);

    bool isBusy() const { return m_reply != nullptr || m_browserBusy; }
    void abort();

    /**
     * 大厅专用的网络管理器。
     *
     * 不复用 `APPLICATION->network()`：那边挂的是启动器全局代理（玩家为了 meta/下载
     * 常常设成直连），而 `*.supabase.co` 恰恰必须走代理。这里按 HallProxy 解析出的
     * 出口单独配置，互不干扰。
     */
    QNetworkAccessManager* network() const { return m_nam; }

    /// 当前出口的人话说明（「直连」/「环境变量代理 …」），状态栏与空态用
    QString proxyNote() const;

    /// 让下一次请求重新解析出口（设置改完、或用户点「重试」）
    static void invalidateProxyCache();

    /**
     * 「出网诊断」：拿当前出口打一次轻量请求（`/rest/v1/` 根路径），
     * 回报耗时 / 是否通 / 一句说明。AIO 的空态里就是这个按钮 ——
     * 用户遇到"浏览器能开、这里不行"时，这一条能立刻区分是代理没配上、代理连不上、
     * 还是站点本身返回空。
     */
    void probe();

   signals:
    void searchDone(const QList<maps::HallItem>& items);
    void detailDone(const maps::HallDetail& detail);
    void facetsDone(const maps::HallFacets& facets);
    void reviewsDone(const QList<maps::HallReview>& rows, int total, bool totalKnown, int offset);
    void userNamesDone(const QHash<QString, QString>& nameByUuid);
    void failed(maps::HallError kind, const QString& humanReason, const QString& techDetail);
    void probeDone(bool ok, int elapsedMs, const QString& detail);
    /// 同步进度：step 从 1 开始
    void syncProgress(int step, int total, const QString& text);
    void syncDone(bool ok, const QString& message);

   private:
    /// 发一次请求；allowProxy=false 时走"直连"那份 NAM（代理失败后的第二次尝试）
    QNetworkReply* send(const QUrl& url, bool allowProxy);
    /// 诊断的第二跳：走代理失败后换直连再来一次（照 AIO probe 的兜底）
    void sendProbe(bool useProxy, qint64 startedAt);
    /// 按当前 endpoint 的主机名重新解析出口（内网 / NO_PROXY 会强制直连）
    void applyProxy();

    QNetworkAccessManager* m_nam = nullptr;
    /// 只用于「代理失败 → 直连再试一次」，平时不碰
    QNetworkAccessManager* m_directNam = nullptr;
    QString m_proxyNote;
    bool m_proxyActive = false;
    /// 这一次请求的兜底记录（成功走了直连时，状态栏要如实写出来）
    QString m_fallbackNote;

    QNetworkReply* get(const QUrl& url);
    /// 同步详情页的分页游标（maps 表按 slug 翻页，直到覆盖列表里所有 slug）
    int m_syncDetailOffset = 0;
    bool m_libraryIncomplete = false;
    /// 快照载入后建索引（列表项 / 详情 / 用户名 / 按图分组的评论）
    void applySnapshot(const HallSnapshot& snapshot);
    /// 本地筛选 + 排序 + 分页（语义对齐服务端查询）
    QList<HallItem> filterLocal(const HallQuery& query) const;
    /// 用快照拼出详情
    HallDetail buildDetailFromCache(const QString& slug) const;
    /// 同步状态机：0..3 四个请求，4 = 落盘完成
    void startSyncStep(int step);
    void finishSync(bool ok, const QString& message);
    void finish(QNetworkReply* reply);
    /// 走浏览器通道重发最近一条请求
    void sendViaBrowser(const QUrl& url);
    /// 解析 JSON（网络通道与浏览器通道共用）
    void handlePayload(const QByteArray& body, const QByteArray& contentRange);
    static HallError classify(QNetworkReply* reply, QString* humanReason);
    static HallItem itemFromJson(const QJsonObject& row);
    HallReview reviewFromJson(const QJsonObject& row) const;
    static QString inList(const QStringList& values);

    QString m_endpoint;
    QNetworkReply* m_reply = nullptr;
    Transport m_transport = Transport::Auto;
    /// 直连被判阻断后自动切换，并记住（同一会话内不再先撞一次墙）
    bool m_browserPreferred = false;
    bool m_browserBusy = false;
    BrowserFetch* m_browser = nullptr;
    /// 最近一次请求的 URL：Blocked 时用它改走浏览器通道
    QUrl m_lastUrl;
    enum class Request { Search, Detail, Facets, Reviews, Users, SyncList, SyncDetails, SyncUsers, SyncReviews } m_kind =
        Request::Search;
    int m_reviewOffset = 0;
    HallQuery m_lastQuery;

    // ── 本地快照 ──
    HallSnapshot m_snapshot;
    HallSnapshot m_pending;  ///< 同步过程中的累加器
    int m_syncStep = 0;
    QList<HallItem> m_items;
    QHash<QString, QJsonObject> m_detailBySlug;
    QHash<QString, QString> m_userById;
    QHash<QString, QList<QJsonObject>> m_reviewRowsByMap;
    /// 评论是按图按需拉的（整库 1000 条窗口装不下所有图），记下当前请求属于哪张图
    QString m_lastReviewsMapUuid;
};

}  // namespace maps
