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
#include "MapNotes.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

#include "FileSystem.h"

namespace maps {

MapNotes MapNotes::load(const QString& instanceRoot)
{
    MapNotes notes;

    QFile file(FS::PathCombine(instanceRoot, fileName()));
    if (!file.open(QIODevice::ReadOnly))
        return notes;  // 还没写过笔记：返回空对象，不是错误

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return notes;  // 损坏就当没有：笔记丢了也不能拦住实例加载

    const auto root = document.object();
    const auto rating = root.value(QStringLiteral("rating"));
    if (rating.isDouble()) {
        const int value = static_cast<int>(rating.toDouble());
        if (value >= 1 && value <= MAX_RATING)
            notes.rating = value;
    }
    notes.note = root.value(QStringLiteral("note")).toString().left(MAX_NOTE);
    notes.alias = root.value(QStringLiteral("alias")).toString().left(MAX_ALIAS);

    const QString updated = root.value(QStringLiteral("updatedAt")).toString();
    if (!updated.isEmpty())
        notes.updatedAt = QDateTime::fromString(updated, Qt::ISODate);
    return notes;
}

bool MapNotes::save(const QString& instanceRoot, QString* error) const
{
    const MapNotes clean = normalized();

    QJsonObject root;
    // 未评分写 null 而不是 0：0 在站方/统计里是"打了零分"，语义不一样
    root.insert(QStringLiteral("rating"), clean.rating > 0 ? QJsonValue(clean.rating) : QJsonValue());
    root.insert(QStringLiteral("note"), clean.note);
    root.insert(QStringLiteral("alias"), clean.alias);
    root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile file(FS::PathCombine(instanceRoot, fileName()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr)
            *error = QStringLiteral("无法写入 notes.json：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

MapNotes MapNotes::normalized() const
{
    MapNotes clean;
    clean.rating = std::clamp(rating, 0, MAX_RATING);
    clean.note = note.left(MAX_NOTE);
    // 中文名里的连续空白折叠成一个，避免卡片被撑爆
    QString aliasText = alias;
    aliasText.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    clean.alias = aliasText.trimmed().left(MAX_ALIAS);
    clean.updatedAt = updatedAt;
    return clean;
}

bool MapNotes::isEmpty() const
{
    return rating <= 0 && note.trimmed().isEmpty() && alias.trimmed().isEmpty();
}

QString MapNotes::ratingText() const
{
    if (rating <= 0)
        return {};
    // 10 → 5.0 星，7 → 3.5 星
    return QStringLiteral("%1 / 5").arg(rating / 2.0, 0, 'f', 1);
}

}  // namespace maps
