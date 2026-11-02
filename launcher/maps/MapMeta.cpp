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
#include "MapMeta.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "FileSystem.h"

namespace maps {
namespace {

QJsonArray toArray(const QStringList& values)
{
    QJsonArray array;
    for (const auto& value : values)
        array.append(value);
    return array;
}

QStringList fromArray(const QJsonValue& value)
{
    QStringList out;
    for (const auto& item : value.toArray())
        out.append(item.toString());
    return out;
}

}  // namespace

MapMeta MapMeta::load(const QString& instanceRoot, QString* error)
{
    MapMeta meta;
    const QString path = FS::PathCombine(instanceRoot, fileName());

    QFile file(path);
    if (!file.exists())
        return meta;  // 不是地图实例
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return meta;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr)
            *error = QStringLiteral("%1 不是合法 JSON：%2").arg(path, parseError.errorString());
        return meta;
    }

    return fromJson(document.object());
}

MapMeta MapMeta::fromJson(const QJsonObject& object)
{
    MapMeta meta;
    meta.title = object.value(QStringLiteral("title")).toString();
    meta.alias = object.value(QStringLiteral("alias")).toString();

    const auto archiveJson = object.value(QStringLiteral("archive")).toObject();
    meta.archive.path = archiveJson.value(QStringLiteral("path")).toString();
    meta.archive.size = static_cast<qint64>(archiveJson.value(QStringLiteral("size")).toDouble());
    meta.archive.sha1 = archiveJson.value(QStringLiteral("sha1")).toString();
    meta.archive.libraryCopy = archiveJson.value(QStringLiteral("libraryCopy")).toString();

    const auto versionJson = object.value(QStringLiteral("version")).toObject();
    meta.version.mcVersion = versionJson.value(QStringLiteral("mcVersion")).toString(QStringLiteral("unknown"));
    meta.version.confidence = confidenceFromString(versionJson.value(QStringLiteral("confidence")).toString());
    meta.version.evidence = fromArray(versionJson.value(QStringLiteral("evidence")));
    meta.version.decidedBy = versionJson.value(QStringLiteral("decidedBy")).toString(QStringLiteral("detector"));

    for (const auto& value : object.value(QStringLiteral("worlds")).toArray()) {
        const auto item = value.toObject();
        MapWorldRecord world;
        world.folder = item.value(QStringLiteral("folder")).toString();
        world.sourceRoot = item.value(QStringLiteral("sourceRoot")).toString();
        if (!world.folder.isEmpty())
            meta.worlds.append(world);
    }

    const auto loaderJson = object.value(QStringLiteral("loader")).toObject();
    meta.loader.type = loaderJson.value(QStringLiteral("type")).toString(QStringLiteral("vanilla"));
    meta.loader.version = loaderJson.value(QStringLiteral("version")).toString();

    meta.packs = fromArray(object.value(QStringLiteral("packs")));
    meta.mods = fromArray(object.value(QStringLiteral("mods")));
    meta.experimentalFeatures = fromArray(object.value(QStringLiteral("experimentalFeatures")));
    meta.requiredMods = fromArray(object.value(QStringLiteral("requiredMods")));
    meta.requiresExternalModpack = object.value(QStringLiteral("requiresExternalModpack")).toBool(false);

    /**
     * 旧版本（2026-11-01 之前）把「作者是用 Forge/Fabric 客户端保存的这张图」当成了
     * 「需要整合包」—— 依据是 level.dat 的 ServerBrands / WasModded。这两个字段只说明
     * 作者自己装了模组，**不说明存档依赖它们**：Torre de Piedra 就是这么被误判的
     * （ServerBrands=["fabric"]、WasModded=1，实际原版 1.21.1 直接能开）。
     *
     * 现在唯一可信的证据是 Forge 写在 level.dat 里的 FML LoadingModList
     * （= requiredMods）。没有它就不该说"缺整合包"，所以这里把旧值钳回去。
     */
    if (meta.requiredMods.isEmpty())
        meta.requiresExternalModpack = false;

    const auto statsJson = object.value(QStringLiteral("stats")).toObject();
    meta.stats.playSeconds = static_cast<qint64>(statsJson.value(QStringLiteral("playSeconds")).toDouble());
    meta.stats.lastExit = statsJson.value(QStringLiteral("lastExit")).toString(QStringLiteral("unknown"));
    const QString lastPlayed = statsJson.value(QStringLiteral("lastPlayed")).toString();
    if (!lastPlayed.isEmpty())
        meta.stats.lastPlayed = QDateTime::fromString(lastPlayed, Qt::ISODate);

    meta.importedAt = QDateTime::fromString(object.value(QStringLiteral("importedAt")).toString(), Qt::ISODate);
    return meta;
}

QJsonObject MapMeta::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), SCHEMA_VERSION);
    root.insert(QStringLiteral("title"), title);
    if (!alias.isEmpty())
        root.insert(QStringLiteral("alias"), alias);

    QJsonObject archiveJson;
    archiveJson.insert(QStringLiteral("path"), archive.path);
    archiveJson.insert(QStringLiteral("size"), static_cast<double>(archive.size));
    if (!archive.sha1.isEmpty())
        archiveJson.insert(QStringLiteral("sha1"), archive.sha1);
    if (!archive.libraryCopy.isEmpty())
        archiveJson.insert(QStringLiteral("libraryCopy"), archive.libraryCopy);
    root.insert(QStringLiteral("archive"), archiveJson);

    QJsonObject versionJson;
    versionJson.insert(QStringLiteral("mcVersion"), version.mcVersion);
    versionJson.insert(QStringLiteral("confidence"), confidenceToString(version.confidence));
    versionJson.insert(QStringLiteral("evidence"), toArray(version.evidence));
    versionJson.insert(QStringLiteral("decidedBy"), version.decidedBy);
    root.insert(QStringLiteral("version"), versionJson);

    QJsonArray worldsJson;
    for (const auto& world : worlds) {
        QJsonObject item;
        item.insert(QStringLiteral("folder"), world.folder);
        item.insert(QStringLiteral("sourceRoot"), world.sourceRoot);
        worldsJson.append(item);
    }
    root.insert(QStringLiteral("worlds"), worldsJson);

    QJsonObject loaderJson;
    loaderJson.insert(QStringLiteral("type"), loader.type);
    if (!loader.version.isEmpty())
        loaderJson.insert(QStringLiteral("version"), loader.version);
    root.insert(QStringLiteral("loader"), loaderJson);

    if (!packs.isEmpty())
        root.insert(QStringLiteral("packs"), toArray(packs));
    if (!mods.isEmpty())
        root.insert(QStringLiteral("mods"), toArray(mods));
    if (!experimentalFeatures.isEmpty())
        root.insert(QStringLiteral("experimentalFeatures"), toArray(experimentalFeatures));
    if (!requiredMods.isEmpty())
        root.insert(QStringLiteral("requiredMods"), toArray(requiredMods));
    if (requiresExternalModpack)
        root.insert(QStringLiteral("requiresExternalModpack"), true);

    QJsonObject statsJson;
    statsJson.insert(QStringLiteral("playSeconds"), static_cast<double>(stats.playSeconds));
    statsJson.insert(QStringLiteral("lastExit"), stats.lastExit);
    if (stats.lastPlayed.isValid())
        statsJson.insert(QStringLiteral("lastPlayed"), stats.lastPlayed.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("stats"), statsJson);

    const auto imported = importedAt.isValid() ? importedAt : QDateTime::currentDateTimeUtc();
    root.insert(QStringLiteral("importedAt"), imported.toUTC().toString(Qt::ISODate));

    return root;
}

bool MapMeta::save(const QString& instanceRoot, QString* error) const
{
    const QString path = FS::PathCombine(instanceRoot, fileName());
    const QByteArray payload = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error != nullptr)
            *error = QStringLiteral("写入 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

}  // namespace maps
