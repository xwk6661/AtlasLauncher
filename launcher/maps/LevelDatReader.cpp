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
#include "LevelDatReader.h"

#include <QDebug>
#include <QFile>
#include <QSet>

#include <io/stream_reader.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include <sstream>

#include "GZip.h"

namespace maps {
namespace {

/// 加载器自身不算"需要玩家安装的模组"
const QSet<QString>& loaderModIds()
{
    static const QSet<QString> ids = { QStringLiteral("minecraft"),   QStringLiteral("forge"),
                                       QStringLiteral("fml"),         QStringLiteral("mcp"),
                                       QStringLiteral("java"),        QStringLiteral("neoforge"),
                                       QStringLiteral("fabricloader"), QStringLiteral("fabric"),
                                       QStringLiteral("quilt_loader"), QStringLiteral("quiltloader") };
    return ids;
}

std::optional<qint64> asInt64(const nbt::value& value)
{
    switch (value.get_type()) {
        case nbt::tag_type::Byte:
            return static_cast<qint64>(value.as<nbt::tag_byte>().get());
        case nbt::tag_type::Short:
            return static_cast<qint64>(value.as<nbt::tag_short>().get());
        case nbt::tag_type::Int:
            return static_cast<qint64>(value.as<nbt::tag_int>().get());
        case nbt::tag_type::Long:
            return value.as<nbt::tag_long>().get();
        case nbt::tag_type::Float:
            return static_cast<qint64>(value.as<nbt::tag_float>().get());
        case nbt::tag_type::Double:
            return static_cast<qint64>(value.as<nbt::tag_double>().get());
        default:
            return std::nullopt;
    }
}

QString asString(const nbt::value& value)
{
    if (value.get_type() != nbt::tag_type::String)
        return {};
    return QString::fromStdString(value.as<nbt::tag_string>().get());
}

QStringList asStringList(const nbt::value& value)
{
    QStringList out;
    if (value.get_type() != nbt::tag_type::List)
        return out;
    const auto& list = value.as<nbt::tag_list>();
    if (list.el_type() != nbt::tag_type::String)
        return out;
    out.reserve(static_cast<int>(list.size()));
    for (const auto& item : list)
        out.append(QString::fromStdString(item.as<nbt::tag_string>().get()));
    return out;
}

/// 取子标签；不存在或类型不符时返回 nullptr
const nbt::value* child(const nbt::tag_compound& parent, const char* key)
{
    if (!parent.has_key(key))
        return nullptr;
    return &parent.at(key);
}

std::optional<int> childInt(const nbt::tag_compound& parent, const char* key)
{
    const auto* tag = child(parent, key);
    if (tag == nullptr)
        return std::nullopt;
    const auto number = asInt64(*tag);
    if (!number)
        return std::nullopt;
    return static_cast<int>(*number);
}

QString childString(const nbt::tag_compound& parent, const char* key)
{
    const auto* tag = child(parent, key);
    return tag == nullptr ? QString() : asString(*tag);
}

QStringList childStringList(const nbt::tag_compound& parent, const char* key)
{
    const auto* tag = child(parent, key);
    return tag == nullptr ? QStringList() : asStringList(*tag);
}

/**
 * Forge 在 fml.LoadingModList 里记的模组清单（1.16.5 形态）。
 * 1.7.10 的 FML 标签只有 ItemData，取不到就返回空 —— 不猜。
 */
QStringList forgeModList(const nbt::tag_compound& root)
{
    const nbt::value* fml = child(root, "fml");
    if (fml == nullptr)
        fml = child(root, "FML");
    if (fml == nullptr || fml->get_type() != nbt::tag_type::Compound)
        return {};

    const auto* listTag = child(fml->as<nbt::tag_compound>(), "LoadingModList");
    if (listTag == nullptr || listTag->get_type() != nbt::tag_type::List)
        return {};

    QStringList ids;
    for (const auto& item : listTag->as<nbt::tag_list>()) {
        if (item.get_type() != nbt::tag_type::Compound)
            continue;
        const auto& entry = item.as<nbt::tag_compound>();
        QString id = childString(entry, "ModId");
        if (id.isEmpty())
            id = childString(entry, "ModID");
        if (id.isEmpty())
            id = childString(entry, "modid");
        if (id.isEmpty())
            continue;
        if (loaderModIds().contains(id.toLower()))
            continue;

        QString version = childString(entry, "ModVersion");
        if (version.isEmpty())
            version = childString(entry, "Version");
        const QString record = version.isEmpty() ? id : QStringLiteral("%1@%2").arg(id, version);
        if (!ids.contains(record))
            ids.append(record);
    }
    return ids;
}

}  // namespace

std::optional<LevelDatFacts> LevelDatReader::fromBytes(const QByteArray& bytes, QString* error)
{
    if (bytes.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("level.dat 为空");
        return std::nullopt;
    }

    QByteArray raw = bytes;
    // GZip 魔数 1f 8b；少数工具导出的是裸 NBT，需要兼容
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes.at(0)) == 0x1f &&
        static_cast<unsigned char>(bytes.at(1)) == 0x8b) {
        QByteArray decompressed;
        if (!GZip::unzip(bytes, decompressed)) {
            if (error != nullptr)
                *error = QStringLiteral("level.dat GZip 解压失败");
            return std::nullopt;
        }
        raw = decompressed;
    }

    std::unique_ptr<nbt::tag_compound> root;
    try {
        std::istringstream stream(std::string(raw.constData(), static_cast<size_t>(raw.size())));
        auto parsed = nbt::io::read_compound(stream);
        if (!parsed.first.empty() || parsed.second == nullptr) {
            if (error != nullptr)
                *error = QStringLiteral("level.dat 根标签不是 compound");
            return std::nullopt;
        }
        root = std::move(parsed.second);
    } catch (const std::exception& e) {
        if (error != nullptr)
            *error = QStringLiteral("level.dat NBT 解析失败：%1").arg(QString::fromUtf8(e.what()));
        return std::nullopt;
    }

    // 标准结构是 { Data: {...} }；极少数工具会平铺，做一次兼容
    const nbt::tag_compound* data = root.get();
    if (const auto* dataTag = child(*root, "Data"); dataTag != nullptr && dataTag->get_type() == nbt::tag_type::Compound)
        data = &dataTag->as<nbt::tag_compound>();

    LevelDatFacts facts;
    facts.levelName = childString(*data, "LevelName");
    facts.dataVersion = childInt(*data, "DataVersion");
    facts.storageVersion = childInt(*data, "version");
    facts.hasBorderSize = data->has_key("BorderSize");
    facts.hasPlayerTag = data->has_key("Player");
    facts.hasDataPacks = data->has_key("DataPacks") || data->has_key("DataPacksVersion");
    facts.dataKeyCount = static_cast<int>(data->size());
    // minecraft:vanilla 是默认项，一直在列表里；只有出现别项才算玩家真开了实验性设置
    // （AIO 在 sniffer 里做同样过滤，见 sniffer/index.ts）
    facts.enabledFeatures = childStringList(*data, "enabled_features");
    facts.enabledFeatures.removeAll(QStringLiteral("minecraft:vanilla"));

    if (const auto* versionTag = child(*data, "Version"); versionTag != nullptr && versionTag->get_type() == nbt::tag_type::Compound) {
        const auto& version = versionTag->as<nbt::tag_compound>();
        facts.versionName = childString(version, "Name");
        facts.versionId = childInt(version, "Id");
        facts.series = childString(version, "Series");
        if (const auto* snapshotTag = child(version, "Snapshot"); snapshotTag != nullptr) {
            const auto snapshot = asInt64(*snapshotTag);
            if (snapshot)
                facts.snapshot = (*snapshot == 1);
        }
    } else {
        // 没有 Version 复合标签时，个别工具会把名字写在 Data.VersionName
        facts.versionName = childString(*data, "VersionName");
        facts.versionId = std::nullopt;
    }

    facts.serverBrands = childStringList(*data, "ServerBrands");
    for (auto& brand : facts.serverBrands)
        brand = brand.toLower();

    const auto modIds = forgeModList(*root);

    bool hasRootMarker = false;
    for (auto it = root->begin(); it != root->end(); ++it) {
        const auto key = QString::fromStdString(it->first).toLower();
        if (key == QLatin1String("fml") || key == QLatin1String("forge")) {
            hasRootMarker = true;
            break;
        }
    }

    bool hasBrandMarker = false;
    for (const auto& brand : facts.serverBrands) {
        if (brand.contains(QLatin1String("forge")) || brand.contains(QLatin1String("fabric")) ||
            brand.contains(QLatin1String("quilt")) || brand.contains(QLatin1String("neoforge"))) {
            hasBrandMarker = true;
            break;
        }
    }

    bool wasModded = false;
    if (const auto* wasModdedTag = child(*data, "WasModded"); wasModdedTag != nullptr) {
        const auto value = asInt64(*wasModdedTag);
        wasModded = value && *value == 1;
    }

    facts.requiredMods = modIds;
    facts.isModded = hasRootMarker || wasModded || hasBrandMarker || !modIds.isEmpty();
    facts.valid = true;
    return facts;
}

std::optional<LevelDatFacts> LevelDatReader::fromFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return std::nullopt;
    }
    return fromBytes(file.readAll(), error);
}

}  // namespace maps
