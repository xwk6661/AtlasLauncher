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
#include "MapLibrary.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "MapMeta.h"
#include "MapRepository.h"

namespace maps {

QString categoryId(MapCategory category)
{
    switch (category) {
        case MapCategory::Rpg:
            return QStringLiteral("rpg");
        case MapCategory::Aad:
            return QStringLiteral("aad");
        case MapCategory::Ctm:
            return QStringLiteral("ctm");
        case MapCategory::Minigame:
            return QStringLiteral("minigame");
        case MapCategory::Other:
            break;
    }
    return QStringLiteral("other");
}

MapCategory categoryFromId(const QString& id)
{
    const QString key = id.trimmed().toLower();
    if (key == QLatin1String("rpg"))
        return MapCategory::Rpg;
    if (key == QLatin1String("aad"))
        return MapCategory::Aad;
    if (key == QLatin1String("ctm"))
        return MapCategory::Ctm;
    if (key == QLatin1String("minigame") || key == QLatin1String("mini-game") || key == QLatin1String("mini_game"))
        return MapCategory::Minigame;
    return MapCategory::Other;
}

QString categoryLabel(MapCategory category)
{
    switch (category) {
        case MapCategory::Rpg:
            return QStringLiteral("RPG");
        case MapCategory::Aad:
            return QStringLiteral("AAD");
        case MapCategory::Ctm:
            return QStringLiteral("CTM");
        case MapCategory::Minigame:
            return QStringLiteral("MiniGame");
        case MapCategory::Other:
            break;
    }
    return QStringLiteral("Other");
}

QList<MapCategory> allCategories()
{
    return { MapCategory::Rpg, MapCategory::Aad, MapCategory::Ctm, MapCategory::Minigame, MapCategory::Other };
}

QList<CategoryInfo> MapLibrary::defaultCategories()
{
    return {
        { QStringLiteral("rpg"), QStringLiteral("RPG") },
        { QStringLiteral("ctm"), QStringLiteral("CTM") },
        { QStringLiteral("aad"), QStringLiteral("AAD") },
        { QStringLiteral("minigame"), QStringLiteral("MiniGame") },
        { QStringLiteral("other"), QStringLiteral("Other") }
    };
}

QString LibraryItem::displayName() const
{
    if (!chineseName.isEmpty())
        return chineseName;
    if (!englishName.isEmpty())
        return englishName;
    return file;
}

QString LibraryItem::badge() const
{
    QStringList parts;
    if (!mcVersion.isEmpty())
        parts << QStringLiteral("[%1]").arg(mcVersion);
    if (!englishName.isEmpty())
        parts << englishName;
    else if (!chineseName.isEmpty())
        parts << chineseName;
    if (!mapVersion.isEmpty())
        parts << mapVersion;
    if (parts.isEmpty())
        return QFileInfo(file).completeBaseName();
    return parts.join(QLatin1Char(' '));
}

MapLibrary::MapLibrary(QObject* parent) : QObject(parent) {}

QString MapLibrary::libraryDir()
{
    return MapRepository::libraryDir();
}

QString MapLibrary::indexPath()
{
    return FS::PathCombine(libraryDir(), QStringLiteral("map-library.json"));
}

QStringList MapLibrary::archiveSuffixes()
{
    // 与 MapArchiveScanner / MapRepository 支持的后缀保持一致
    return { QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
             QStringLiteral("tar"), QStringLiteral("gz"),  QStringLiteral("mcworld") };
}

namespace {

/// 索引里的一条 → 内存条目
LibraryItem itemFromJson(const QJsonObject& object, const QString& dir)
{
    LibraryItem item;
    item.file = object.value("file").toString();
    item.path = FS::PathCombine(dir, item.file);
    item.category = object.value("category").toString().trimmed().toLower();
    if (item.category.isEmpty())
        item.category = QStringLiteral("other");
    item.mcVersion = object.value("mcVersion").toString();
    item.englishName = object.value("englishName").toString();
    item.mapVersion = object.value("mapVersion").toString();
    item.chineseName = object.value("chineseName").toString();
    item.note = object.value("note").toString();
    item.bytes = static_cast<qint64>(object.value("bytes").toDouble());
    return item;
}

QJsonObject itemToJson(const LibraryItem& item, const QJsonObject& previous)
{
    QJsonObject object = previous;
    object.insert("file", item.file);
    const QString cat = item.category.trimmed().toLower();
    object.insert("category", cat.isEmpty() ? QStringLiteral("other") : cat);
    object.insert("mcVersion", item.mcVersion);
    object.insert("englishName", item.englishName);
    object.insert("mapVersion", item.mapVersion);
    object.insert("chineseName", item.chineseName);
    object.insert("note", item.note);
    object.insert("bytes", static_cast<double>(item.bytes));
    if (!object.contains("addedAt"))
        object.insert("addedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return object;
}

QJsonObject readIndexObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "MapLibrary: 索引不是合法 JSON，按空索引处理：" << path << error.errorString();
        return {};
    }
    return document.object();
}

bool writeIndexObject(const QString& path, const QJsonObject& root)
{
    if (!FS::ensureFolderPathExists(QFileInfo(path).absolutePath()))
        return false;
    // 原子写：先写临时文件再改名，中途被杀不会留下半截索引
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}  // namespace

void MapLibrary::reload()
{
    const QString dir = libraryDir();
    m_items.clear();
    m_root = readIndexObject(indexPath());

    m_categories.clear();
    if (m_root.contains("categories") && m_root.value("categories").isArray()) {
        const auto catsArray = m_root.value("categories").toArray();
        for (const auto& val : catsArray) {
            if (val.isObject()) {
                const auto obj = val.toObject();
                const QString id = obj.value("id").toString().trimmed().toLower();
                const QString name = obj.value("name").toString().trimmed();
                if (!id.isEmpty() && !name.isEmpty()) {
                    m_categories.append({ id, name });
                }
            } else if (val.isString()) {
                const QString name = val.toString().trimmed();
                if (!name.isEmpty()) {
                    m_categories.append({ name.toLower(), name });
                }
            }
        }
    }
    if (m_categories.isEmpty()) {
        m_categories = defaultCategories();
    }
    bool hasOther = false;
    for (const auto& cat : m_categories) {
        if (cat.id == QLatin1String("other")) {
            hasOther = true;
            break;
        }
    }
    if (!hasOther) {
        m_categories.append({ QStringLiteral("other"), QStringLiteral("Other") });
    }

    QHash<QString, QJsonObject> byFile;
    const auto entries = m_root.value("entries").toArray();
    for (const auto& value : entries) {
        const auto object = value.toObject();
        const QString file = object.value("file").toString();
        if (!file.isEmpty())
            byFile.insert(file, object);
    }

    QDir directory(dir);
    QStringList files;
    if (directory.exists()) {
        const auto suffixes = archiveSuffixes();
        files = directory.entryList(QDir::Files | QDir::Readable, QDir::Name);
        files.erase(std::remove_if(files.begin(), files.end(),
                                   [&suffixes](const QString& name) {
                                       return !suffixes.contains(QFileInfo(name).suffix().toLower());
                                   }),
                    files.end());
    }

    // 「已安装」：拿每个地图实例的 map.json 解析出压缩包，按文件名对上
    QHash<QString, QStringList> installed;
    if (APPLICATION != nullptr && APPLICATION->instances() != nullptr) {
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); ++i) {
            auto* instance = instances->at(i);
            if (instance == nullptr)
                continue;
            const QString metaPath = FS::PathCombine(instance->instanceRoot(), MapMeta::fileName());
            if (!QFile::exists(metaPath))
                continue;
            const auto meta = MapMeta::load(instance->instanceRoot());
            const QString archive = MapRepository::resolveArchivePath(meta);
            if (archive.isEmpty())
                continue;
            installed[QFileInfo(archive).fileName()].append(instance->id());
        }
    }

    auto categoryExists = [this](const QString& id) {
        return std::any_of(m_categories.begin(), m_categories.end(), [&](const CategoryInfo& c) {
            return c.id == id;
        });
    };

    for (const auto& file : files) {
        LibraryItem item;
        if (byFile.contains(file)) {
            item = itemFromJson(byFile.value(file), dir);
            if (!categoryExists(item.category))
                item.category = QStringLiteral("other");
        } else {
            // 目录里有、索引里没有：当成新图，归 other，等玩家拖到合适的分类
            item.file = file;
            item.path = FS::PathCombine(dir, file);
            item.category = QStringLiteral("other");
            item.bytes = QFileInfo(item.path).size();
        }
        if (item.bytes <= 0)
            item.bytes = QFileInfo(item.path).size();
        item.installedInstances = installed.value(file);
        item.installed = !item.installedInstances.isEmpty();
        m_items.append(item);
    }

    // 已安装的排前面，再按显示名
    std::sort(m_items.begin(), m_items.end(), [](const LibraryItem& a, const LibraryItem& b) {
        if (a.installed != b.installed)
            return a.installed;
        return a.displayName().localeAwareCompare(b.displayName()) < 0;
    });

    emit changed();
}

QString MapLibrary::categoryName(const QString& id) const
{
    const QString targetId = id.trimmed().toLower();
    for (const auto& cat : m_categories) {
        if (cat.id == targetId)
            return cat.name;
    }
    return QStringLiteral("Other");
}

QString MapLibrary::addCategory(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};

    for (const auto& cat : m_categories) {
        if (cat.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return {};
    }

    QString baseId;
    for (const QChar ch : trimmed) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-'))
            baseId.append(ch.toLower());
    }
    if (baseId.isEmpty())
        baseId = QStringLiteral("cat");

    QString id = baseId;
    int suffix = 1;
    auto idExists = [this](const QString& candidate) {
        return std::any_of(m_categories.begin(), m_categories.end(), [&](const CategoryInfo& c) {
            return c.id == candidate;
        });
    };
    while (idExists(id)) {
        id = QStringLiteral("%1_%2").arg(baseId).arg(suffix++);
    }

    int insertPos = m_categories.size();
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories[i].id == QLatin1String("other")) {
            insertPos = i;
            break;
        }
    }
    m_categories.insert(insertPos, { id, trimmed });

    if (!saveIndex())
        return {};

    emit changed();
    return id;
}

bool MapLibrary::renameCategory(const QString& id, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QString targetId = id.trimmed().toLower();
    bool found = false;
    for (auto& cat : m_categories) {
        if (cat.id == targetId) {
            cat.name = trimmed;
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

bool MapLibrary::deleteCategory(const QString& id)
{
    const QString targetId = id.trimmed().toLower();
    if (targetId == QLatin1String("other"))
        return false;

    int index = -1;
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories[i].id == targetId) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return false;

    m_categories.removeAt(index);

    for (auto& item : m_items) {
        if (item.category == targetId)
            item.category = QStringLiteral("other");
    }

    auto entries = m_root.value("entries").toArray();
    for (int i = 0; i < entries.size(); ++i) {
        auto object = entries.at(i).toObject();
        if (object.value("category").toString().trimmed().toLower() == targetId) {
            object.insert("category", QStringLiteral("other"));
            entries.replace(i, object);
        }
    }
    m_root.insert("entries", entries);

    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

QList<LibraryItem> MapLibrary::items(const QString& categoryId) const
{
    if (categoryId.trimmed().isEmpty())
        return m_items;

    const QString targetId = categoryId.trimmed().toLower();
    QList<LibraryItem> out;
    for (const auto& item : m_items) {
        if (item.category == targetId)
            out.append(item);
    }
    return out;
}

QList<LibraryItem> MapLibrary::items(MapCategory category) const
{
    return items(categoryId(category));
}

int MapLibrary::count(const QString& categoryId) const
{
    return items(categoryId).size();
}

int MapLibrary::count(MapCategory category) const
{
    return count(categoryId(category));
}

bool MapLibrary::setCategory(const QString& file, const QString& categoryId)
{
    const QString targetId = categoryId.trimmed().toLower();
    bool touched = false;
    for (auto& item : m_items) {
        if (item.file != file)
            continue;
        if (item.category != targetId) {
            item.category = targetId;
            touched = true;
        }
        break;
    }
    if (!touched)
        return true;

    // 把新分类写回索引；其它字段原样保留
    auto entries = m_root.value("entries").toArray();
    for (int i = 0; i < entries.size(); ++i) {
        auto object = entries.at(i).toObject();
        if (object.value("file").toString() != file)
            continue;
        object.insert("category", targetId);
        entries.replace(i, object);
    }
    m_root.insert("entries", entries);
    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

bool MapLibrary::setCategory(const QString& file, MapCategory category)
{
    return setCategory(file, categoryId(category));
}

bool MapLibrary::saveIndex()
{
    // 用内存里的条目重建 entries：目录里已经没有的条目顺手清掉
    QHash<QString, QJsonObject> previous;
    for (const auto& value : m_root.value("entries").toArray()) {
        const auto object = value.toObject();
        previous.insert(object.value("file").toString(), object);
    }

    QJsonArray entries;
    for (const auto& item : m_items)
        entries.append(itemToJson(item, previous.value(item.file)));

    QJsonArray categoriesArray;
    for (const auto& cat : m_categories) {
        QJsonObject catObj;
        catObj.insert("id", cat.id);
        catObj.insert("name", cat.name);
        categoriesArray.append(catObj);
    }

    m_root.insert("schema", 1);
    m_root.insert("categories", categoriesArray);
    m_root.insert("entries", entries);
    m_root.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return writeIndexObject(indexPath(), m_root);
}

void MapLibrary::rememberImported(const QString& archivePath, const QString& mcVersion, const QString& englishName,
                                  const QString& chineseName)
{
    const QString file = QFileInfo(archivePath).fileName();
    if (file.isEmpty())
        return;

    QJsonObject root = readIndexObject(indexPath());
    auto entries = root.value("entries").toArray();

    bool found = false;
    for (int i = 0; i < entries.size(); ++i) {
        auto object = entries.at(i).toObject();
        if (object.value("file").toString() != file)
            continue;
        found = true;
        // 只补空字段：玩家手改过的分类/中文名不能被导入流程覆盖
        if (object.value("mcVersion").toString().isEmpty() && !mcVersion.isEmpty())
            object.insert("mcVersion", mcVersion);
        if (object.value("englishName").toString().isEmpty() && !englishName.isEmpty())
            object.insert("englishName", englishName);
        if (object.value("chineseName").toString().isEmpty() && !chineseName.isEmpty())
            object.insert("chineseName", chineseName);
        entries.replace(i, object);
        break;
    }
    if (!found) {
        QJsonObject object;
        object.insert("file", file);
        object.insert("category", categoryId(MapCategory::Other));
        object.insert("mcVersion", mcVersion);
        object.insert("englishName", englishName);
        object.insert("chineseName", chineseName);
        object.insert("mapVersion", QString());
        object.insert("note", QString());
        object.insert("bytes", static_cast<double>(QFileInfo(archivePath).size()));
        object.insert("addedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        entries.append(object);
    }

    root.insert("schema", 1);
    root.insert("entries", entries);
    root.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!writeIndexObject(indexPath(), root))
        qWarning() << "MapLibrary: 写索引失败" << indexPath();
}

}  // namespace maps
