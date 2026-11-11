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

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QString>

#include "tasks/Task.h"

class QNetworkReply;

namespace maps::l10n {

/// 一次翻译请求里最多塞多少条 / 多少字符（太大的请求容易被网关掐）
struct TranslateLimits {
    int maxItems = 40;
    int maxChars = 3000;
};

/**
 * 用 OpenAI 兼容接口把 `strings.json` 里的待译条目翻成中文，就地写回。
 *
 * 三个刻意的选择：
 *   1. **不占线程**。全程 `QNetworkAccessManager` 异步回调，`executeTask()` 发完第一个
 *      请求就返回 —— `Task::start()` 是同步执行 `executeTask()` 的，在这里干活会卡界面。
 *   2. **串行发**。一批回来再发下一批，别把并发和限流问题一起引进来。
 *   3. **失败不写错**。某条没翻出来就留空（走原文），宁可不翻也不写半句。
 *
 * 只改 `zh` 为空的条目；已经有译文的原样保留。
 */
class MapL10nTranslateTask : public Task {
    Q_OBJECT

   public:
    MapL10nTranslateTask(QString jsonPath, QString baseUrl, QString apiKey, QString model,
                         TranslateLimits limits = {});
    ~MapL10nTranslateTask() override;

    void executeTask() override;
    bool abort() override;

    /// 成功写入的条数 / 失败（留空）的条数
    int translated() const { return m_translated; }
    int failed() const { return m_failed; }

    /**
     * 从模型输出里抠出 `{"items":[{"i":1,"zh":"…"}]}`。
     *
     * 单独拆出来是为了能单测：模型偶尔会裹上 ```json 围栏、
     * 或者在前后多说一句客套话，这都得容忍。
     */
    static QHash<int, QString> parseResponse(const QByteArray& body);

   private slots:
    void onReplyFinished();

   private:
    void loadPendingItems();
    void sendNextBatch();
    void finishWithError(const QString& message);
    void writeBack();

    QString m_jsonPath;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    TranslateLimits m_limits;

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;

    struct BatchItem {
        int index = 0;  // 在 items 数组里的下标
        QString source;
    };
    QList<BatchItem> m_pending;
    QList<BatchItem> m_batch;
    QHash<int, QString> m_results;  // items 下标 → 译文

    int m_total = 0;
    int m_done = 0;
    int m_translated = 0;
    int m_failed = 0;
    bool m_aborted = false;
    /// 原始文档与条目，写回时保持其它字段不变
    QJsonObject m_document;
};

}  // namespace maps::l10n
