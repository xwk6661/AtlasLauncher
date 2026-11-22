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
#include "SeriesNames.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace maps::series {

namespace {

QHash<QString, detail::Entry> loadCodesFrom(const QString& path)
{
    QHash<QString, detail::Entry> out;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return out;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return out;

    const auto codes = doc.object().value(QStringLiteral("codes")).toObject();
    for (auto it = codes.begin(); it != codes.end(); ++it) {
        const auto object = it.value().toObject();
        detail::Entry entry;
        entry.name = object.value(QStringLiteral("name")).toString().trimmed();
        entry.shortName = object.value(QStringLiteral("short")).toString().trimmed();
        if (entry.name.isEmpty())
            continue;
        out.insert(it.key().trimmed().toUpper(), entry);
    }
    return out;
}

/// 随包表。只读一次：这张表在进程生命周期内不会变。
const QHash<QString, detail::Entry>& codes()
{
    static const QHash<QString, detail::Entry> table = loadCodesFrom(QStringLiteral(":/maps/series.json"));
    return table;
}

/// `03` → `3`：站上的编号是 `3`，标签里补了零（`SP03`）不该带进界面
QString normalizedNumber(QString digits)
{
    while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
        digits.remove(0, 1);
    return digits;
}

}  // namespace

detail::Entry entryForCode(const QString& code)
{
    return codes().value(code.trimmed().toUpper());
}

QString codeForName(const QString& name)
{
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return {};

    for (auto it = codes().constBegin(); it != codes().constEnd(); ++it) {
        if (it.value().name.compare(wanted, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    return {};
}

QString abbrevFor(const QString& seriesName, int number)
{
    const QString wanted = seriesName.trimmed();
    if (wanted.isEmpty())
        return {};

    // 期号缺省按 1：站上只在部分图上标了 series_num（`[US]` 与 `[US9]` 同属
    // Untold Stories），缺号时不写数字会让同一系列出现两种写法。
    const int issue = number > 0 ? number : 1;

    const QString known = codeForName(wanted);
    if (!known.isEmpty())
        return known + QString::number(issue);

    // 表里没有：取首字母。虚词（the / of / and …）跳过，否则 "City of Nations"
    // 会变成 "CoN"；但如果跳完只剩一个词，就把开头的虚词也算上 ——
    // "The Pantheon" 要的是 "TP"，不是 "P"。
    static const QStringList stopWords = { QStringLiteral("the"), QStringLiteral("of"), QStringLiteral("and"),
                                           QStringLiteral("a"), QStringLiteral("an") };
    QStringList allInitials;
    QStringList significantInitials;
    const auto words = wanted.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const auto& word : words) {
        const QString initial = QString(word.at(0)).toUpper();
        allInitials << initial;
        if (!stopWords.contains(word.toLower()))
            significantInitials << initial;
    }
    const QStringList& chosen = significantInitials.size() >= 2 ? significantInitials : allInitials;
    if (chosen.isEmpty())
        return {};
    return chosen.join(QString()) + QString::number(issue);
}

QString displayLabel(const QString& tag)
{
    static const QRegularExpression pattern(QStringLiteral("^([A-Za-z]+)(\\d+)$"));
    const auto match = pattern.match(tag.trimmed());
    if (!match.hasMatch())
        return tag;

    const auto entry = entryForCode(match.captured(1));
    if (entry.name.isEmpty())
        return tag;  // 表里没有：原样显示，不猜

    const QString name = entry.shortName.isEmpty() ? entry.name : entry.shortName;
    return QStringLiteral("%1 %2").arg(name, normalizedNumber(match.captured(2)));
}

}  // namespace maps::series
