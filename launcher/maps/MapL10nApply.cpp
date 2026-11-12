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
#include "MapL10nApply.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThreadPool>
#include <QtConcurrent>

#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include <zlib.h>

#include <cstring>
#include <memory>
#include <sstream>
#include <vector>

#include "FileSystem.h"
#include "MapL10nProbe.h"
#include "MMCZip.h"
#include "archive/ArchiveWriter.h"

namespace maps::l10n {
namespace {

constexpr int SECTOR = 4096;
constexpr int HEADER = SECTOR * 2;

QString tr(const char* text)
{
    return QCoreApplication::translate("maps::l10n", text);
}

// ────────────────────────── 压缩 ──────────────────────────

/// windowBits：15 = zlib，15+16 = gzip
QByteArray deflateWith(const QByteArray& input, int windowBits)
{
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};

    QByteArray output(int(deflateBound(&stream, uLong(input.size()))) + 64, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = uInt(output.size());

    const int status = deflate(&stream, Z_FINISH);
    const int produced = int(output.size()) - int(stream.avail_out);
    deflateEnd(&stream);

    if (status != Z_STREAM_END)
        return {};
    output.resize(produced);
    return output;
}

QByteArray compressPayload(const QByteArray& raw, quint8 compression)
{
    switch (compression) {
        case 1:
            return deflateWith(raw, 15 + 16);
        case 2:
            return deflateWith(raw, 15);
        case 3:
            return raw;
        default:
            return {};  // 未知压缩：放弃，保留原文
    }
}

QByteArray inflatePayload(const QByteArray& input)
{
    if (input.isEmpty())
        return {};
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, 32 + 15) != Z_OK)
        return {};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());

    QByteArray output(int(qint64(input.size()) * 3) + 4096, '\0');
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

// ────────────────────────── NBT 序列化 ──────────────────────────

QByteArray serializeNbt(const std::string& name, const nbt::tag_compound& root)
{
    std::ostringstream stream(std::ios::out | std::ios::binary);
    nbt::io::write_tag(name, root, stream);
    const std::string bytes = stream.str();
    return QByteArray(bytes.data(), int(bytes.size()));
}

// ────────────────────────── 文本替换 ──────────────────────────

bool patchJson(QJsonValue& node, const QHash<QString, QString>& mapping)
{
    if (node.isArray()) {
        auto array = node.toArray();
        bool changed = false;
        for (int i = 0; i < array.size(); ++i) {
            auto element = array.at(i);
            if (patchJson(element, mapping)) {
                array.replace(i, element);
                changed = true;
            }
        }
        if (changed)
            node = array;
        return changed;
    }
    if (!node.isObject())
        return false;

    auto object = node.toObject();
    bool changed = false;

    if (const auto text = object.value(QStringLiteral("text")); text.isString()) {
        const QString source = text.toString();
        if (mapping.contains(source)) {
            object.insert(QStringLiteral("text"), mapping.value(source));
            changed = true;
        }
    }
    for (const auto* key : { "extra", "with" }) {
        const QString name = QLatin1String(key);
        if (!object.contains(name))
            continue;
        auto child = object.value(name);
        if (patchJson(child, mapping)) {
            object.insert(name, child);
            changed = true;
        }
    }
    if (changed)
        node = object;
    return changed;
}

/// 一条 NBT 字符串：组件对象 / JSON 字符串 / 裸文本，三条路都要能改
bool patchRaw(QString& value, const QHash<QString, QString>& mapping)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            QJsonValue root = document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
            if (patchJson(root, mapping)) {
                value = QString::fromUtf8(
                    QJsonDocument(root.isArray() ? QJsonDocument(root.toArray()) : QJsonDocument(root.toObject()))
                        .toJson(QJsonDocument::Compact));
                return true;
            }
            return false;
        }
    }

    // 告示牌整条消息是带外层引号的 JSON 字符串
    if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"'))) {
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(("[" + trimmed + "]").toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isArray() && document.array().size() == 1 &&
            document.array().at(0).isString()) {
            const QString inner = document.array().at(0).toString();
            if (!mapping.contains(inner))
                return false;
            QJsonArray wrapper;
            wrapper.append(mapping.value(inner));
            const QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
            if (encoded.size() < 2)
                return false;
            value = QString::fromUtf8(encoded.mid(1, encoded.size() - 2));  // 去掉 [] 包回去
            return true;
        }
    }

    if (!mapping.contains(value))
        return false;
    value = mapping.value(value);
    return true;
}

/// 命令方块：`{...}` 片段里的组件
bool patchCommand(QString& command, const QHash<QString, QString>& mapping)
{
    static const QRegularExpression blob(QStringLiteral("\\{[^{}]*\\}"));
    QString result;
    int last = 0;
    bool changed = false;

    auto it = blob.globalMatch(command);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString candidate = match.captured(0);
        QJsonParseError error{};
        auto document = QJsonDocument::fromJson(candidate.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            QString relaxed = candidate;
            relaxed.replace(QLatin1String("\\'"), QLatin1String("'"));
            document = QJsonDocument::fromJson(relaxed.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
                continue;
        }
        QJsonValue value = document.object();
        if (!patchJson(value, mapping))
            continue;

        result += command.mid(last, match.capturedStart() - last);
        result += QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        last = match.capturedEnd();
        changed = true;
    }

    if (!changed)
        return false;
    result += command.mid(last);
    command = result;
    return true;
}

// ────────────────────────── 树遍历：改 / 清 ──────────────────────────

void patchCompound(nbt::tag_compound& compound, const QHash<QString, QString>& mapping, ApplyStats& stats);

void patchValue(nbt::value& slot, const QString& key, const QHash<QString, QString>& mapping, ApplyStats& stats)
{
    if (slot.get_type() == nbt::tag_type::String) {
        QString text = QString::fromStdString(slot.as<nbt::tag_string>().get());
        bool changed = false;
        if (isCommandKey(key))
            changed = patchCommand(text, mapping);
        else if (isTextKey(key) && !isSkippedKey(key))
            changed = patchRaw(text, mapping);

        if (changed) {
            slot = nbt::value(nbt::make_unique<nbt::tag_string>(text.toStdString()));
            ++stats.replaced;
        }
        return;
    }

    if (slot.get_type() == nbt::tag_type::List && isTextListKey(key)) {
        auto& list = slot.as<nbt::tag_list>();
        if (list.el_type() != nbt::tag_type::String)
            return;
        for (size_t i = 0; i < list.size(); ++i) {
            QString text = QString::fromStdString(list.at(i).as<nbt::tag_string>().get());
            if (!patchRaw(text, mapping))
                continue;
            list.set(i, nbt::value(nbt::make_unique<nbt::tag_string>(text.toStdString())));
            ++stats.replaced;
        }
        return;
    }

    if (slot.get_type() == nbt::tag_type::Compound) {
        patchCompound(slot.as<nbt::tag_compound>(), mapping, stats);
        return;
    }
    if (slot.get_type() == nbt::tag_type::List) {
        auto& list = slot.as<nbt::tag_list>();
        for (size_t i = 0; i < list.size(); ++i) {
            nbt::value& item = list.at(i);
            if (item.get_type() == nbt::tag_type::Compound)
                patchCompound(item.as<nbt::tag_compound>(), mapping, stats);
            else if (item.get_type() == nbt::tag_type::List)
                patchValue(item, QString(), mapping, stats);
        }
    }
}

void patchCompound(nbt::tag_compound& compound, const QHash<QString, QString>& mapping, ApplyStats& stats)
{
    for (auto& entry : compound) {
        const QString key = QString::fromStdString(entry.first);
        if (isSkippedSubtree(key))
            continue;
        patchValue(entry.second, key, mapping, stats);
    }
}

/// 结构比对用：把文本字段清空，剩下的应当完全一致
void blankValue(nbt::value& slot, const QString& key)
{
    if (slot.get_type() == nbt::tag_type::String) {
        const bool carrier = isCommandKey(key) || (isTextKey(key) && !isSkippedKey(key));
        if (carrier)
            slot = nbt::value(nbt::make_unique<nbt::tag_string>(std::string()));
        return;
    }
    if (slot.get_type() == nbt::tag_type::List && isTextListKey(key)) {
        auto& list = slot.as<nbt::tag_list>();
        if (list.el_type() != nbt::tag_type::String)
            return;
        for (size_t i = 0; i < list.size(); ++i)
            list.set(i, nbt::value(nbt::make_unique<nbt::tag_string>(std::string())));
        return;
    }
    if (slot.get_type() == nbt::tag_type::Compound) {
        for (auto& entry : slot.as<nbt::tag_compound>()) {
            const QString childKey = QString::fromStdString(entry.first);
            if (isSkippedSubtree(childKey))
                continue;
            blankValue(entry.second, childKey);
        }
        return;
    }
    if (slot.get_type() == nbt::tag_type::List) {
        auto& list = slot.as<nbt::tag_list>();
        for (size_t i = 0; i < list.size(); ++i) {
            nbt::value& item = list.at(i);
            if (item.get_type() == nbt::tag_type::Compound)
                blankValue(item, QString());
            else if (item.get_type() == nbt::tag_type::List)
                blankValue(item, QString());
        }
    }
}

QByteArray blankedBytes(const std::string& name, nbt::tag_compound& root)
{
    for (auto& entry : root) {
        const QString key = QString::fromStdString(entry.first);
        if (isSkippedSubtree(key))
            continue;
        blankValue(entry.second, key);
    }
    return serializeNbt(name, root);
}

// ────────────────────────── MCA ──────────────────────────

struct Entry {
    quint32 offset = 0;
    quint8 sectors = 0;
    quint8 compression = 2;
    QByteArray payload;
};

std::vector<Entry> readEntries(const QByteArray& blob)
{
    std::vector<Entry> entries(1024);
    for (int index = 0; index < 1024; ++index) {
        const auto* location = reinterpret_cast<const uchar*>(blob.constData()) + index * 4;
        Entry entry;
        entry.offset = (quint32(location[0]) << 16) | (quint32(location[1]) << 8) | quint32(location[2]);
        entry.sectors = location[3];
        if (entry.offset == 0 || entry.sectors == 0)
            continue;

        const qint64 start = qint64(entry.offset) * SECTOR;
        if (start + 5 > blob.size())
            continue;
        const auto* head = reinterpret_cast<const uchar*>(blob.constData()) + start;
        const quint32 length =
            (quint32(head[0]) << 24) | (quint32(head[1]) << 16) | (quint32(head[2]) << 8) | quint32(head[3]);
        if (length < 1 || start + 4 + length > blob.size())
            continue;

        entry.compression = head[4];
        entry.payload = blob.mid(int(start) + 5, int(length) - 1);
        entries[size_t(index)] = entry;
    }
    return entries;
}

/// 解析载荷 → (根标签名, 树)；失败返回 false
bool parsePayload(const QByteArray& payload, quint8 compression, std::string& name,
                  std::unique_ptr<nbt::tag_compound>& root)
{
    QByteArray raw;
    if (compression == 1 || compression == 2) {
        raw = inflatePayload(payload);
        if (raw.isEmpty())
            return false;
    } else if (compression == 3) {
        raw = payload;
    } else {
        return false;
    }

    try {
        std::istringstream stream(std::string(raw.constData(), size_t(raw.size())));
        auto parsed = nbt::io::read_compound(stream);
        name = parsed.first;
        root = std::move(parsed.second);
        return root != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

/**
 * 处理单个 .mca：能改的改，改完比结构，不一致就丢弃。
 * @return 该文件是否有区块被改动（没有就不用重写文件）
 */
bool patchRegionFile(const QString& path, const QHash<QString, QString>& mapping, ApplyStats& stats)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray blob = file.readAll();
    file.close();
    if (blob.size() < HEADER)
        return false;

    auto entries = readEntries(blob);
    ++stats.files;

    // 有任何一项位置表看着有数据、却读不出载荷，就**整个文件不动**：
    // 重写会把这一项丢掉，等于凭空少一个区块
    for (const auto& entry : entries) {
        if (entry.offset != 0 && entry.payload.isEmpty())
            return false;
    }

    bool touched = false;
    for (auto& entry : entries) {
        if (entry.payload.isEmpty())
            continue;
        ++stats.chunks;

        std::string name;
        std::unique_ptr<nbt::tag_compound> root;
        if (!parsePayload(entry.payload, entry.compression, name, root))
            continue;

        // 在副本上改，原始树留着做结构比对
        nbt::tag_compound patched(*root);
        ApplyStats local;
        for (auto& child : patched) {
            const QString key = QString::fromStdString(child.first);
            if (isSkippedSubtree(key))
                continue;
            patchValue(child.second, key, mapping, local);
        }
        if (local.replaced == 0)
            continue;

        // 结构比对：两边都把文本清空，剩下的必须逐字节一致。
        // 注意用法比对用的**副本** —— 直接在 patched 上清空，写回去的就是一个没文本的区块
        // （这个 bug 是单元测试抓出来的：一份只映射了一条的补丁，把整块文本全抹了）。
        nbt::tag_compound structureOnly(patched);
        const QByteArray before = blankedBytes(name, *root);
        const QByteArray after = blankedBytes(name, structureOnly);
        if (before != after) {
            ++stats.structureDiffs;
            ++stats.discarded;
            continue;
        }

        const QByteArray serialized = serializeNbt(name, patched);
        const QByteArray compressed = compressPayload(serialized, entry.compression);
        if (compressed.isEmpty()) {
            ++stats.discarded;
            continue;
        }

        entry.payload = compressed;
        stats.replaced += local.replaced;
        ++stats.patchedChunks;
        touched = true;
    }

    if (!touched)
        return false;

    // 整体重建：未改动的区块按原始压缩字节搬运，改过的用新载荷
    QByteArray rebuilt(HEADER, '\0');
    int nextSector = 2;
    for (int index = 0; index < 1024; ++index) {
        const auto& entry = entries[size_t(index)];
        if (entry.payload.isEmpty())
            continue;

        QByteArray record;
        record.append(char((entry.payload.size() + 1) >> 24));
        record.append(char(((entry.payload.size() + 1) >> 16) & 0xFF));
        record.append(char(((entry.payload.size() + 1) >> 8) & 0xFF));
        record.append(char((entry.payload.size() + 1) & 0xFF));
        record.append(char(entry.compression));
        record += entry.payload;

        const int padding = (SECTOR - (record.size() % SECTOR)) % SECTOR;
        record.append(padding, '\0');
        const int sectors = record.size() / SECTOR;
        if (sectors > 255) {
            // 单个区块重新压缩后超过 1 MB（理论上不该发生）。这种情况整张文件都不动，
            // 让游戏继续读原文件，总比写出一个装不下的区块好。
            ++stats.discarded;
            return false;
        }

        rebuilt[index * 4 + 0] = char((nextSector >> 16) & 0xFF);
        rebuilt[index * 4 + 1] = char((nextSector >> 8) & 0xFF);
        rebuilt[index * 4 + 2] = char(nextSector & 0xFF);
        rebuilt[index * 4 + 3] = char(sectors);
        rebuilt += record;
        nextSector += sectors;
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(rebuilt);
    return out.commit();
}

}  // namespace

QHash<QString, QString> loadTranslations(const QString& jsonPath, int* withTranslation, QString* error)
{
    QHash<QString, QString> mapping;
    if (withTranslation != nullptr)
        *withTranslation = 0;

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = tr("打不开 %1").arg(jsonPath);
        return mapping;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr)
            *error = tr("strings.json 解析失败：%1").arg(parseError.errorString());
        return mapping;
    }

    const auto items = document.object().value(QStringLiteral("items")).toArray();
    for (const auto& value : items) {
        const auto object = value.toObject();
        const QString source = object.value(QStringLiteral("en")).toString();
        const QString zh = object.value(QStringLiteral("zh")).toString();
        if (source.isEmpty() || zh.isEmpty())
            continue;
        mapping.insert(source, zh);
        if (withTranslation != nullptr)
            ++(*withTranslation);
    }
    return mapping;
}

QHash<QString, QString> loadKeyTranslations(const QString& jsonPath, int* withTranslation, QString* error)
{
    QHash<QString, QString> mapping;
    if (withTranslation != nullptr)
        *withTranslation = 0;

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = tr("打不开 %1").arg(jsonPath);
        return mapping;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr)
            *error = tr("strings.json 解析失败：%1").arg(parseError.errorString());
        return mapping;
    }

    const auto items = document.object().value(QStringLiteral("items")).toArray();
    for (const auto& value : items) {
        const auto object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() != QLatin1String("key"))
            continue;
        const QString key = object.value(QStringLiteral("key")).toString();
        const QString zh = object.value(QStringLiteral("zh")).toString();
        if (key.isEmpty() || zh.isEmpty())
            continue;
        mapping.insert(key, zh);
        if (withTranslation != nullptr)
            ++(*withTranslation);
    }
    return mapping;
}

// ────────────────────────── A 档：只改资源包 ──────────────────────────

namespace {

/// 资源包里的语言表入口（与探针同一套路径规则）
struct PackLangEntry {
    QString nameSpace;
    QString locale;
    QString path;
};

bool splitPackLangPath(const QString& name, PackLangEntry* out)
{
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

QByteArray langJson(const QHash<QString, QString>& table)
{
    QJsonObject object;
    QStringList keys = table.keys();
    std::sort(keys.begin(), keys.end());  // 稳定输出，便于 diff
    for (const auto& key : keys)
        object.insert(key, table.value(key));
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QHash<QString, QString> parseLangJson(const QByteArray& bytes)
{
    QHash<QString, QString> table;
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return table;
    const auto object = doc.object();
    for (auto it = object.begin(); it != object.end(); ++it)
        table.insert(it.key(), it.value().toString());
    return table;
}

}  // namespace

PackApplyStats applyToPack(const QString& worldRoot, const QHash<QString, QString>& keyToZh,
                           const QString& backupDir)
{
    PackApplyStats stats;

    const QString zipPath = FS::PathCombine(worldRoot, QStringLiteral("resources.zip"));
    const QString dirPath = FS::PathCombine(worldRoot, QStringLiteral("resources"));
    const bool fromZip = QFileInfo::exists(zipPath);
    if (!fromZip && !QFileInfo(dirPath).isDir()) {
        stats.error = tr("这张图的存档里没有自带资源包（resources.zip / resources/），没有可写的语言表。");
        return stats;
    }
    stats.packPath = fromZip ? zipPath : dirPath;

    // ── 1. 找出「键最多的命名空间」的 en_us / zh_cn ──
    QList<PackLangEntry> entries;
    if (fromZip) {
        MMCZip::ArchiveReader reader(zipPath);
        const auto parsed = reader.parse([&entries](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            if (file->isFile()) {
                PackLangEntry entry;
                if (splitPackLangPath(file->filename(), &entry))
                    entries.append(entry);
            }
            return false;
        });
        if (!parsed) {
            stats.error = tr("读不出资源包：%1").arg(parsed.error());
            return stats;
        }
    } else {
        QDirIterator it(FS::PathCombine(dirPath, QStringLiteral("assets")), QStringList{ "*.json" }, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absolute = it.next();
            const QString relative = QString(QDir(dirPath).relativeFilePath(absolute)).replace('\\', '/');
            PackLangEntry entry;
            if (splitPackLangPath(relative, &entry))
                entries.append(entry);
        }
    }

    if (entries.isEmpty()) {
        stats.error = tr("资源包里没有任何语言表，没有可写的地方。");
        return stats;
    }

    const auto readEntry = [&](const QString& path) -> QByteArray {
        if (!fromZip) {
            QFile file(FS::PathCombine(dirPath, path));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        }
        MMCZip::ArchiveReader reader(zipPath);
        const auto data = reader.readFile(path);
        return data ? *data : QByteArray();
    };

    // 每个 ns 的 en 表大小，取最大的那个（与探针一致）
    QString bestNs;
    int bestSize = -1;
    QHash<QString, QString> enTable;
    for (const auto& entry : entries) {
        if (entry.locale != QLatin1String("en_us"))
            continue;
        const auto table = parseLangJson(readEntry(entry.path));
        if (int(table.size()) > bestSize) {
            bestSize = int(table.size());
            bestNs = entry.nameSpace;
            enTable = table;
        }
    }
    if (bestNs.isEmpty() || enTable.isEmpty()) {
        stats.error = tr("资源包里没有 en_us 语言表，判不出该往哪个命名空间写中文。");
        return stats;
    }
    stats.total = int(enTable.size());

    // ── 2. 目标表：已有 zh_cn 就在它上面合并，否则从 en_us 复制一份 ──
    const QString zhPath = QStringLiteral("assets/%1/lang/zh_cn.json").arg(bestNs);
    QHash<QString, QString> zhTable;
    for (const auto& entry : entries) {
        if (entry.nameSpace == bestNs && entry.locale == QLatin1String("zh_cn")) {
            zhTable = parseLangJson(readEntry(entry.path));
            break;
        }
    }
    if (zhTable.isEmpty())
        zhTable = enTable;  // 没翻到的键保持英文，总比显示成键名强

    for (auto it = keyToZh.begin(); it != keyToZh.end(); ++it) {
        if (!enTable.contains(it.key()))
            continue;  // 拼错的键写进去也不会被读到
        zhTable.insert(it.key(), it.value());
        ++stats.translated;
    }
    stats.localeEntry = zhPath;
    if (stats.translated == 0) {
        stats.error = tr("译文里没有一条对得上资源包的键 —— 可能这张图的文本不在语言表里（见页面上的说明）。");
        return stats;
    }

    const QByteArray json = langJson(zhTable);

    // ── 3. 目录型：直接写；zip 型：整包重写，先把原包备份 ──
    if (!fromZip) {
        const QString target = FS::PathCombine(dirPath, zhPath);
        if (!FS::ensureFilePathExists(target)) {
            stats.error = tr("写不了 %1").arg(target);
            return stats;
        }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
            stats.error = tr("写不了 %1：%2").arg(target, file.errorString());
            return stats;
        }
        return stats;
    }

    if (!backupDir.isEmpty()) {
        if (FS::ensureFolderPathExists(backupDir)) {
            const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            const QString backup =
                QDir(backupDir).filePath(QStringLiteral("resources-%1.zip").arg(stamp));
            if (!QFile::copy(zipPath, backup)) {
                stats.error = tr("备份原资源包失败（%1），为安全起见没有改动它。").arg(backup);
                return stats;
            }
            stats.backupPath = backup;
        }
    }

    const QString tmpPath = zipPath + QStringLiteral(".new");
    QFile::remove(tmpPath);
    {
        MMCZip::ArchiveWriter writer(tmpPath);
        const auto opened = writer.open();
        if (!opened) {
            stats.error = tr("建不了新资源包：%1").arg(opened.error());
            QFile::remove(tmpPath);
            return stats;
        }

        MMCZip::ArchiveReader reader(zipPath);
        QString failure;
        const auto parsed = reader.parse([&](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            if (!file->isFile())
                return false;
            if (file->filename() == zhPath)
                return false;  // 旧的 zh_cn 丢掉，稍后用新的写进去
            const auto added = writer.addFile(file);
            if (!added) {
                failure = added.error();
                return true;  // 停止遍历
            }
            return false;
        });
        if (!failure.isEmpty() || !parsed) {
            writer.close();
            QFile::remove(tmpPath);
            stats.error = tr("重写资源包失败：%1").arg(failure.isEmpty() ? parsed.error() : failure);
            return stats;
        }
        const auto added = writer.addFile(zhPath, json);
        if (!added) {
            writer.close();
            QFile::remove(tmpPath);
            stats.error = tr("写不进 zh_cn.json：%1").arg(added.error());
            return stats;
        }
        const auto closed = writer.close();
        if (!closed) {
            QFile::remove(tmpPath);
            stats.error = tr("资源包收尾失败：%1").arg(closed.error());
            return stats;
        }
    }

    if (!FS::move(tmpPath, zipPath)) {
        QFile::remove(tmpPath);
        stats.error = tr("新资源包就位失败（原包未动，备份在 %1）。").arg(stats.backupPath);
        return stats;
    }
    return stats;
}

ApplyStats applyToCopy(const QString& sourceRoot, const QString& outputRoot,
                       const QHash<QString, QString>& translations,
                       const std::function<bool(int done, int total)>& progress,
                       const QHash<QString, QString>& keyTranslations)
{
    ApplyStats stats;

    if (!QFileInfo(sourceRoot).isDir()) {
        stats.error = tr("存档目录不存在：%1").arg(sourceRoot);
        return stats;
    }
    // 只有语言表的译文时也允许跑：这类图不必改区块，复制完改资源包就够
    if (translations.isEmpty() && keyTranslations.isEmpty()) {
        stats.error = tr("没有任何已翻译的条目。");
        return stats;
    }
    if (QFileInfo::exists(outputRoot)) {
        stats.error = tr("目标目录已存在，换一个名字：%1").arg(outputRoot);
        return stats;
    }

    // 先整体复制：源目录一个字节都不碰
    if (!FS::ensureFolderPathExists(QFileInfo(outputRoot).absolutePath())) {
        stats.error = tr("无法创建目录：%1").arg(QFileInfo(outputRoot).absolutePath());
        return stats;
    }
    {
        FS::copy copier(sourceRoot, outputRoot);
        if (!copier()) {
            stats.error = tr("复制存档失败（磁盘空间不足或文件被占用）。");
            return stats;
        }
    }

    QStringList files;
    for (const auto& sub : { "region", "entities", "DIM-1/region", "DIM1/region" }) {
        QDir dir(FS::PathCombine(outputRoot, QString(sub)));
        if (!dir.exists())
            continue;
        for (const auto& name : dir.entryList(QStringList{ "*.mca" }, QDir::Files, QDir::Name))
            files.append(dir.absoluteFilePath(name));
    }

    const int total = int(files.size()) + (keyTranslations.isEmpty() ? 0 : 1);
    int done = 0;
    // 只有语言表译文时（`translations` 为空）不必碰区块：复制 + 改资源包就够
    if (!translations.isEmpty()) {
        for (const auto& path : files) {
            patchRegionFile(path, translations, stats);
            ++done;
            if (progress && !progress(done, total))
                break;
        }
    } else {
        done = int(files.size());
    }

    // 混合档：副本上的资源包也要一起汉化（语言表那部分）
    if (!keyTranslations.isEmpty()) {
        const auto pack = applyToPack(outputRoot, keyTranslations, QString());
        stats.packTranslated = pack.translated;
        if (!pack.error.isEmpty() && stats.error.isEmpty())
            qWarning() << "MapL10nApply: 副本资源包没改成" << pack.error;
        ++done;
        if (progress)
            progress(done, total);
    }

    return stats;
}

// ────────────────────────── 任务 ──────────────────────────

MapL10nApplyTask::MapL10nApplyTask(QString sourceRoot, QString outputRoot, QHash<QString, QString> translations,
                                   QHash<QString, QString> keyTranslations)
    : m_sourceRoot(std::move(sourceRoot))
    , m_outputRoot(std::move(outputRoot))
    , m_translations(std::move(translations))
    , m_keyTranslations(std::move(keyTranslations))
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &MapL10nApplyTask::onApplyFinished);
}

bool MapL10nApplyTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_aborted.store(true);
    return true;
}

void MapL10nApplyTask::executeTask()
{
    setStatus(tr("正在复制存档并替换文本…"));
    setProgress(0, 0);

    m_watcher.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        m_stats = applyToCopy(m_sourceRoot, m_outputRoot, m_translations, [this](int done, int total) {
            if (m_aborted.load())
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    setProgress(done, total);
                    setStatus(tr("正在处理区块文件… %1 / %2").arg(done).arg(total));
                },
                Qt::QueuedConnection);
            return true;
        },
                                  m_keyTranslations);
    }));
}

void MapL10nApplyTask::onApplyFinished()
{
    if (m_aborted.load()) {
        emitAborted();
        return;
    }
    if (!m_stats.error.isEmpty()) {
        emitFailed(m_stats.error);
        return;
    }
    if (!m_stats.ok()) {
        emitFailed(tr("有 %1 个区块结构比对没过，已丢弃。").arg(m_stats.structureDiffs));
        return;
    }
    emitSucceeded();
}

// ────────────────────────── 备份 + 替换 ──────────────────────────

namespace {

/// 递归列出目录下所有文件：<绝对路径, zip 内路径>
void collectFiles(const QString& root, const QString& prefix, QList<QPair<QString, QString>>& out, int depth = 0)
{
    if (depth > 16)
        return;

    const QDir dir(root);
    const auto entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const auto& info : entries) {
        // 存档里不该有符号链接；真有也不跟（可能指回上级目录，跟进去会绕圈）
        if (info.isSymLink())
            continue;
        if (info.isDir()) {
            collectFiles(info.absoluteFilePath(), prefix + info.fileName() + QLatin1Char('/'), out, depth + 1);
        } else {
            out.append({ info.absoluteFilePath(), prefix + info.fileName() });
        }
    }
}

/// 同名备份已存在时加序号，不覆盖上一次的备份
QString uniquePath(const QString& dir, const QString& base, const QString& suffix)
{
    QString path = QDir(dir).filePath(base + suffix);
    for (int i = 2; QFileInfo::exists(path) && i < 100; ++i)
        path = QDir(dir).filePath(QStringLiteral("%1-%2%3").arg(base).arg(i).arg(suffix));
    return path;
}

}  // namespace

ReplaceStats replaceWorldWithBackup(const QString& worldRoot, const QString& newWorldRoot,
                                    const QString& backupDir,
                                    const std::function<bool(int done, int total)>& progress)
{
    ReplaceStats stats;
    stats.worldFolder = QFileInfo(worldRoot).fileName();

    if (!QFileInfo(worldRoot).isDir()) {
        stats.error = tr("旧存档目录不存在：%1").arg(worldRoot);
        return stats;
    }
    if (!QFileInfo(newWorldRoot).isDir()) {
        stats.error = tr("汉化版目录不存在：%1").arg(newWorldRoot);
        return stats;
    }
    // 没有 level.dat 的目录（例如上次运行被中断）会被游戏当成新世界重建 —— 绝不能拿来顶替
    if (!QFile::exists(FS::PathCombine(newWorldRoot, QStringLiteral("level.dat")))) {
        stats.error = tr("汉化版不完整（缺 level.dat），不能用来替换：%1").arg(newWorldRoot);
        return stats;
    }

    QList<QPair<QString, QString>> files;
    collectFiles(worldRoot, QString(), files);
    if (files.isEmpty()) {
        stats.error = tr("旧存档里一个文件都没有，没有可备份的内容。");
        return stats;
    }
    stats.files = int(files.size());

    if (!FS::ensureFolderPathExists(backupDir)) {
        stats.error = tr("无法创建备份目录：%1").arg(backupDir);
        return stats;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    stats.backupPath = uniquePath(backupDir, QStringLiteral("%1-%2").arg(stats.worldFolder, stamp),
                                  QStringLiteral(".zip"));

    {
        MMCZip::ArchiveWriter writer(stats.backupPath);
        // 区块文件内部已经 zlib 压过，再 deflate 一遍只是白烧 CPU（实测打包时间差一个数量级）
        writer.setCompression(QStringLiteral("store"));

        auto opened = writer.open();
        if (!opened) {
            stats.error = tr("无法创建备份压缩包：%1").arg(opened.error());
            return stats;
        }

        int done = 0;
        bool cancelled = false;
        for (const auto& [source, dest] : files) {
            auto added = writer.addFile(source, dest);
            if (!added) {
                (void)writer.close();
                QFile::remove(stats.backupPath);
                stats.error = tr("备份失败（%1）：%2").arg(dest, added.error());
                return stats;
            }
            ++done;
            if (progress && !progress(done, stats.files)) {
                cancelled = true;
                break;
            }
        }

        auto closed = writer.close();
        if (cancelled) {
            QFile::remove(stats.backupPath);
            stats.error = tr("已取消，旧存档没有改动。");
            return stats;
        }
        if (!closed) {
            QFile::remove(stats.backupPath);
            stats.error = tr("备份压缩包收尾失败：%1").arg(closed.error());
            return stats;
        }
    }

    stats.backupBytes = QFileInfo(stats.backupPath).size();
    if (stats.backupBytes <= 0) {
        QFile::remove(stats.backupPath);
        stats.error = tr("备份压缩包是空的，已放弃替换。");
        return stats;
    }

    // ── 到这里备份已经落地，才开始动目录 ──
    const QString oldMoved = worldRoot + QStringLiteral(".old-") + stamp;
    if (QFileInfo::exists(oldMoved))
        FS::deletePath(oldMoved);

    if (!FS::move(worldRoot, oldMoved)) {
        stats.error = tr("无法把旧存档挪开（可能正被游戏占用）。备份已保留：%1").arg(stats.backupPath);
        return stats;
    }
    if (!FS::move(newWorldRoot, worldRoot)) {
        // 就位失败：把旧存档原样放回去。备份 zip 也留着，双保险。
        if (!FS::move(oldMoved, worldRoot))
            stats.error = tr("汉化版就位失败，且旧存档没能挪回原位！\n旧存档在：%1\n备份：%2")
                              .arg(oldMoved, stats.backupPath);
        else
            stats.error = tr("汉化版就位失败，旧存档已还原。备份：%1").arg(stats.backupPath);
        return stats;
    }

    // 旧目录已经完整打进 zip，临时目录可以删了
    FS::deletePath(oldMoved);
    return stats;
}

MapL10nReplaceTask::MapL10nReplaceTask(QString worldRoot, QString newWorldRoot, QString backupDir)
    : m_worldRoot(std::move(worldRoot))
    , m_newWorldRoot(std::move(newWorldRoot))
    , m_backupDir(std::move(backupDir))
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &MapL10nReplaceTask::onReplaceFinished);
}

bool MapL10nReplaceTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_aborted.store(true);
    return true;
}

void MapL10nReplaceTask::executeTask()
{
    setStatus(tr("正在打包备份旧存档…"));
    setProgress(0, 0);

    m_watcher.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        m_stats = replaceWorldWithBackup(m_worldRoot, m_newWorldRoot, m_backupDir, [this](int done, int total) {
            if (m_aborted.load())
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    setProgress(done, total);
                    // 打包完就该动目录了，这一步很快但没有回调，先改掉状态文字
                    setStatus(done >= total ? tr("正在替换存档目录…")
                                            : tr("正在打包备份旧存档… %1 / %2").arg(done).arg(total));
                },
                Qt::QueuedConnection);
            return true;
        });
    }));
}

void MapL10nReplaceTask::onReplaceFinished()
{
    if (m_aborted.load()) {
        emitAborted();
        return;
    }
    if (!m_stats.error.isEmpty()) {
        emitFailed(m_stats.error);
        return;
    }
    emitSucceeded();
}

// ────────────────────────── 从备份还原 ──────────────────────────

QList<BackupEntry> listBackups(const QString& backupDir)
{
    QList<BackupEntry> entries;
    QDir dir(backupDir);
    if (!dir.exists())
        return entries;

    // QDir::Time 让最新的排在最前：「恢复原图」最常要的就是刚才那一次
    const auto files = dir.entryInfoList({ QStringLiteral("*.zip") }, QDir::Files, QDir::Time);
    entries.reserve(files.size());
    for (const auto& info : files) {
        entries.append(BackupEntry{ info.absoluteFilePath(), info.lastModified(), info.size() });
    }
    return entries;
}

RestoreStats restoreWorldFromBackup(const QString& worldRoot, const QString& backupPath,
                                    const std::function<bool(int done, int total)>& progress)
{
    RestoreStats stats;
    stats.backupPath = backupPath;
    stats.worldFolder = QFileInfo(worldRoot).fileName();

    if (!QFileInfo::exists(backupPath)) {
        stats.error = tr("备份文件不存在：%1").arg(backupPath);
        return stats;
    }
    if (!QFileInfo(worldRoot).isDir()) {
        stats.error = tr("存档目录不存在：%1").arg(worldRoot);
        return stats;
    }
    if (progress && !progress(0, 1)) {
        stats.error = tr("已取消。");
        return stats;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString scratch = worldRoot + QStringLiteral(".restore-") + stamp;
    if (QFileInfo::exists(scratch))
        FS::deletePath(scratch);

    // 解压到世界目录旁边（同一个卷，后面才有得 rename）。MMCZip 这一层不回调进度，
    // 所以进度条只在「开始 / 完成」两头动，中间靠状态文字说明。
    auto extracted = MMCZip::extractDir(backupPath, scratch);
    if (!extracted) {
        FS::deletePath(scratch);
        stats.error = tr("解压备份失败：%1").arg(extracted.error());
        return stats;
    }
    stats.files = int(extracted->size());

    // 没有 level.dat 的目录会被游戏当成新世界重建 —— 宁可报错也不能让它就位
    if (!QFile::exists(FS::PathCombine(scratch, QStringLiteral("level.dat")))) {
        FS::deletePath(scratch);
        stats.error = tr("这份备份里没有 level.dat，不像是世界存档，已放弃。");
        return stats;
    }
    if (progress && !progress(1, 1)) {
        FS::deletePath(scratch);
        stats.error = tr("已取消，存档没有改动。");
        return stats;
    }

    // ── 解压好了才开始动目录 ──
    const QString oldMoved = worldRoot + QStringLiteral(".old-") + stamp;
    if (QFileInfo::exists(oldMoved))
        FS::deletePath(oldMoved);

    if (!FS::move(worldRoot, oldMoved)) {
        FS::deletePath(scratch);
        stats.error = tr("无法把当前存档挪开（可能正被游戏占用）。");
        return stats;
    }
    if (!FS::move(scratch, worldRoot)) {
        if (!FS::move(oldMoved, worldRoot))
            stats.error = tr("还原失败，且当前存档没能挪回原位！\n当前存档在：%1").arg(oldMoved);
        else
            stats.error = tr("还原失败，当前存档已还原。");
        FS::deletePath(scratch);
        return stats;
    }

    FS::deletePath(oldMoved);
    return stats;
}

MapL10nRestoreTask::MapL10nRestoreTask(QString worldRoot, QString backupPath)
    : m_worldRoot(std::move(worldRoot))
    , m_backupPath(std::move(backupPath))
{
    setAbortable(true);
    setAbortButtonText(tr("取消"));
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &MapL10nRestoreTask::onRestoreFinished);
}

bool MapL10nRestoreTask::abort()
{
    if (!canAbort() || !isRunning())
        return false;
    m_aborted.store(true);
    return true;
}

void MapL10nRestoreTask::executeTask()
{
    setStatus(tr("正在解压备份…"));
    setProgress(0, 0);

    m_watcher.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        m_stats = restoreWorldFromBackup(m_worldRoot, m_backupPath, [this](int done, int total) {
            if (m_aborted.load())
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    setProgress(done, total);
                    if (done >= total)
                        setStatus(tr("正在就位存档目录…"));
                },
                Qt::QueuedConnection);
            return true;
        });
    }));
}

void MapL10nRestoreTask::onRestoreFinished()
{
    if (m_aborted.load()) {
        emitAborted();
        return;
    }
    if (!m_stats.error.isEmpty()) {
        emitFailed(m_stats.error);
        return;
    }
    emitSucceeded();
}

}  // namespace maps::l10n
