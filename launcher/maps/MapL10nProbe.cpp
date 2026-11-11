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
#include "MapL10nProbe.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <zlib.h>

#include <io/stream_reader.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"

namespace maps::l10n {
namespace {

/// 这个模块没有 QObject 上下文，翻译走固定 context（与界面里的 tr() 不冲突）
QString tr(const char* text)
{
    return QCoreApplication::translate("maps::l10n", text);
}

// 数据包那一节要扫结构文件（.nbt），它们实现在「区块」一节，先声明
QByteArray inflateAny(const QByteArray& input);
void scanStructureBytes(const QByteArray& bytes, TextCollector& out);

// ────────────────────────── 文本载体白名单 ──────────────────────────

/**
 * 值本身是文本组件（或纯文本）的键。
 *
 * `Text1`–`Text4` 是 1.8/1.12 告示牌的键名。参考实现（mcsl 的 inject.py）漏了这一组，
 * 导致老图的告示牌**整体不计入**——而告示牌恰恰是老图最主要的文本载体
 * （实测 Ragecraft III 单个 region 文件就有 157 组）。这里补上。
 */
const QSet<QString>& textKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("CustomName"),          QStringLiteral("DisplayName"),
        QStringLiteral("minecraft:custom_name"), QStringLiteral("custom_name"),
        QStringLiteral("minecraft:item_name"), QStringLiteral("item_name"),
        QStringLiteral("text"),                QStringLiteral("title"),
        QStringLiteral("author"),              QStringLiteral("Text1"),
        QStringLiteral("Text2"),               QStringLiteral("Text3"),
        QStringLiteral("Text4"),
    };
    return keys;
}

/// 值是"文本列表"的键（告示牌消息、书写页、物品 lore）
const QSet<QString>& textListKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("messages"), QStringLiteral("Lore"), QStringLiteral("lore"),
        QStringLiteral("minecraft:lore"), QStringLiteral("pages"), QStringLiteral("raw"),
    };
    return keys;
}

/// 命令方块：文本藏在 SNBT 里的 JSON 片段中
const QString& commandKey()
{
    static const QString key = QStringLiteral("Command");
    return key;
}

/// 这些键即使在白名单里也不值得碰（标识符、材质名、音效名、计分项名）
const QSet<QString>& skipKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("id"),       QStringLiteral("Name"),   QStringLiteral("material"),
        QStringLiteral("item"),     QStringLiteral("sound"),  QStringLiteral("type"),
        QStringLiteral("EntityId"), QStringLiteral("Motive"), QStringLiteral("Status"),
        QStringLiteral("Lock"),     QStringLiteral("Pattern"),
    };
    return keys;
}

/**
 * 整棵剪掉的子树。
 *
 * 一个区块里绝大部分数据是方块状态数组（`Sections` 下每个 section 4096 个方块）
 * 与光照/高度图。实测不剪枝 10.6 ms/区块，剪完 1.5 ms/区块，命中数完全一致。
 */
const QSet<QString>& skipSubtrees()
{
    static const QSet<QString> keys = {
        QStringLiteral("Sections"),         QStringLiteral("Heightmaps"),
        QStringLiteral("Biomes"),           QStringLiteral("block_states"),
        QStringLiteral("Palette"),          QStringLiteral("BlockLight"),
        QStringLiteral("SkyLight"),         QStringLiteral("PostProcessing"),
        QStringLiteral("Lights"),           QStringLiteral("block_ticks"),
        QStringLiteral("fluid_ticks"),      QStringLiteral("ToBeTicked"),
        QStringLiteral("LiquidsToBeTicked"), QStringLiteral("Structures"),
        QStringLiteral("CarvingMasks"),     QStringLiteral("blending_data"),
        QStringLiteral("below_zero_retrogen"),
    };
    return keys;
}

// ────────────────────────── 散文判定 ──────────────────────────

bool hasLetters(const QString& text)
{
    static const QRegularExpression letter(QStringLiteral("[A-Za-z\u4e00-\u9fff]"));
    return letter.match(text).hasMatch();
}

/// 去掉 § 格式化码与私用区字形（位图图标），剩下的才是"字"
QString stripDecorations(const QString& text)
{
    static const QRegularExpression format(QStringLiteral("\u00a7."));
    static const QRegularExpression glyph(QStringLiteral("[\ue000-\uf8ff]"));
    QString out = text;
    out.remove(format);
    out.remove(glyph);
    return out.trimmed();
}

// ────────────────────────── 收集 ──────────────────────────

void addLiteral(const QString& text, TextCollector& out)
{
    if (!needsTranslation(text))
        return;
    out.texts[text] += 1;
    out.literalOccurrences += 1;
}

void collectFromJson(const QJsonValue& value, TextCollector& out, int depth)
{
    if (depth > 24)
        return;
    if (value.isArray()) {
        for (const auto& item : value.toArray())
            collectFromJson(item, out, depth + 1);
        return;
    }
    if (!value.isObject())
        return;

    const auto object = value.toObject();
    if (const auto translate = object.value(QStringLiteral("translate")); translate.isString()) {
        // 键化：文本在语言表里，这里只记引用，不算字面量
        out.keys.insert(translate.toString());
        out.refs += 1;
    }
    if (const auto text = object.value(QStringLiteral("text")); text.isString())
        addLiteral(text.toString(), out);

    if (object.contains(QLatin1String("extra")))
        collectFromJson(object.value(QLatin1String("extra")), out, depth + 1);
    if (object.contains(QLatin1String("with")))
        collectFromJson(object.value(QLatin1String("with")), out, depth + 1);
}

/// 一条 NBT 字符串：可能是组件对象、JSON 字符串、或裸文本
void collectFromRaw(const QString& raw, TextCollector& out)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return;

    if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            collectFromJson(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), out, 0);
            return;
        }
    }

    // 告示牌的整条消息是**带外层引号的 JSON 字符串**（`"you've had"`），
    // 单独 parse 会因为不是对象/数组而失败，包进数组再解
    if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"'))) {
        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(("[" + trimmed + "]").toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1 &&
            doc.array().at(0).isString()) {
            addLiteral(doc.array().at(0).toString(), out);
            return;
        }
    }

    addLiteral(raw, out);
}

/**
 * 命令方块：SNBT 里嵌着 JSON 片段。
 *
 * `{...}` 的朴素匹配够用——组件的 JSON 内部不含裸花括号（字符串里的会被转义）。
 * 地图作者会把单引号转义成 `\'`，那不是合法 JSON 转义，所以失败后要还原一次再试。
 *
 * 解不出来**不算错误**：命令里大量 `{id:"minecraft:stone",Count:1b}` 这种
 * 是 SNBT 的 NBT 前缀，本来就不是 JSON（Final Paradox 有 62 处）。
 * 把它计成"解析失败"只会让体检报告看起来像是坏了。
 */
void collectFromCommand(const QString& command, TextCollector& out)
{
    static const QRegularExpression blob(QStringLiteral("\\{[^{}]*\\}"));
    auto it = blob.globalMatch(command);
    while (it.hasNext()) {
        const QString candidate = it.next().captured(0);
        QJsonParseError error{};
        auto doc = QJsonDocument::fromJson(candidate.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            QString relaxed = candidate;
            relaxed.replace(QLatin1String("\\'"), QLatin1String("'"));
            doc = QJsonDocument::fromJson(relaxed.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
                continue;  // 不是 JSON（SNBT 的 NBT 前缀），跳过
        }
        collectFromJson(QJsonValue(doc.object()), out, 0);
    }
}

// ────────────────────────── NBT 遍历 ──────────────────────────

void walkTag(const nbt::tag& tag, TextCollector& out, int depth = 0)
{
    if (depth > 64)
        return;

    if (tag.get_type() == nbt::tag_type::Compound) {
        for (const auto& entry : tag.as<nbt::tag_compound>()) {
            const QString key = QString::fromStdString(entry.first);
            const nbt::value& value = entry.second;

            if (skipSubtrees().contains(key))
                continue;

            if (value.get_type() == nbt::tag_type::String) {
                if (key == commandKey()) {
                    collectFromCommand(QString::fromStdString(value.as<nbt::tag_string>().get()), out);
                } else if (textKeys().contains(key) && !skipKeys().contains(key)) {
                    collectFromRaw(QString::fromStdString(value.as<nbt::tag_string>().get()), out);
                }
                continue;
            }

            if (value.get_type() == nbt::tag_type::List && textListKeys().contains(key)) {
                for (const auto& item : value.as<nbt::tag_list>()) {
                    if (item.get_type() == nbt::tag_type::String)
                        collectFromRaw(QString::fromStdString(item.as<nbt::tag_string>().get()), out);
                }
                continue;
            }

            if (value.get_type() == nbt::tag_type::Compound || value.get_type() == nbt::tag_type::List)
                walkTag(value.get(), out, depth + 1);
        }
        return;
    }

    if (tag.get_type() == nbt::tag_type::List) {
        for (const auto& item : tag.as<nbt::tag_list>())
            walkTag(item.get(), out, depth + 1);
    }
}

// ────────────────────────── MCA 读取 ──────────────────────────

constexpr int SECTOR = 4096;

/**
 * 解压区块载荷。
 *
 * `32 + MAX_WBITS` 让 zlib 自己识别是 gzip 还是 zlib 头，一个函数通吃。
 * 没有用 libnbtplusplus 的 `izlibstream`：这个仓库构建时 `NBT_USE_ZLIB` 是关的，
 * 那些符号根本不存在（链接期才会发现）。项目本来就在用 zlib（`GZip.cpp`）。
 */
QByteArray inflateAny(const QByteArray& input)
{
    if (input.isEmpty())
        return {};

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, 32 + MAX_WBITS) != Z_OK)
        return {};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());

    QByteArray output;
    output.resize(int(std::max<qint64>(qint64(input.size()) * 3, 4096)));

    qint64 produced = 0;
    int status = Z_OK;
    for (;;) {
        if (produced == output.size())
            output.resize(int(qint64(output.size()) * 2));
        stream.next_out = reinterpret_cast<Bytef*>(output.data() + produced);
        stream.avail_out = uInt(qint64(output.size()) - produced);

        status = inflate(&stream, Z_NO_FLUSH);
        produced = qint64(output.size()) - qint64(stream.avail_out);
        if (status == Z_STREAM_END)
            break;
        if (status != Z_OK) {
            inflateEnd(&stream);
            return {};
        }
    }
    inflateEnd(&stream);

    output.resize(int(produced));
    return output;
}

struct RawChunk {
    quint8 compression = 2;
    QByteArray payload;
};

/**
 * 解析 region 文件头部的 1024 项位置表。
 *
 * 位置表越界的条目**静默跳过**，不算错误：地形生成到一半被中断的地图、
 * 或者被外部工具截断过的存档里很常见（Final Paradox 就有 62 个），
 * 参考实现也是这么处理的。只有"读到了但解不开"才算错误。
 */
QList<RawChunk> readChunkPayloads(const QString& path)
{
    QList<RawChunk> chunks;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return chunks;

    const QByteArray blob = file.readAll();
    if (blob.size() < 2 * SECTOR)
        return chunks;  // 0 字节或残缺：当作空 region

    for (int index = 0; index < 1024; ++index) {
        const auto* location = reinterpret_cast<const uchar*>(blob.constData()) + index * 4;
        const quint32 offset = (quint32(location[0]) << 16) | (quint32(location[1]) << 8) | quint32(location[2]);
        const quint8 sectors = location[3];
        if (offset == 0 || sectors == 0)
            continue;

        const qint64 start = qint64(offset) * SECTOR;
        if (start + 5 > blob.size())
            continue;

        const auto* head = reinterpret_cast<const uchar*>(blob.constData()) + start;
        const quint32 length =
            (quint32(head[0]) << 24) | (quint32(head[1]) << 16) | (quint32(head[2]) << 8) | quint32(head[3]);
        if (length < 1 || start + 4 + length > blob.size())
            continue;

        RawChunk chunk;
        chunk.compression = head[4];
        chunk.payload = blob.mid(int(start) + 5, int(length) - 1);
        chunks.append(chunk);
    }
    return chunks;
}

/// 区块压缩：1 = gzip、2 = zlib、3 = 不压缩
std::unique_ptr<nbt::tag_compound> parseNbtPayload(const QByteArray& payload, quint8 compression)
{
    QByteArray raw;
    if (compression == 1 || compression == 2) {
        raw = inflateAny(payload);
        if (raw.isEmpty())
            return nullptr;
    } else if (compression == 3) {
        raw = payload;
    } else {
        return nullptr;  // 未知压缩（例如 1.15 之前用过的 LZ4）直接放弃，不猜
    }

    std::istringstream stream(std::string(raw.constData(), size_t(raw.size())));
    return nbt::io::read_compound(stream).second;
}

// ────────────────────────── 资源包 ──────────────────────────

struct LangEntry {
    QString nameSpace;
    QString locale;
    QString path;  // 目录内相对路径，或 zip 条目名
};

bool splitLangPath(const QString& name, LangEntry* out)
{
    // assets/<ns>/lang/<code>.json —— 必须正好四段
    const QStringList parts = name.split(QLatin1Char('/'));
    if (parts.size() != 4 || parts.at(0) != QLatin1String("assets") || parts.at(2) != QLatin1String("lang"))
        return false;
    if (!parts.at(3).endsWith(QLatin1String(".json")))
        return false;
    out->nameSpace = parts.at(1);
    out->locale = parts.at(3).chopped(5);
    out->path = name;
    return true;
}

QByteArray readLangBytes(const QString& packPath, bool fromZip, const QString& entry)
{
    if (fromZip) {
        MMCZip::ArchiveReader reader(packPath);
        const auto data = reader.readFile(entry);
        return data ? *data : QByteArray();
    }
    QFile file(FS::PathCombine(packPath, entry));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/**
 * 列出资源包里所有 `assets/<ns>/lang/<code>.json`。
 *
 * 与 `scanPack` 里的枚举保持一致（那边为了在读不出来时补一条 note，保留了内联写法）。
 * 两处都走 `splitLangPath`，路径规则只有一份。
 */
QList<LangEntry> listLangEntries(const QString& packPath, bool fromZip)
{
    QList<LangEntry> entries;
    if (fromZip) {
        MMCZip::ArchiveReader reader(packPath);
        reader.parse([&entries](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            if (file->isFile()) {
                LangEntry entry;
                if (splitLangPath(file->filename(), &entry))
                    entries.append(entry);
            }
            return false;
        });
    } else {
        QDirIterator it(FS::PathCombine(packPath, QStringLiteral("assets")), QStringList{ "*.json" },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absolute = it.next();
            const QString relative = QDir(packPath).relativeFilePath(absolute);
            LangEntry entry;
            if (splitLangPath(QString(relative).replace('\\', '/'), &entry))
                entries.append(entry);
        }
    }
    return entries;
}

void scanPack(ProbeResult& result)
{
    const QString zipPath = FS::PathCombine(result.root, QStringLiteral("resources.zip"));
    const QString dirPath = FS::PathCombine(result.root, QStringLiteral("resources"));

    const bool fromZip = QFileInfo::exists(zipPath);
    if (!fromZip && !QFileInfo(dirPath).isDir())
        return;

    result.pack.found = true;
    result.pack.path = fromZip ? zipPath : dirPath;

    QList<LangEntry> entries;
    if (fromZip) {
        MMCZip::ArchiveReader reader(zipPath);
        const auto parsed = reader.parse([&entries](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            if (file->isFile()) {
                LangEntry entry;
                if (splitLangPath(file->filename(), &entry))
                    entries.append(entry);
            }
            return false;  // false = 继续读下一个条目（true 是"停止"）
        });
        if (!parsed) {
            result.notes.append(tr("世界自带的 resources.zip 读不出来：%1").arg(parsed.error()));
            return;
        }
    } else {
        QDirIterator it(FS::PathCombine(dirPath, QStringLiteral("assets")), QStringList{ "*.json" },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absolute = it.next();
            const QString relative = QDir(dirPath).relativeFilePath(absolute);
            LangEntry entry;
            if (splitLangPath(QString(relative).replace('\\', '/'), &entry))
                entries.append(entry);
        }
    }

    if (entries.isEmpty())
        return;

    // 只留 en_us / zh_cn 的完整表（算缺失要用），其余语言只统计键数
    QHash<QString, QHash<QString, int>> counts;                     // ns → code → 键数
    QHash<QString, QHash<QString, QHash<QString, QString>>> tables;  // ns → code → 表

    for (const auto& entry : entries) {
        const QByteArray bytes = readLangBytes(result.pack.path, fromZip, entry.path);
        if (bytes.isEmpty())
            continue;

        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const auto object = doc.object();
        counts[entry.nameSpace][entry.locale] = int(object.size());
        if (entry.locale == QLatin1String("en_us") || entry.locale == QLatin1String("zh_cn")) {
            QHash<QString, QString> table;
            table.reserve(object.size());
            for (auto it = object.begin(); it != object.end(); ++it)
                table.insert(it.key(), it.value().toString());
            tables[entry.nameSpace][entry.locale] = table;
        }
    }

    if (counts.isEmpty())
        return;

    // 取 en_us 键最多的命名空间（地图可能同时带自己的 ns 与覆盖 minecraft 的 ns）
    QString best;
    int bestSize = -1;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        const int size = it.value().value(QStringLiteral("en_us"), 0);
        if (size > bestSize) {
            bestSize = size;
            best = it.key();
        }
    }
    result.pack.namespaceName = best;
    result.pack.locales = counts.value(best);

    const auto en = tables.value(best).value(QStringLiteral("en_us"));
    const auto zh = tables.value(best).value(QStringLiteral("zh_cn"));
    result.pack.enKeys = int(en.size());
    result.pack.zhKeys = int(zh.size());

    for (auto it = en.begin(); it != en.end(); ++it) {
        const QString existing = zh.value(it.key());
        if (!existing.isEmpty() && existing != it.value())
            continue;  // 已有中文且不是照抄英文
        if (looksLikeProse(it.value()))
            ++result.pack.missingProse;
    }
}

// ────────────────────────── 数据包 ──────────────────────────

const QRegularExpression& translateRefRe()
{
    static const QRegularExpression re(QStringLiteral("\"translate\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    return re;
}

const QRegularExpression& textLiteralRe()
{
    static const QRegularExpression re(QStringLiteral("\"text\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    return re;
}

/// 把 JSON 字符串的转义体还原成真正的文本；还原不了就用原样
QString unescapeJsonBody(const QString& body)
{
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(("[\"" + body + "\"]").toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1)
        return doc.array().at(0).toString();
    return body;
}

void collectFromDatapackText(const QString& text, TextCollector& out)
{
    auto refs = translateRefRe().globalMatch(text);
    while (refs.hasNext()) {
        out.keys.insert(unescapeJsonBody(refs.next().captured(1)));
        out.refs += 1;
    }
    auto literals = textLiteralRe().globalMatch(text);
    while (literals.hasNext())
        addLiteral(unescapeJsonBody(literals.next().captured(1)), out);
}

void scanDatapacks(ProbeResult& result)
{
    const QString root = FS::PathCombine(result.root, QStringLiteral("datapacks"));
    if (!QFileInfo(root).isDir())
        return;

    QStringList archives;

    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
            archives.append(path);
            continue;
        }
        if (path.endsWith(QLatin1String(".nbt"), Qt::CaseInsensitive)) {
            // 结构文件：本身就是一个完整的 NBT，文本在 block_entities 里
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            ++result.datapackFiles;
            scanStructureBytes(file.readAll(), result.datapacks);
            continue;
        }
        if (!path.endsWith(QLatin1String(".mcfunction")) && !path.endsWith(QLatin1String(".json")))
            continue;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        ++result.datapackFiles;
        collectFromDatapackText(QString::fromUtf8(file.readAll()), result.datapacks);
    }

    // 数据包也可能打包成 zip（Torre de Piedra 的四个包全是 zip）
    for (const auto& archive : archives) {
        MMCZip::ArchiveReader reader(archive);
        reader.parse([&result](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            if (!file->isFile())
                return false;
            const QString name = file->filename();
            const bool isStructure = name.endsWith(QLatin1String(".nbt"));
            const bool isText = name.endsWith(QLatin1String(".mcfunction")) || name.endsWith(QLatin1String(".json"));
            if (!isStructure && !isText)
                return false;
            const auto data = file->readAll();
            if (!data)
                return false;
            ++result.datapackFiles;
            if (isStructure)
                scanStructureBytes(*data, result.datapacks);
            else
                collectFromDatapackText(QString::fromUtf8(*data), result.datapacks);
            return false;  // 继续读下一个条目
        });
    }
}

// ────────────────────────── 区块 ──────────────────────────

QStringList regionDirectories(const QString& root)
{
    return { FS::PathCombine(root, QStringLiteral("region")),
             FS::PathCombine(root, QStringLiteral("entities")),
             FS::PathCombine(root, QStringLiteral("DIM-1"), QStringLiteral("region")),
             FS::PathCombine(root, QStringLiteral("DIM1"), QStringLiteral("region")) };
}

/// 只数列目录，不读内容 —— 快检也要能告诉玩家"还有多少要扫"
int countRegionFiles(const QString& root)
{
    int count = 0;
    for (const auto& directory : regionDirectories(root)) {
        QDir dir(directory);
        if (dir.exists())
            count += int(dir.entryList(QStringList{ "*.mca" }, QDir::Files).size());
    }
    return count;
}

/**
 * 按**内容**遍历：任何字符串都拿去判一次，不看键名。
 *
 * 只给结构文件用。区块有稳定 schema，白名单更精确也更快；但结构文件里
 * 键名五花八门（1.8 的物品名在 `display.Name`，命名空间式的 `components` 又是另一套），
 * 白名单必然漏。噪声不用担心：`minecraft:stone` 这种标识符过不了散文判定。
 */
void walkStringsByContent(const nbt::tag& tag, TextCollector& out, int depth = 0)
{
    if (depth > 64)
        return;

    // 列表里的字符串元素也要接（结构文件的 `pages` / `messages` 就是这种）
    if (tag.get_type() == nbt::tag_type::String) {
        collectFromRaw(QString::fromStdString(tag.as<nbt::tag_string>().get()), out);
        return;
    }

    if (tag.get_type() == nbt::tag_type::Compound) {
        for (const auto& entry : tag.as<nbt::tag_compound>()) {
            const QString key = QString::fromStdString(entry.first);
            if (skipSubtrees().contains(key))
                continue;
            const nbt::value& value = entry.second;
            if (value.get_type() == nbt::tag_type::String) {
                // 命令方块要拆里面嵌的 JSON，整条当文本判是判不出东西的
                if (key == commandKey())
                    collectFromCommand(QString::fromStdString(value.as<nbt::tag_string>().get()), out);
                else
                    collectFromRaw(QString::fromStdString(value.as<nbt::tag_string>().get()), out);
            } else if (value.get_type() == nbt::tag_type::Compound || value.get_type() == nbt::tag_type::List) {
                walkStringsByContent(value.get(), out, depth + 1);
            }
        }
        return;
    }

    if (tag.get_type() == nbt::tag_type::List) {
        for (const auto& item : tag.as<nbt::tag_list>())
            walkStringsByContent(item.get(), out, depth + 1);
    }
}

/**
 * 结构文件（数据包里的 `.nbt`）：箱子、告示牌、讲台的内容存在这里，
 * 结构方块存下来的示意图全靠它。**这一类最容易漏**——Final Paradox 的
 * 19 个结构文件里藏着 716 处 translate 引用，占全图引用量的 2.5%。
 *
 * 结构文件一般 gzip 压缩，但也有工具存裸 NBT，所以先试解压、失败就按原样读。
 */
void scanStructureBytes(const QByteArray& bytes, TextCollector& out)
{
    if (bytes.isEmpty())
        return;
    QByteArray raw = inflateAny(bytes);
    if (raw.isEmpty())
        raw = bytes;

    try {
        std::istringstream stream(std::string(raw.constData(), size_t(raw.size())));
        const auto parsed = nbt::io::read_compound(stream);
        if (parsed.second != nullptr)
            walkStringsByContent(*parsed.second, out);
    } catch (const std::exception&) {
        // 结构文件坏了不影响结论：跳过，不计入错误（和位置表越界同理）
    }
}

/// data/*.dat 是 GZip 压缩的 NBT（level.dat、scoreboard.dat…）
void scanDataFile(const QString& path, TextCollector& out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = inflateAny(file.readAll());
    if (raw.isEmpty())
        return;
    std::istringstream stream(std::string(raw.constData(), size_t(raw.size())));
    const auto parsed = nbt::io::read_compound(stream);
    if (parsed.second != nullptr)
        walkTag(*parsed.second, out);
}

}  // namespace

QString modeLabel(Mode mode)
{
    switch (mode) {
        case Mode::Key:
            return tr("完整（资源包）");
        case Mode::Mixed:
            return tr("部分（资源包 + 存档）");
        case Mode::Injection:
            break;
    }
    return tr("需改写存档");
}

QString modeDescription(Mode mode)
{
    switch (mode) {
        case Mode::Key:
            return tr("文本都走语言表，产出资源包就够了，不动存档。");
        case Mode::Mixed:
            return tr("一部分文本在语言表里，另一部分硬编码在存档中，两边都要处理。");
        case Mode::Injection:
            break;
    }
    return tr("文本写在存档数据里，汉化必须改写存档副本。");
}

bool looksLikeProse(const QString& text)
{
    if (text.trimmed().size() < 3)
        return false;
    if (text.startsWith(QLatin1Char('/')))
        return false;  // 命令不是翻译单元

    const QString stripped = stripDecorations(text);
    if (stripped.size() < 3)
        return false;
    if (!hasLetters(stripped))
        return false;

    // 纯标识符（snake_case / 命名空间 / 路径）不是散文。
    // 只认**全小写**：游戏里真正的显示文本几乎都带大写或空格，
    // 而 `sapping_potion_buff` / `minecraft:stone` 这类不会。写成大小写都收
    // 会把 `Storage` 这种正常单词一起误杀（实测踩过）。
    static const QRegularExpression identifier(QStringLiteral("^[a-z0-9_.:/\\[\\]-]+$"));
    if (identifier.match(stripped).hasMatch())
        return false;

    // base64 皮肤数据、长串无空格的编码：翻了一定坏
    static const QRegularExpression encoded(QStringLiteral("^[A-Za-z0-9+/=]{24,}$"));
    if (encoded.match(stripped).hasMatch())
        return false;

    return true;
}

bool containsCjk(const QString& text)
{
    // 注意用 C++ 的 \u 转义（写成真实字符），不要写成 "[\\u4e00-\\u9fff]" ——
    // 双反斜杠会原样交给 PCRE2，而它不认 \u，正则直接非法、永远匹配不到
    static const QRegularExpression cjk(QStringLiteral("[\u4e00-\u9fff]"));
    return cjk.match(text).hasMatch();
}

bool isTextKey(const QString& key)
{
    return textKeys().contains(key);
}

bool isTextListKey(const QString& key)
{
    return textListKeys().contains(key);
}

bool isCommandKey(const QString& key)
{
    return key == commandKey();
}

bool isSkippedKey(const QString& key)
{
    return skipKeys().contains(key);
}

bool isSkippedSubtree(const QString& key)
{
    return skipSubtrees().contains(key);
}

bool needsTranslation(const QString& text)
{
    // 已经是中文了就不必再翻 —— 中文原创地图与"已经汉化过的版本"都走这条
    return looksLikeProse(text) && !containsCjk(text);
}

QStringList translateUnits(const QString& raw)
{
    TextCollector scratch;
    collectFromRaw(raw, scratch);
    QStringList units;
    units.reserve(scratch.texts.size());
    for (auto it = scratch.texts.begin(); it != scratch.texts.end(); ++it)
        units.append(it.key());
    return units;
}

LangTables readLangTables(const PackFacts& pack)
{
    LangTables tables;
    if (!pack.found || pack.path.isEmpty() || pack.namespaceName.isEmpty())
        return tables;

    const bool fromZip = pack.path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
    for (const auto& entry : listLangEntries(pack.path, fromZip)) {
        if (entry.nameSpace != pack.namespaceName)
            continue;
        if (entry.locale != QLatin1String("en_us") && entry.locale != QLatin1String("zh_cn"))
            continue;
        const QByteArray bytes = readLangBytes(pack.path, fromZip, entry.path);
        if (bytes.isEmpty())
            continue;

        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const auto object = doc.object();
        auto& target = entry.locale == QLatin1String("en_us") ? tables.en : tables.zh;
        target.reserve(object.size());
        for (auto it = object.begin(); it != object.end(); ++it)
            target.insert(it.key(), it.value().toString());
    }
    return tables;
}

double ProbeResult::keyRatio() const
{
    const int refs = datapacks.refs + regions.refs;
    const int literals = datapacks.literalOccurrences + regions.literalOccurrences;
    const int total = refs + literals;
    return total > 0 ? double(refs) / double(total) : 0.0;
}

Mode ProbeResult::mode() const
{
    // 与参考实现同一套阈值：两端实测 0.998 与 0.0985，中间地带很宽
    const double ratio = keyRatio();
    if (ratio > 0.8)
        return Mode::Key;
    if (ratio > 0.3)
        return Mode::Mixed;
    return Mode::Injection;
}

ProbeResult scanFast(const QString& worldRoot, const QString& mcVersion)
{
    ProbeResult result;
    result.root = worldRoot;
    result.mcVersion = mcVersion;

    if (!QFileInfo(worldRoot).isDir()) {
        result.error = tr("存档目录不存在：%1").arg(worldRoot);
        return result;
    }

    scanPack(result);
    scanDatapacks(result);
    result.regionFiles = countRegionFiles(worldRoot);

    if (!result.pack.found)
        result.notes.append(tr("这张图没有自带资源包（resources.zip / resources/），语言表路线用不上。"));
    else if (result.pack.enKeys > 0 && result.pack.zhKeys == 0)
        result.notes.append(tr("资源包里没有中文语言表，%1 条待译。").arg(result.pack.missingProse));

    return result;
}

bool scanRegions(ProbeResult& result, const std::function<bool(int done, int total)>& progress)
{
    QStringList files;
    for (const auto& directory : regionDirectories(result.root)) {
        QDir dir(directory);
        if (!dir.exists())
            continue;
        const auto names = dir.entryList(QStringList{ "*.mca" }, QDir::Files, QDir::Name);
        for (const auto& name : names)
            files.append(dir.absoluteFilePath(name));
    }

    QDir dataDir(FS::PathCombine(result.root, QStringLiteral("data")));
    QStringList dataFiles;
    if (dataDir.exists())
        for (const auto& name : dataDir.entryList(QStringList{ "*.dat" }, QDir::Files))
            dataFiles.append(dataDir.absoluteFilePath(name));

    const int total = int(files.size() + dataFiles.size());
    result.regionFiles = int(files.size());
    int done = 0;

    for (const auto& path : files) {
        for (const auto& chunk : readChunkPayloads(path)) {
            ++result.chunks;
            try {
                const auto root = parseNbtPayload(chunk.payload, chunk.compression);
                if (root != nullptr)
                    walkTag(*root, result.regions);
            } catch (const std::exception& e) {
                ++result.regions.errors;
                qWarning("l10n: chunk parse failed in %s (compression %d): %s",
                         qPrintable(QFileInfo(path).fileName()), int(chunk.compression), e.what());
            }
        }
        ++done;
        if (progress && !progress(done, total))
            return false;
    }

    for (const auto& path : dataFiles) {
        try {
            scanDataFile(path, result.regions);
        } catch (const std::exception& e) {
            ++result.regions.errors;
            qWarning("l10n: data file parse failed in %s: %s", qPrintable(QFileInfo(path).fileName()), e.what());
        }
        ++done;
        if (progress && !progress(done, total))
            return false;
    }

    result.regionsScanned = true;
    return true;
}

}  // namespace maps::l10n
