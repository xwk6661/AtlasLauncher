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
#include "MapL10nExport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace maps::l10n {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("maps::l10n", text);
}

/// CSV 字段：含逗号/引号/换行就整体加引号，内部引号翻倍
QString csvField(const QString& value)
{
    QString out = value;
    const bool needsQuote =
        out.contains(QLatin1Char(',')) || out.contains(QLatin1Char('"')) ||
        out.contains(QLatin1Char('\n')) || out.contains(QLatin1Char('\r'));
    out.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return needsQuote ? QStringLiteral("\"%1\"").arg(out) : out;
}

/// 引用了这张图里哪些语言表键（用来给"优先级"排序，不用于过滤）
QSet<QString> referencedKeys(const ProbeResult& result)
{
    QSet<QString> keys = result.datapacks.keys;
    keys.unite(result.regions.keys);
    return keys;
}

QList<ExportItem> collectFromPack(const ProbeResult& result, const QSet<QString>& referenced, int* out)
{
    QList<ExportItem> items;
    if (!result.pack.found || result.pack.enKeys == 0)
        return items;

    const auto tables = readLangTables(result.pack);
    for (auto it = tables.en.begin(); it != tables.en.end(); ++it) {
        const QString existing = tables.zh.value(it.key());
        if (!existing.isEmpty() && existing != it.value())
            continue;  // 已经有像样的中文
        // en 里本来就写着中文的是中文原创地图（或作者把中文放进了 en_us），不必再翻
        if (!needsTranslation(it.value()))
            continue;

        ExportItem item;
        item.kind = QStringLiteral("key");
        item.key = it.key();
        item.source = it.value();
        item.count = 1;
        item.existing = existing;
        item.referenced = referenced.contains(it.key());
        items.append(item);
        ++(*out);
    }
    return items;
}

QList<ExportItem> collectFromWorld(const ProbeResult& result, int* out)
{
    // 同一句原文可能既在数据包里、又在区块里 —— 按原文合并，出现次数相加
    QHash<QString, int> counts = result.datapacks.texts;
    for (auto it = result.regions.texts.begin(); it != result.regions.texts.end(); ++it)
        counts[it.key()] += it.value();

    QList<ExportItem> items;
    items.reserve(counts.size());
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        ExportItem item;
        item.kind = QStringLiteral("literal");
        item.source = it.key();
        item.count = it.value();
        items.append(item);
        ++(*out);
    }
    return items;
}

QString modeId(Mode mode)
{
    switch (mode) {
        case Mode::Key:
            return QStringLiteral("key");
        case Mode::Mixed:
            return QStringLiteral("mixed");
        case Mode::Injection:
            break;
    }
    return QStringLiteral("injection");
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size())
        return false;
    return file.commit();
}

}  // namespace

ExportResult exportStrings(const ProbeResult& result, const QString& outputDirectory)
{
    ExportResult out;
    out.directory = outputDirectory;
    out.regionsScanned = result.regionsScanned;

    if (!result.isValid()) {
        out.error = result.error.isEmpty() ? tr("体检结果无效，先跑一次体检。") : result.error;
        return out;
    }

    const QSet<QString> referenced = referencedKeys(result);
    QList<ExportItem> items = collectFromPack(result, referenced, &out.fromPack);
    items += collectFromWorld(result, &out.fromWorld);
    out.total = int(items.size());

    if (items.isEmpty()) {
        out.error = tr("没有可翻译的文本 —— 这张图可能已经全中文了，或者文字都在图片里。");
        return out;
    }

    // 有引用的排前面（多半是玩家真会看到的），再按出现次数、原文排序，保证每次导出顺序一致
    std::sort(items.begin(), items.end(), [](const ExportItem& a, const ExportItem& b) {
        if (a.referenced != b.referenced)
            return a.referenced;
        if (a.count != b.count)
            return a.count > b.count;
        return a.source < b.source;
    });

    if (!QDir().mkpath(outputDirectory)) {
        out.error = tr("无法创建目录：%1").arg(outputDirectory);
        return out;
    }

    // ── strings.json ──
    QJsonArray array;
    for (const auto& item : items) {
        QJsonObject object;
        object.insert(QStringLiteral("kind"), item.kind);
        if (!item.key.isEmpty())
            object.insert(QStringLiteral("key"), item.key);
        object.insert(QStringLiteral("en"), item.source);
        object.insert(QStringLiteral("zh"), QString());
        object.insert(QStringLiteral("count"), item.count);
        object.insert(QStringLiteral("referenced"), item.referenced);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("world"), result.root);
    root.insert(QStringLiteral("mcVersion"), result.mcVersion);
    root.insert(QStringLiteral("mode"), modeId(result.mode()));
    root.insert(QStringLiteral("keyRatio"), result.keyRatio());
    root.insert(QStringLiteral("regionsScanned"), result.regionsScanned);

    QJsonObject counts;
    counts.insert(QStringLiteral("total"), out.total);
    counts.insert(QStringLiteral("fromPack"), out.fromPack);
    counts.insert(QStringLiteral("fromWorld"), out.fromWorld);
    root.insert(QStringLiteral("counts"), counts);
    root.insert(QStringLiteral("items"), array);

    out.jsonPath = QDir(outputDirectory).filePath(QStringLiteral("strings.json"));
    if (!writeFile(out.jsonPath, QJsonDocument(root).toJson(QJsonDocument::Indented))) {
        out.error = tr("写不了 %1").arg(out.jsonPath);
        return out;
    }

    // ── strings.csv（带 BOM，Excel 打开中文才不乱码）──
    QString csv;
    csv += QStringLiteral("id,kind,key,en,count,referenced,zh\n");
    int id = 1;
    for (const auto& item : items) {
        csv += QString::number(id++);
        csv += QLatin1Char(',') + csvField(item.kind);
        csv += QLatin1Char(',') + csvField(item.key);
        csv += QLatin1Char(',') + csvField(item.source);
        csv += QLatin1Char(',') + QString::number(item.count);
        csv += QLatin1Char(',') + QString(item.referenced ? QStringLiteral("1") : QStringLiteral("0"));
        csv += QLatin1Char(',') + csvField(QString());
        csv += QLatin1Char('\n');
    }

    // BOM 必须是原始字节，不能先塞进 QString —— 那样会被当成三个 Latin-1 字符再编码一次
    QByteArray csvBytes("\xEF\xBB\xBF", 3);
    csvBytes += csv.toUtf8();

    out.csvPath = QDir(outputDirectory).filePath(QStringLiteral("strings.csv"));
    if (!writeFile(out.csvPath, csvBytes)) {
        out.error = tr("写不了 %1").arg(out.csvPath);
        return out;
    }

    return out;
}

}  // namespace maps::l10n
