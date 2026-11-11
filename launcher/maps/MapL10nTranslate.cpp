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
#include "MapL10nTranslate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace maps::l10n {
namespace {

/**
 * 给模型的硬约束。故意写得很具体：
 * 地图文本里有 § 格式化码、私用区字形图标、`%s` 占位符，翻错了要么丢格式、要么崩排版。
 */
QString systemPrompt()
{
    return QStringLiteral(
        "你是 Minecraft 地图本地化译者，把地图里的英文（偶尔夹杂西班牙语等）翻成简体中文。\n"
        "硬性规则：\n"
        "1. 保留所有 § 格式化码（如 §a §l §o §r）及其位置，不要翻译、不要新增、不要删除。\n"
        "2. 保留私用区字形字符（U+E000–U+F8FF，地图自绘的图标），原样搬过去。\n"
        "3. 保留 %s %d %1$s 这类占位符、命令、URL、以及 #RRGGBB 颜色值。\n"
        "4. Minecraft 专有名词用官方简体中文译名：Creeper=苦力怕、Blaze=烈焰人、"
        "Nether=下界、End=末地、Ender Dragon=末影龙、Zombie=僵尸。\n"
        "5. 告示牌、物品名这类短文本要短，宁可意译也不要超长。\n"
        "6. 无法确定怎么译的，就保留原文，不要编。\n"
        "只输出 JSON，格式：{\"items\":[{\"i\":<编号>,\"zh\":\"<译文>\"}]}，"
        "不要代码围栏、不要解释。");
}

QString userPayload(const QList<QPair<int, QString>>& batch)
{
    QJsonArray array;
    for (const auto& pair : batch) {
        QJsonObject object;
        object.insert(QStringLiteral("i"), pair.first);
        object.insert(QStringLiteral("en"), pair.second);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

/// 从一段可能带客套话或代码围栏的文本里抠出第一个完整 JSON 对象
QByteArray extractJsonObject(const QByteArray& raw)
{
    int start = raw.indexOf('{');
    int end = raw.lastIndexOf('}');
    if (start < 0 || end <= start)
        return {};
    return raw.mid(start, end - start + 1);
}

}  // namespace

MapL10nTranslateTask::MapL10nTranslateTask(QString jsonPath, QString baseUrl, QString apiKey, QString model,
                                           TranslateLimits limits)
    : m_jsonPath(std::move(jsonPath))
    , m_baseUrl(std::move(baseUrl))
    , m_apiKey(std::move(apiKey))
    , m_model(std::move(model))
    , m_limits(limits)
{
    setAbortable(true);
    setAbortButtonText(tr("停止翻译"));
}

MapL10nTranslateTask::~MapL10nTranslateTask()
{
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

bool MapL10nTranslateTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_aborted = true;
    if (m_reply != nullptr)
        m_reply->abort();
    return true;
}

void MapL10nTranslateTask::executeTask()
{
    setStatus(tr("正在读取待译清单…"));

    QFile file(m_jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emitFailed(tr("打不开 %1").arg(m_jsonPath));
        return;
    }
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emitFailed(tr("strings.json 解析失败：%1").arg(error.errorString()));
        return;
    }
    m_document = document.object();

    loadPendingItems();
    if (m_pending.isEmpty()) {
        emitFailed(tr("清单里没有待译条目（可能已经全部翻好了）。"));
        return;
    }

    m_total = int(m_pending.size());
    setProgress(0, m_total);
    setStatus(tr("开始翻译 %1 条…").arg(m_total));

    // 这一句之后就不再阻塞了，剩下的全靠回调
    sendNextBatch();
}

void MapL10nTranslateTask::loadPendingItems()
{
    const auto items = m_document.value(QStringLiteral("items")).toArray();
    for (int i = 0; i < items.size(); ++i) {
        const auto object = items.at(i).toObject();
        const QString source = object.value(QStringLiteral("en")).toString();
        const QString existing = object.value(QStringLiteral("zh")).toString();
        if (source.isEmpty() || !existing.isEmpty())
            continue;  // 已经有译文的不动
        m_pending.append({ i, source });
    }
}

void MapL10nTranslateTask::sendNextBatch()
{
    if (m_aborted) {
        emitAborted();
        return;
    }
    if (m_pending.isEmpty()) {
        writeBack();
        return;
    }

    // 一批：条数或字符数到上限就切
    m_batch.clear();
    int chars = 0;
    while (!m_pending.isEmpty()) {
        const auto& next = m_pending.first();
        const int cost = int(next.source.size());
        if (!m_batch.isEmpty() && (m_batch.size() >= m_limits.maxItems || chars + cost > m_limits.maxChars))
            break;
        chars += cost;
        m_batch.append(next);
        m_pending.removeFirst();
    }

    QList<QPair<int, QString>> payload;
    payload.reserve(m_batch.size());
    for (const auto& item : m_batch)
        payload.append({ item.index, item.source });

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), userPayload(payload));

    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), systemPrompt());
    messages.append(system);
    messages.append(message);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("temperature"), 0.2);
    body.insert(QStringLiteral("messages"), messages);

    QString base = m_baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkRequest request(QUrl(base + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &MapL10nTranslateTask::onReplyFinished);
}

void MapL10nTranslateTask::onReplyFinished()
{
    if (m_reply == nullptr)
        return;

    const auto reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (m_aborted) {
            emitAborted();
            return;
        }
        finishWithError(tr("请求失败：%1").arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();

    // 先摘出 choices[0].message.content，再解里面那层 JSON
    const auto envelope = QJsonDocument::fromJson(body).object();
    const auto choices = envelope.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        finishWithError(tr("接口没有返回 choices：%1").arg(QString::fromUtf8(body.left(200))));
        return;
    }
    const QString content =
        choices.at(0).toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    if (content.isEmpty()) {
        finishWithError(tr("接口返回了空的 content。"));
        return;
    }

    const auto parsed = parseResponse(content.toUtf8());

    int batchOk = 0;
    for (const auto& item : m_batch) {
        const QString zh = parsed.value(item.index);
        if (zh.trimmed().isEmpty()) {
            ++m_failed;
            continue;
        }
        m_results.insert(item.index, zh);
        ++batchOk;
    }
    m_translated += batchOk;
    m_done += int(m_batch.size());

    setProgress(m_done, m_total);
    setStatus(tr("已翻译 %1 / %2 条%3").arg(m_done).arg(m_total).arg(m_failed > 0
                                                                        ? tr("（失败 %1）").arg(m_failed)
                                                                        : QString()));

    sendNextBatch();
}

void MapL10nTranslateTask::finishWithError(const QString& message)
{
    // 已经翻好的部分照样写回去：几百条的结果不该因为最后一批失败就丢掉
    if (!m_results.isEmpty())
        writeBack();
    emitFailed(message);
}

void MapL10nTranslateTask::writeBack()
{
    auto items = m_document.value(QStringLiteral("items")).toArray();
    for (auto it = m_results.begin(); it != m_results.end(); ++it) {
        if (it.key() < 0 || it.key() >= items.size())
            continue;
        auto object = items.at(it.key()).toObject();
        object.insert(QStringLiteral("zh"), it.value());
        items.replace(it.key(), object);
    }
    m_document.insert(QStringLiteral("items"), items);

    QSaveFile file(m_jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emitFailed(tr("写不回 %1").arg(m_jsonPath));
        return;
    }
    file.write(QJsonDocument(m_document).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emitFailed(tr("写不回 %1").arg(m_jsonPath));
        return;
    }

    emitSucceeded();
}

QHash<int, QString> MapL10nTranslateTask::parseResponse(const QByteArray& body)
{
    QHash<int, QString> out;
    const QByteArray json = extractJsonObject(body);
    if (json.isEmpty())
        return out;

    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return out;

    const auto items = document.object().value(QStringLiteral("items")).toArray();
    for (const auto& value : items) {
        const auto object = value.toObject();
        const int index = object.value(QStringLiteral("i")).toInt(-1);
        const QString zh = object.value(QStringLiteral("zh")).toString();
        if (index >= 0 && !zh.isEmpty())
            out.insert(index, zh);
    }
    return out;
}

}  // namespace maps::l10n
