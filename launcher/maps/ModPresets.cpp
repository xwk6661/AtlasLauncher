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
#include "ModPresets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrlQuery>

#include <memory>
#include <optional>

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "Json.h"
#include "QObjectPtr.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"

namespace maps {
namespace {

constexpr int PRESET_SCHEMA = 1;

ModPresetEntry entry(const QString& project, const QString& name = {}, const QString& note = {}, bool enabled = true)
{
    ModPresetEntry value;
    value.project = project;
    value.name = name;
    value.note = note;
    value.enabled = enabled;
    return value;
}

ModPreset makePreset(const QString& id, const QString& name, const QString& description, const QString& loader,
                     QList<ModPresetEntry> mods)
{
    ModPreset preset;
    preset.id = id;
    preset.name = name;
    preset.description = description;
    preset.loader = loader;
    preset.mods = std::move(mods);
    return preset;
}

QJsonObject toJson(const ModPreset& preset)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), preset.id);
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("description"), preset.description);
    object.insert(QStringLiteral("loader"), preset.loader);

    QJsonArray mods;
    for (const auto& mod : preset.mods) {
        QJsonObject value;
        value.insert(QStringLiteral("project"), mod.project);
        if (!mod.name.isEmpty())
            value.insert(QStringLiteral("name"), mod.name);
        if (!mod.note.isEmpty())
            value.insert(QStringLiteral("note"), mod.note);
        value.insert(QStringLiteral("enabled"), mod.enabled);
        mods.append(value);
    }
    object.insert(QStringLiteral("mods"), mods);
    return object;
}

ModPreset fromJson(const QJsonObject& object)
{
    ModPreset preset;
    preset.id = object.value(QStringLiteral("id")).toString();
    preset.name = object.value(QStringLiteral("name")).toString();
    preset.description = object.value(QStringLiteral("description")).toString();
    preset.loader = object.value(QStringLiteral("loader")).toString(QStringLiteral("any"));
    if (preset.loader.isEmpty())
        preset.loader = QStringLiteral("any");

    for (const auto& value : object.value(QStringLiteral("mods")).toArray()) {
        const auto mod = value.toObject();
        ModPresetEntry item;
        item.project = mod.value(QStringLiteral("project")).toString().trimmed();
        item.name = mod.value(QStringLiteral("name")).toString();
        item.note = mod.value(QStringLiteral("note")).toString();
        item.enabled = mod.value(QStringLiteral("enabled")).toBool(true);
        if (!item.project.isEmpty())
            preset.mods.append(item);
    }
    return preset;
}

QString loaderFilterFor(const QString& loader)
{
    // Quilt 能跑 Fabric 模组；NeoForge 不兼容旧 Forge 模组，所以不互相回退
    if (loader == QLatin1String("quilt"))
        return QStringLiteral("quilt");
    return loader;
}

}  // namespace

QString ModPresets::path()
{
    return FS::PathCombine(APPLICATION->dataRoot(), QStringLiteral("mod-presets.json"));
}

QList<ModPreset> ModPresets::builtin()
{
    // 只放"高版本基本通用"的模组：性能向与基础实用向。
    // 每个条目都是 Modrinth 的 project slug，装的时候按实例版本现查文件。
    return {
        makePreset(QStringLiteral("fabric-performance"), QStringLiteral("性能优化（Fabric）"),
                   QStringLiteral("先把帧数稳住：渲染、区块、内存、实体四条线各一个。"),
                   QStringLiteral("fabric"),
                   {
                       entry(QStringLiteral("sodium"), QStringLiteral("Sodium"), QStringLiteral("渲染优化")),
                       entry(QStringLiteral("lithium"), QStringLiteral("Lithium"), QStringLiteral("服务端逻辑优化")),
                       entry(QStringLiteral("ferrite-core"), QStringLiteral("FerriteCore"), QStringLiteral("降低内存占用")),
                       entry(QStringLiteral("krypton"), QStringLiteral("Krypton"), QStringLiteral("网络栈优化")),
                       entry(QStringLiteral("c2me-fabric"), QStringLiteral("C2ME"), QStringLiteral("并发区块加载（目前只有 alpha）")),
                       entry(QStringLiteral("modernfix"), QStringLiteral("ModernFix"), QStringLiteral("启动与内存修复")),
                       entry(QStringLiteral("entityculling"), QStringLiteral("Entity Culling"), QStringLiteral("剔除看不见的实体")),
                       entry(QStringLiteral("immediatelyfast"), QStringLiteral("ImmediatelyFast"),
                             QStringLiteral("即时渲染优化")),
                   }),
        makePreset(QStringLiteral("fabric-utility"), QStringLiteral("基础实用（Fabric）"),
                   QStringLiteral("菜单、配方查询、苹果皮、小地图 —— 装完就能正常玩。"),
                   QStringLiteral("fabric"),
                   {
                       entry(QStringLiteral("modmenu"), QStringLiteral("Mod Menu"), QStringLiteral("模组列表")),
                       entry(QStringLiteral("cloth-config"), QStringLiteral("Cloth Config"), QStringLiteral("配置库")),
                       entry(QStringLiteral("rei"), QStringLiteral("REI"), QStringLiteral("配方查询")),
                       entry(QStringLiteral("appleskin"), QStringLiteral("AppleSkin"), QStringLiteral("饱食度显示")),
                       entry(QStringLiteral("xaeros-minimap"), QStringLiteral("Xaero's Minimap"), QStringLiteral("小地图")),
                       entry(QStringLiteral("wthit"), QStringLiteral("WTHIT"), QStringLiteral("方块信息")),
                   }),
        makePreset(QStringLiteral("forge-performance"), QStringLiteral("性能优化（Forge / NeoForge）"),
                   QStringLiteral("Forge 系的等价组合；NeoForge 实例同样适用。"), QStringLiteral("forge"),
                   {
                       entry(QStringLiteral("embeddium"), QStringLiteral("Embeddium"), QStringLiteral("渲染优化")),
                       entry(QStringLiteral("modernfix"), QStringLiteral("ModernFix"), QStringLiteral("启动与内存修复")),
                       entry(QStringLiteral("ferrite-core"), QStringLiteral("FerriteCore"), QStringLiteral("降低内存占用")),
                       entry(QStringLiteral("saturn"), QStringLiteral("Saturn"), QStringLiteral("内存分配优化")),
                       entry(QStringLiteral("entityculling"), QStringLiteral("Entity Culling"), QStringLiteral("剔除看不见的实体")),
                   }),
    };
}

QList<ModPreset> ModPresets::load()
{
    QFile file(path());
    if (!file.exists()) {
        const auto defaults = builtin();
        save(defaults);
        return defaults;
    }
    if (!file.open(QIODevice::ReadOnly))
        return builtin();

    const auto document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject())
        return builtin();

    const auto root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != PRESET_SCHEMA)
        return builtin();

    QList<ModPreset> presets;
    for (const auto& value : root.value(QStringLiteral("presets")).toArray()) {
        auto preset = fromJson(value.toObject());
        if (preset.isValid())
            presets.append(preset);
    }
    return presets.isEmpty() ? builtin() : presets;
}

bool ModPresets::save(const QList<ModPreset>& presets, QString* error)
{
    QJsonArray array;
    for (const auto& preset : presets) {
        if (preset.isValid())
            array.append(toJson(preset));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), PRESET_SCHEMA);
    root.insert(QStringLiteral("presets"), array);

    const QString target = path();
    QDir().mkpath(QFileInfo(target).absolutePath());

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool ModPresets::matchesLoader(const QString& presetLoader, const QString& instanceLoader)
{
    if (presetLoader.isEmpty() || presetLoader == QLatin1String("any"))
        return true;
    if (presetLoader.compare(instanceLoader, Qt::CaseInsensitive) == 0)
        return true;
    // Quilt 跑 Fabric 模组是官方支持的
    if (presetLoader == QLatin1String("fabric") && instanceLoader == QLatin1String("quilt"))
        return true;
    // 新版 Forge 与 NeoForge 互不兼容，但同一个 Modrinth 项目通常两边都发文件，
    // 所以预设写 "forge" 也能用在 NeoForge 实例上（查询时仍按 neoforge 过滤）
    if (presetLoader == QLatin1String("forge") && instanceLoader == QLatin1String("neoforge"))
        return true;
    return false;
}

// ───────────────────────────── 解析任务 ─────────────────────────────

ModPresetResolveTask::ModPresetResolveTask(QList<ModPresetEntry> entries, QString gameVersion, QStringList loaders,
                                           QObject* parent)
    : Task(parent), m_entries(std::move(entries)), m_gameVersion(std::move(gameVersion)), m_loaders(std::move(loaders))
{
    m_results.resize(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        m_results[i].entry = m_entries.at(i);
}

void ModPresetResolveTask::executeTask()
{
    m_index = 0;
    setProgress(0, m_entries.size());
    fetchNext();
}

void ModPresetResolveTask::fetchNext()
{
    if (m_index >= m_entries.size()) {
        setProgress(m_entries.size(), m_entries.size());
        emitSucceeded();
        return;
    }

    const auto& entry = m_entries.at(m_index);
    const QString label = entry.name.isEmpty() ? entry.project : entry.name;
    setStatus(tr("正在查询 %1 …").arg(label));
    setProgress(m_index, m_entries.size());

    QUrl url(QStringLiteral("%1/project/%2/version").arg(BuildConfig.MODRINTH_PROD_URL, entry.project));
    QUrlQuery query;
    if (!m_gameVersion.isEmpty())
        query.addQueryItem(QStringLiteral("game_versions"), QStringLiteral("[\"%1\"]").arg(m_gameVersion));

    QStringList loaders;
    for (const auto& loader : m_loaders) {
        const QString id = loaderFilterFor(loader);
        if (!id.isEmpty())
            loaders.append(QStringLiteral("\"%1\"").arg(id));
    }
    if (!loaders.isEmpty())
        query.addQueryItem(QStringLiteral("loaders"), QStringLiteral("[%1]").arg(loaders.join(QLatin1Char(','))));
    url.setQuery(query);

    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    auto job = makeShared<NetJob>(QStringLiteral("Modrinth::PresetVersion"), APPLICATION->network());
    job->addNetAction(action);
    m_current = job;

    connect(job.get(), &Task::succeeded, this, [this, response] {
        parseResponse(m_index, *response);
        emit entryResolved(m_index);
        ++m_index;
        fetchNext();
    });
    connect(job.get(), &Task::failed, this, [this](const QString& reason) {
        m_results[m_index].error = reason.isEmpty() ? tr("查询失败") : reason;
        emit entryResolved(m_index);
        ++m_index;
        fetchNext();
    });

    job->start();
}

void ModPresetResolveTask::parseResponse(int index, const QByteArray& data)
{
    auto& result = m_results[index];

    auto document = Json::requireDocument(data, QStringLiteral("Modrinth versions"));
    if (!document) {
        result.error = document.error();
        return;
    }
    if (!document->isArray()) {
        result.error = tr("接口返回的不是版本列表");
        return;
    }

    std::optional<ModPlatform::IndexedVersion> release;
    std::optional<ModPlatform::IndexedVersion> any;

    for (const auto& value : document->array()) {
        auto parsed = Modrinth::loadIndexedPackVersion(value.toObject());
        if (!parsed)
            continue;

        auto version = *parsed;
        // 接口已经按条件过滤过，但缓存/代理偶尔会回整表，这里再挡一次
        if (!m_gameVersion.isEmpty() && !version.mcVersion.contains(m_gameVersion))
            continue;

        if (version.versionType == ModPlatform::IndexedVersionType::Release) {
            if (!release)
                release = version;
        } else if (!any) {
            any = version;
        }
    }

    const auto chosen = release ? release : any;
    if (!chosen) {
        result.error = tr("没有适配 %1 / %2 的版本").arg(m_gameVersion, m_loaders.join(QStringLiteral("、")));
        return;
    }

    auto pack = std::make_shared<ModPlatform::IndexedPack>();
    pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    pack->slug = result.entry.project;
    pack->name = result.entry.name.isEmpty() ? result.entry.project : result.entry.name;
    pack->addonId = chosen->addonId;
    pack->websiteUrl = QStringLiteral("https://modrinth.com/mod/%1").arg(result.entry.project);

    result.pack = pack;
    result.version = *chosen;
    result.versionLabel = chosen->versionNumber.isEmpty() ? chosen->version : chosen->versionNumber;
    result.ok = true;
}

}  // namespace maps
