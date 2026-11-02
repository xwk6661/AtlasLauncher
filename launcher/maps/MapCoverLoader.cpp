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
#include "MapCoverLoader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <atomic>

#include "Application.h"
#include "BrowserFetch.h"

namespace maps {
namespace {

constexpr int BATCH_SIZE = 8;          ///< 一次浏览器进程顺带取几张
constexpr int PARALLEL_IN_PAGE = 6;    ///< 页面内同时发几个 <img>
constexpr int COVER_WIDTH = 480;       ///< 落盘宽度：列表 288、详情 96，480 足够且留了余量
constexpr int DIRECT_TIMEOUT_MS = 6000;
constexpr int BROWSER_TIMEOUT_MS = 45000;
constexpr int MAX_ATTEMPTS = 2;

QString unescapeHtml(QByteArray text)
{
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    text.replace("&#39;", "'");
    text.replace("&amp;", "&");
    return QString::fromUtf8(text);
}

/// 从 dump 出来的 DOM 里取 `<pre id="covers">` 的文本
QString extractCovers(const QByteArray& dom)
{
    const int start = dom.indexOf("<pre id=\"covers\"");
    if (start < 0)
        return {};
    const int contentStart = dom.indexOf('>', start);
    const int end = dom.indexOf("</pre>", contentStart);
    if (contentStart < 0 || end < 0)
        return {};
    return unescapeHtml(dom.mid(contentStart + 1, end - contentStart - 1));
}

QByteArray decodeDataUrl(const QString& dataUrl)
{
    const int comma = dataUrl.indexOf(QLatin1Char(','));
    if (comma < 0 || !dataUrl.startsWith(QLatin1String("data:image/")))
        return {};
    return QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1());
}

}  // namespace

MapCoverLoader::MapCoverLoader(QObject* parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &MapCoverLoader::dispatch);

    m_nam = new QNetworkAccessManager(this);
    m_ownNam = true;
}

MapCoverLoader::~MapCoverLoader()
{
    if (m_browser != nullptr) {
        m_browser->disconnect(this);
        m_browser->kill();
        m_browser->waitForFinished(2000);
    }
    if (!m_browserProfile.isEmpty())
        QDir(m_browserProfile).removeRecursively();
}

MapCoverLoader* MapCoverLoader::instance()
{
    static MapCoverLoader loader;
    return &loader;
}

QString MapCoverLoader::cacheDir() const
{
    if (!m_cacheDir.isEmpty())
        return m_cacheDir;

    QString root;
    if (auto* app = qobject_cast<Application*>(QCoreApplication::instance()))
        root = app->dataRoot();
    else
        root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    return QDir(root).filePath(QStringLiteral("maps/covers"));
}

void MapCoverLoader::setCacheDir(const QString& dir)
{
    m_cacheDir = dir;
    QDir().mkpath(m_cacheDir);
}

void MapCoverLoader::setNetwork(QNetworkAccessManager* nam)
{
    if (nam == nullptr)
        return;
    if (m_ownNam && m_nam != nullptr)
        m_nam->deleteLater();
    m_nam = nam;
    m_ownNam = false;
}

void MapCoverLoader::setTransportMode(const QString& mode)
{
    const auto lowered = mode.trimmed().toLower();
    m_transport = (lowered == QLatin1String("network") || lowered == QLatin1String("browser")) ? lowered
                                                                                              : QStringLiteral("auto");
}

QString MapCoverLoader::cachePath(const QString& url) const
{
    const auto key = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(cacheDir()).filePath(QString::fromLatin1(key) + QStringLiteral(".jpg"));
}

QImage MapCoverLoader::loadFromDisk(const QString& url) const
{
    QFile file(cachePath(url));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QImage::fromData(file.readAll(), "JPG");
}

void MapCoverLoader::saveToDisk(const QString& url, const QByteArray& jpeg)
{
    QDir().mkpath(cacheDir());
    QFile file(cachePath(url));
    if (file.open(QIODevice::WriteOnly))
        file.write(jpeg);
}

QImage MapCoverLoader::cached(const QString& url)
{
    if (url.isEmpty())
        return {};

    auto it = m_memory.constFind(url);
    if (it != m_memory.constEnd()) {
        // 命中就挪到队尾（最近使用）
        m_lru.removeAll(url);
        m_lru.append(url);
        return it.value();
    }

    const auto image = loadFromDisk(url);
    if (!image.isNull())
        rememberImage(url, image);
    return image;
}

void MapCoverLoader::rememberImage(const QString& url, const QImage& image)
{
    if (image.isNull())
        return;
    m_memory.insert(url, image);
    m_lru.removeAll(url);
    m_lru.append(url);
    while (m_lru.size() > m_memoryCap) {
        const auto oldest = m_lru.takeFirst();
        m_memory.remove(oldest);
    }
}

void MapCoverLoader::request(const QString& url)
{
    if (url.isEmpty() || m_queued.contains(url))
        return;

    if (m_bad.contains(url)) {
        // 已知取不到：也给个回执，免得调用方的「pending」永远挂着
        QTimer::singleShot(0, this, [this, url] { emit coverFailed(url, tr("这张封面之前已确认取不到")); });
        return;
    }

    if (const auto image = cached(url); !image.isNull()) {
        // 缓存命中：延后一拍再发，调用方总能先连上信号
        QTimer::singleShot(0, this, [this, url, image] { emit coverReady(url, image); });
        return;
    }

    m_queued.insert(url);
    m_queue.append(url);
    schedule();
}

void MapCoverLoader::schedule()
{
    if (m_busy || m_queue.isEmpty())
        return;
    if (!m_timer->isActive())
        m_timer->start(120);  // 合并同一帧里的多行请求，别一行一个浏览器进程
}

QStringList MapCoverLoader::takeBatch()
{
    QStringList batch;
    while (!m_queue.isEmpty() && batch.size() < BATCH_SIZE) {
        const auto url = m_queue.takeFirst();
        m_queued.remove(url);
        batch.append(url);
    }
    return batch;
}

MapCoverLoader::HostMode MapCoverLoader::hostMode(const QString& host) const
{
    return m_hostModes.value(host, HostMode::Unknown);
}

void MapCoverLoader::setHostMode(const QString& host, HostMode mode)
{
    if (!host.isEmpty())
        m_hostModes.insert(host, mode);
}

void MapCoverLoader::dispatch()
{
    if (m_busy)
        return;

    m_batch = takeBatch();
    if (m_batch.isEmpty())
        return;

    m_busy = true;
    m_batchOk = 0;
    m_batchFailed = 0;
    m_directQueue.clear();
    m_browserQueue.clear();

    for (const auto& url : m_batch) {
        const auto host = QUrl(url).host();
        if (m_transport == QLatin1String("browser")) {
            m_browserQueue.append(url);
        } else if (m_transport == QLatin1String("network")) {
            m_directQueue.append(url);
        } else if (hostMode(host) == HostMode::Browser) {
            m_browserQueue.append(url);
        } else {
            // 未知主机先直连探一次：没被阻断的网络里 Qt 比开浏览器快一个数量级
            m_directQueue.append(url);
        }
    }

    if (m_directQueue.isEmpty())
        startBrowser();
    else
        tryDirect();
}

void MapCoverLoader::tryDirect()
{
    if (m_nam.isNull()) {
        // 外部给的网络管理器被页面带走了：退回到自己的那个
        m_nam = new QNetworkAccessManager(this);
        m_ownNam = true;
    }

    m_pendingReplies = m_directQueue.size();

    for (const auto& url : m_directQueue) {
        QNetworkRequest request{ QUrl(url) };
        request.setRawHeader("User-Agent",
                             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                             "Chrome/156.0.0.0 Safari/537.36");
        request.setRawHeader("Referer", "https://www.ctmmaps.com/");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(DIRECT_TIMEOUT_MS);

        auto* reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            const auto body = reply->readAll();
            const auto error = reply->error();
            reply->deleteLater();

            QImage image;
            if (error == QNetworkReply::NoError)
                image.loadFromData(body);

            if (!image.isNull()) {
                // 直连成功 → 这条线路对这个主机是通的，后面同类请求不必再开浏览器
                setHostMode(QUrl(url).host(), HostMode::Direct);
                acceptImage(url, body, false);
            } else {
                setHostMode(QUrl(url).host(), HostMode::Browser);
                if (m_transport == QLatin1String("network")) {
                    failUrl(url, tr("网络通道取图失败"));
                } else {
                    // 换浏览器通道再试：SNI 阻断只有 Chromium 的 ECH 能过
                    if (!m_browserQueue.contains(url))
                        m_browserQueue.append(url);
                }
            }

            if (--m_pendingReplies > 0)
                return;
            if (m_browserQueue.isEmpty())
                finishBatch();
            else
                startBrowser();
        });
    }
}

QString MapCoverLoader::generatePage(const QStringList& urls) const
{
    QJsonArray array;
    for (const auto& url : urls)
        array.append(url);
    // 内联进 <script> 前把 "</" 转义，避免 URL 里出现 </script> 把页面截断
    auto json = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    json.replace(QLatin1String("</"), QLatin1String("<\\/"));

    return QStringLiteral(R"(<!doctype html>
<html><head><meta charset="utf-8"><title>PrismLauncher covers</title></head>
<body>
<pre id="covers">pending</pre>
<script>
const URLS = %1;
const W = %2;
const LIMIT = %3;
const results = new Array(URLS.length);
let next = 0, done = 0;
function finishOne() {
  if (++done === URLS.length) document.getElementById('covers').textContent = JSON.stringify(results);
}
function loadOne(i, attempt) {
  if (i >= URLS.length) return;
  const img = new Image();
  img.onload = () => {
    try {
      const c = document.createElement('canvas');
      const h = Math.max(1, Math.round(W * img.naturalHeight / img.naturalWidth));
      c.width = W; c.height = h;
      c.getContext('2d').drawImage(img, 0, 0, W, h);
      results[i] = { ok: true, data: c.toDataURL('image/jpeg', 0.82) };
    } catch (e) {
      results[i] = { ok: false, error: String(e) };
    }
    finishOne();
    if (next < URLS.length) loadOne(next++, 1);
  };
  img.onerror = () => {
    if (attempt < 2) { setTimeout(() => loadOne(i, attempt + 1), 220); return; }
    results[i] = { ok: false, error: 'imgError' };
    finishOne();
    if (next < URLS.length) loadOne(next++, 1);
  };
  const sep = URLS[i].includes('?') ? '&' : '?';
  img.src = attempt === 1 ? URLS[i] : URLS[i] + sep + 'retry=' + attempt;
}
for (let i = 0; i < Math.min(LIMIT, URLS.length); ++i) loadOne(next++, 1);
</script>
</body></html>)")
        .arg(json)
        .arg(COVER_WIDTH)
        .arg(PARALLEL_IN_PAGE);
}

void MapCoverLoader::startBrowser()
{
    if (m_browserQueue.isEmpty()) {
        finishBatch();
        return;
    }

    const auto browser = BrowserFetch::browserPath();
    if (browser.isEmpty()) {
        for (const auto& url : m_browserQueue)
            failUrl(url, tr("本机没有找到 Edge / Chrome，无法用浏览器通道取图"));
        m_browserQueue.clear();
        finishBatch();
        return;
    }

    static std::atomic<int> counter{ 0 };
    m_browserProfile = QDir::tempPath() + QStringLiteral("/prism-covers-%1-%2")
                                                  .arg(QCoreApplication::applicationPid())
                                                  .arg(++counter);
    QDir().mkpath(m_browserProfile);

    const auto pagePath = QDir(m_browserProfile).filePath(QStringLiteral("covers.html"));
    QFile page(pagePath);
    if (!page.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        for (const auto& url : m_browserQueue)
            failUrl(url, tr("无法写入取图页面"));
        m_browserQueue.clear();
        finishBatch();
        return;
    }
    page.write(generatePage(m_browserQueue).toUtf8());
    page.close();

    const int budget = 9000 + 1500 * ((m_browserQueue.size() + PARALLEL_IN_PAGE - 1) / PARALLEL_IN_PAGE);

    m_browser = new QProcess(this);
    m_browser->setProgram(browser);
    m_browser->setArguments({
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        // 图床的 QUIC 常被中间设备打断，走 TCP 更稳（与 BrowserFetch 同一结论）
        QStringLiteral("--disable-quic"),
        /**
         * 必须关 Web 安全：跨域图片画进 canvas 会被标记为 tainted，toDataURL 直接抛异常。
         * 这个进程用一次性空白 profile、只加载上面那个临时页面，跑完即杀。
         */
        QStringLiteral("--disable-web-security"),
        QStringLiteral("--no-proxy-server"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-extensions"),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--mute-audio"),
        QStringLiteral("--hide-scrollbars"),
        QStringLiteral("--user-data-dir=%1").arg(m_browserProfile),
        QStringLiteral("--virtual-time-budget=%1").arg(budget),
        QStringLiteral("--dump-dom"),
        QUrl::fromLocalFile(pagePath).toString(),
    });

    auto* killer = new QTimer(this);
    killer->setSingleShot(true);
    connect(killer, &QTimer::timeout, this, [this, killer] {
        killer->deleteLater();
        if (m_browser != nullptr && m_browser->state() != QProcess::NotRunning)
            m_browser->kill();
    });
    killer->start(BROWSER_TIMEOUT_MS);

    connect(m_browser, &QProcess::finished, this, [this, killer](int code, QProcess::ExitStatus status) {
        killer->stop();
        killer->deleteLater();
        if (m_browser == nullptr)
            return;

        const auto dom = m_browser->readAllStandardOutput();
        m_browser->deleteLater();
        m_browser = nullptr;
        QDir(m_browserProfile).removeRecursively();
        m_browserProfile.clear();

        const auto payload = extractCovers(dom);
        const auto document = QJsonDocument::fromJson(payload.toUtf8());
        if (!document.isArray()) {
            const auto reason = status == QProcess::CrashExit ? tr("浏览器进程崩溃（退出码 %1）").arg(code)
                                                              : tr("浏览器没有返回封面数据（退出码 %1）").arg(code);
            for (const auto& url : m_browserQueue)
                failUrl(url, reason);
            m_browserQueue.clear();
            finishBatch();
            return;
        }

        const auto array = document.array();
        for (int i = 0; i < m_browserQueue.size(); ++i) {
            const auto url = m_browserQueue.at(i);
            const auto entry = i < array.size() ? array.at(i).toObject() : QJsonObject{};
            const auto bytes = entry.value(QStringLiteral("ok")).toBool()
                                   ? decodeDataUrl(entry.value(QStringLiteral("data")).toString())
                                   : QByteArray{};
            if (bytes.isEmpty()) {
                const auto attempts = m_attempts.value(url) + 1;
                m_attempts.insert(url, attempts);
                if (attempts < MAX_ATTEMPTS) {
                    if (!m_queued.contains(url)) {
                        m_queued.insert(url);
                        m_queue.append(url);
                    }
                } else {
                    failUrl(url, entry.value(QStringLiteral("error")).toString(tr("取图失败")));
                }
                continue;
            }
            acceptImage(url, bytes, true);
        }

        m_browserQueue.clear();
        finishBatch();
    });

    connect(m_browser, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        QDir(m_browserProfile).removeRecursively();
        m_browserProfile.clear();
        if (m_browser != nullptr) {
            m_browser->deleteLater();
            m_browser = nullptr;
        }
        for (const auto& url : m_browserQueue)
            failUrl(url, tr("浏览器启动失败"));
        m_browserQueue.clear();
        finishBatch();
    });

    m_browser->start();
}

void MapCoverLoader::acceptImage(const QString& url, const QByteArray& bytes, bool alreadyThumbnail)
{
    QImage image;
    if (!image.loadFromData(bytes)) {
        failUrl(url, tr("拿到的不是图片"));
        return;
    }

    QByteArray stored = bytes;
    if (!alreadyThumbnail) {
        if (image.width() > COVER_WIDTH)
            image = image.scaledToWidth(COVER_WIDTH, Qt::SmoothTransformation);
        QBuffer buffer(&stored);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "JPG", 85);
    }

    saveToDisk(url, stored);
    rememberImage(url, image);
    m_bad.remove(url);
    m_attempts.remove(url);
    ++m_batchOk;
    emit coverReady(url, image);
}

void MapCoverLoader::failUrl(const QString& url, const QString& reason)
{
    ++m_batchFailed;
    m_bad.insert(url);
    emit coverFailed(url, reason);
}

void MapCoverLoader::finishBatch()
{
    if (m_browser != nullptr) {
        m_browser->disconnect(this);
        m_browser->kill();
        m_browser->deleteLater();
        m_browser = nullptr;
    }
    if (!m_browserProfile.isEmpty()) {
        QDir(m_browserProfile).removeRecursively();
        m_browserProfile.clear();
    }

    m_busy = false;
    m_directQueue.clear();
    m_browserQueue.clear();
    emit batchFinished(m_batchOk, m_batchFailed);

    if (!m_queue.isEmpty())
        schedule();
}

}  // namespace maps
