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
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "HallCache.h"
#include "HallMatcher.h"
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
    if (!title.isEmpty())
        return title;
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

QString LibraryItem::groupKey() const
{
    if (!hallSlug.isEmpty())
        return QStringLiteral("hall:") + hallSlug;

    QString base = englishName;
    if (base.isEmpty())
        base = chineseName;
    if (base.isEmpty())
        base = QFileInfo(file).completeBaseName();

    const QString normalized = variant::normalizeName(base);
    return QStringLiteral("local:") + (normalized.isEmpty() ? file : normalized);
}

QString LibraryItem::variantSummary() const
{
    // 统一走 variant::summary()：同一类别必须同字数（原版 / 简中 / 繁中 / 官中），
    // 别一边「简中」一边「简中汉化v1.0」——那看起来像两张不同的图。
    return variant.summary();
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
    item.title = object.value("title").toString();
    item.note = object.value("note").toString();
    item.bytes = static_cast<qint64>(object.value("bytes").toDouble());

    const auto hall = object.value("hall").toObject();
    item.hallSlug = hall.value("slug").toString().trimmed();
    item.hallVerified = item.hallSlug.isEmpty() ? false : hall.value("verified").toBool();

    if (object.contains("variant") && object.value("variant").isObject())
        item.variant = variant::Info::fromJson(object.value("variant").toObject());
    else
        item.variant = variant::inferFromFileName(item.file, item.note);
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
    // 没改过名就别往索引里塞空字段 —— 和一个空 `hall` 对象同理，看着像"改过"
    if (item.title.isEmpty())
        object.remove("title");
    else
        object.insert("title", item.title);
    object.insert("note", item.note);
    object.insert("bytes", static_cast<double>(item.bytes));

    // 大厅关联：没关联就把字段删掉，别留一个空对象让人以为关联过
    if (!item.hallSlug.isEmpty()) {
        QJsonObject hall;
        hall.insert("slug", item.hallSlug);
        hall.insert("verified", item.hallVerified);
        object.insert("hall", hall);
    } else {
        object.remove("hall");
    }
    object.insert("variant", item.variant.toJson());

    if (!object.contains("addedAt"))
        object.insert("addedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return object;
}

/**
 * 索引文件的进程内互斥。
 *
 * 正常路径上所有"读-改-写"都在 UI 线程（导入任务的登记已经挪回主线程，见
 * `MapImportTask::registerInLibrary()`），这把锁是给"以后有人在别处再读一次"
 * 留的兜底：`QSaveFile` 只保证"单次写入原子"，挡不住两个线程各自拿着旧快照覆盖。
 */
QMutex g_indexFileMutex;

QJsonObject readIndexObject(const QString& path)
{
    QMutexLocker lock(&g_indexFileMutex);
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
    QMutexLocker lock(&g_indexFileMutex);
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

bool MapLibrary::setCategoryForItems(const QStringList& files, const QString& categoryId)
{
    if (files.isEmpty())
        return true;

    const QString targetId = categoryId.trimmed().toLower();
    const QSet<QString> wanted(files.begin(), files.end());
    bool touched = false;
    for (auto& item : m_items) {
        if (!wanted.contains(item.file) || item.category == targetId)
            continue;
        item.category = targetId;
        touched = true;
    }
    if (!touched)
        return true;
    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

bool MapLibrary::setMapVersion(const QString& file, const QString& version)
{
    // 玩家可能写成「v1.0」或「1.0」：**存的时候去掉 v**，显示时统一加，
    // 否则同一张图会出现「v1.0」和「vv1.0」两种写法
    QString value = version.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        value.remove(0, 1);
    value = value.trimmed();

    for (auto& item : m_items) {
        if (item.file != file)
            continue;
        if (item.mapVersion == value)
            return true;
        item.mapVersion = value;
        if (!saveIndex())
            return false;
        emit changed();
        return true;
    }
    return false;
}

bool MapLibrary::setHallLink(const QString& file, const QString& hallSlug, bool verified)
{
    const QString slug = hallSlug.trimmed();
    bool touched = false;
    for (auto& item : m_items) {
        if (item.file != file)
            continue;
        // 解除关联时 verified 一并清掉，别留一个"已确认但没有目标"的状态
        const bool newVerified = slug.isEmpty() ? false : verified;
        if (item.hallSlug != slug || item.hallVerified != newVerified) {
            item.hallSlug = slug;
            item.hallVerified = newVerified;
            touched = true;
        }
        break;
    }
    if (!touched)
        return true;
    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

bool MapLibrary::markHallVerified(const QString& file, const QString& hallSlug)
{
    if (file.isEmpty() || hallSlug.isEmpty())
        return false;

    QJsonObject root = readIndexObject(indexPath());
    auto entries = root.value("entries").toArray();
    for (int i = 0; i < entries.size(); ++i) {
        auto object = entries.at(i).toObject();
        if (object.value("file").toString() != file)
            continue;

        QJsonObject hall;
        hall.insert("slug", hallSlug);
        hall.insert("verified", true);
        object.insert("hall", hall);
        entries.replace(i, object);
        root.insert("entries", entries);
        root.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        return writeIndexObject(indexPath(), root);
    }
    return false;
}

bool MapLibrary::setVariant(const QString& file, const variant::Info& info)
{
    for (auto& item : m_items) {
        if (item.file != file)
            continue;
        item.variant = info;
        if (!saveIndex())
            return false;
        emit changed();
        return true;
    }
    return false;
}

bool MapLibrary::setEnglishName(const QString& file, const QString& englishName)
{
    for (auto& item : m_items) {
        if (item.file != file)
            continue;
        if (item.englishName == englishName)
            return true;
        item.englishName = englishName;
        if (!saveIndex())
            return false;
        emit changed();
        return true;
    }
    return false;
}

bool MapLibrary::setTitle(const QString& file, const QString& title)
{
    return setTitleForItems({ file }, title);
}

bool MapLibrary::setTitleForItems(const QStringList& files, const QString& title)
{
    if (files.isEmpty())
        return true;

    const QString wanted = title.trimmed();
    const QSet<QString> targets(files.begin(), files.end());
    bool touched = false;
    for (auto& item : m_items) {
        if (!targets.contains(item.file) || item.title == wanted)
            continue;
        item.title = wanted;
        touched = true;
    }
    if (!touched)
        return true;
    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

bool MapLibrary::setDefaultVariant(const QString& file)
{
    QString key;
    for (const auto& item : m_items) {
        if (item.file == file) {
            key = item.groupKey();
            break;
        }
    }
    if (key.isEmpty())
        return false;

    bool touched = false;
    for (auto& item : m_items) {
        if (item.groupKey() != key)
            continue;
        const bool want = item.file == file;
        if (item.variant.isDefault != want) {
            item.variant.isDefault = want;
            touched = true;
        }
    }
    if (!touched)
        return true;
    if (!saveIndex())
        return false;

    emit changed();
    return true;
}

QList<LibraryItem> MapLibrary::groupItems(const QString& groupKeyValue) const
{
    QList<LibraryItem> result;
    for (const auto& item : m_items) {
        if (item.groupKey() == groupKeyValue)
            result << item;
    }
    return result;
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
                                  const QString& chineseName, const QString& hallSlug, const variant::Info& info)
{
    const QString file = QFileInfo(archivePath).fileName();
    if (file.isEmpty())
        return;

    QJsonObject root = readIndexObject(indexPath());
    auto entries = root.value("entries").toArray();

    // 导入时玩家明确选过的关联/语言，优先级高于已有值（但分类与中文名仍然只在空时补）
    const auto writeIdentity = [&](QJsonObject& object) {
        if (!hallSlug.isEmpty()) {
            QJsonObject hall;
            hall.insert("slug", hallSlug);
            hall.insert("verified", true);
            object.insert("hall", hall);
        }
        if (!info.isEmpty())
            object.insert("variant", info.toJson());
    };

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
        writeIdentity(object);
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
        writeIdentity(object);
        entries.append(object);
    }

    root.insert("schema", 1);
    root.insert("entries", entries);
    root.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!writeIndexObject(indexPath(), root))
        qWarning() << "MapLibrary: 写索引失败" << indexPath();
}

bool MapLibrary::removeItems(const QStringList& files, QString* error)
{
    if (files.isEmpty())
        return true;

    const QSet<QString> targets(files.begin(), files.end());

    // 先整批体检：只要有一个还挂着实例，就一条都不动（不做"删一半"）
    for (const auto& item : m_items) {
        if (!targets.contains(item.file) || !item.installed)
            continue;
        if (error != nullptr) {
            *error = QCoreApplication::translate("MapLibrary", "「%1」还有 %2 个实例在用（%3），"
                                                               "删掉库文件后这些实例将无法重装。\n"
                                                               "请先删除实例，或只删别的版本。")
                         .arg(item.displayName())
                         .arg(item.installedInstances.size())
                         .arg(item.installedInstances.join(QStringLiteral(", ")));
        }
        return false;
    }

    const QString dir = libraryDir();
    QStringList removed;
    for (const auto& item : m_items) {
        if (!targets.contains(item.file))
            continue;
        const QString path = FS::PathCombine(dir, item.file);
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error != nullptr)
                *error = QCoreApplication::translate("MapLibrary", "删不掉文件：%1").arg(QDir::toNativeSeparators(path));
            return false;
        }
        removed << item.file;
    }

    if (removed.isEmpty()) {
        if (error != nullptr)
            *error = QCoreApplication::translate("MapLibrary", "这些条目已经不在库里了。");
        return false;
    }

    const QSet<QString> drop(removed.begin(), removed.end());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (drop.contains(m_items.at(i).file))
            m_items.removeAt(i);
    }

    if (!saveIndex()) {
        if (error != nullptr)
            *error = QCoreApplication::translate("MapLibrary", "文件已删除，但索引写不进去：%1")
                         .arg(QDir::toNativeSeparators(indexPath()));
        return false;
    }

    emit changed();
    return true;
}

int MapLibrary::autoLinkUnassigned()
{
    const auto snapshot = HallCache::load();
    if (!snapshot.isValid())
        return 0;

    QHash<QString, QString> links;  // file -> slug
    for (const auto& item : m_items) {
        if (!item.hallSlug.isEmpty())
            continue;
        HallMatchQuery query;
        query.englishName = item.englishName;
        query.chineseName = item.chineseName;
        query.fileName = item.file;
        query.mcVersion = item.mcVersion;

        const auto candidates = HallMatcher::rank(snapshot.listRows, query, 2);
        if (candidates.isEmpty())
            continue;
        const double top = candidates.first().score;
        const double second = candidates.size() > 1 ? candidates.at(1).score : 0.0;
        if (top < 0.75)
            continue;
        if (candidates.size() > 1 && (top - second) < 0.15)
            continue;
        links.insert(item.file, candidates.first().slug);
    }

    if (links.isEmpty())
        return 0;

    for (auto& item : m_items) {
        const auto it = links.constFind(item.file);
        if (it == links.constEnd())
            continue;
        item.hallSlug = it.value();
        item.hallVerified = false;
    }

    if (!saveIndex())
        qWarning() << "MapLibrary: 自动关联后写索引失败" << indexPath();
    emit changed();
    return static_cast<int>(links.size());
}

namespace {

/// Windows 文件名里不能出现的字符：换成空格，最后把连续空格与首尾空格压掉
QString sanitizeFileBase(QString name)
{
    static const QString illegal = QStringLiteral("<>:\"/\\|?*");
    for (const QChar ch : illegal) {
        name.replace(ch, QLatin1Char(' '));
    }
    name.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")), QStringLiteral(" "));
    name.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    name = name.trimmed();
    // Windows 不允许文件名以点或空格结尾
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.size() > 120)
        name = name.left(120).trimmed();
    return name;
}

QString variantSuffix(const variant::Info& info)
{
    if (info.kind != variant::Kind::Localized)
        return {};
    if (!info.label.trimmed().isEmpty())
        return QStringLiteral("（%1）").arg(info.label.trimmed());
    switch (info.language) {
        case variant::Language::SimplifiedChinese:
            return QStringLiteral("（简中）");
        case variant::Language::TraditionalChinese:
            return QStringLiteral("（繁中）");
        default:
            return {};
    }
}

}  // namespace

int MapLibrary::normalizeFileNames(QString* error)
{
    const QString dir = libraryDir();

    struct Plan {
        int index = -1;
        QString from;
        QString to;
    };
    QList<Plan> plans;
    QSet<QString> used;  // 已占用的目标名（小写比较，Windows 不区分大小写）

    for (int i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items.at(i);
        QString base = item.englishName.trimmed();
        if (base.isEmpty())
            base = item.chineseName.trimmed();
        if (base.isEmpty())
            base = QFileInfo(item.file).completeBaseName();

        QString stem;
        if (!item.mcVersion.trimmed().isEmpty())
            stem += QStringLiteral("[%1]").arg(item.mcVersion.trimmed());
        stem += sanitizeFileBase(base);
        if (!item.mapVersion.trimmed().isEmpty())
            stem += QStringLiteral(" ") + sanitizeFileBase(item.mapVersion);
        stem += variantSuffix(item.variant);
        if (stem.isEmpty())
            continue;

        const QString suffix = QFileInfo(item.file).suffix();
        QString target = suffix.isEmpty() ? stem : stem + QLatin1Char('.') + suffix;

        // 同名（含与别人撞名）就加序号，别覆盖
        QString unique = target;
        int n = 2;
        while (used.contains(unique.toLower()) ||
               (QFile::exists(FS::PathCombine(dir, unique)) && FS::PathCombine(dir, unique) != FS::PathCombine(dir, item.file))) {
            const QString numbered = suffix.isEmpty()
                                         ? QStringLiteral("%1 (%2)").arg(stem).arg(n)
                                         : QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(suffix);
            unique = numbered;
            ++n;
        }
        used.insert(unique.toLower());

        if (unique != item.file)
            plans.append({ i, item.file, unique });
    }

    if (plans.isEmpty())
        return 0;

    int renamed = 0;
    for (const auto& plan : plans) {
        const QString from = FS::PathCombine(dir, plan.from);
        const QString to = FS::PathCombine(dir, plan.to);
        if (!QFile::rename(from, to)) {
            if (error != nullptr)
                *error = QCoreApplication::translate("MapLibrary", "改名失败：%1 → %2")
                             .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to));
            break;
        }

        m_items[plan.index].file = plan.to;
        m_items[plan.index].path = to;

        // 实例侧：map.json 里存的绝对路径跟着改，否则重装会说"找不到来源压缩包"
        if (APPLICATION != nullptr && APPLICATION->instances() != nullptr) {
            auto* instances = APPLICATION->instances();
            for (int i = 0; i < instances->count(); ++i) {
                auto* instance = instances->at(i);
                if (instance == nullptr)
                    continue;
                const QString root = instance->instanceRoot();
                if (!QFile::exists(FS::PathCombine(root, MapMeta::fileName())))
                    continue;
                QString loadError;
                auto meta = MapMeta::load(root, &loadError);
                if (!loadError.isEmpty())
                    continue;
                bool touched = false;
                if (!meta.archive.libraryCopy.isEmpty() && QFileInfo(meta.archive.libraryCopy) == QFileInfo(from)) {
                    meta.archive.libraryCopy = to;
                    touched = true;
                }
                if (!meta.archive.path.isEmpty() && QFileInfo(meta.archive.path) == QFileInfo(from)) {
                    meta.archive.path = to;
                    touched = true;
                }
                if (touched) {
                    QString saveError;
                    if (!meta.save(root, &saveError))
                        qWarning() << "MapLibrary: 改名后写 map.json 失败" << root << saveError;
                }
            }
        }

        ++renamed;
    }

    if (renamed > 0) {
        if (!saveIndex()) {
            if (error != nullptr)
                *error = QCoreApplication::translate("MapLibrary", "文件已改名，但索引写不进去：%1")
                             .arg(QDir::toNativeSeparators(indexPath()));
        }
        emit changed();
    }
    return renamed;
}

}  // namespace maps
