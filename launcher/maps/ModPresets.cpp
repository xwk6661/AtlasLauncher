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
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"

namespace maps {
namespace {

constexpr int PRESET_SCHEMA = 2;

/// 一次解析最多生成多少条（含依赖）：依赖链是开放的，必须有上限，
/// 否则一个环或一个"全家桶"模组就能把整个安装流程拖成几百条。
constexpr int MAX_RESOLUTIONS = 60;

ModPresetEntry entry(const QString& project, const QString& name, const QString& note, bool enabled = true,
                     const QString& provider = QString::fromLatin1(modprovider::MODRINTH))
{
    ModPresetEntry value;
    value.provider = provider;
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
        value.insert(QStringLiteral("provider"), mod.provider);
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
        item.provider = mod.value(QStringLiteral("provider")).toString(QString::fromLatin1(modprovider::MODRINTH));
        if (item.provider.isEmpty())
            item.provider = QString::fromLatin1(modprovider::MODRINTH);
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

/// CurseForge 的 modLoaderType：1 Forge / 4 Fabric / 5 Quilt / 6 NeoForge
int curseLoaderType(const QStringList& loaders)
{
    for (const auto& loader : loaders) {
        if (loader == QLatin1String("neoforge"))
            return 6;
        if (loader == QLatin1String("forge"))
            return 1;
        if (loader == QLatin1String("quilt"))
            return 5;
        if (loader == QLatin1String("fabric"))
            return 4;
    }
    return 0;
}

ModPlatform::ModLoaderTypes loaderMask(const QStringList& loaders)
{
    ModPlatform::ModLoaderTypes mask;
    for (const auto& loader : loaders) {
        if (loader == QLatin1String("neoforge"))
            mask |= ModPlatform::NeoForge;
        else if (loader == QLatin1String("forge"))
            mask |= ModPlatform::Forge;
        else if (loader == QLatin1String("quilt"))
            mask |= ModPlatform::Quilt;
        else if (loader == QLatin1String("fabric"))
            mask |= ModPlatform::Fabric;
    }
    return mask;
}

QString textOr(const QJsonValue& value, const QString& fallback = {})
{
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? fallback : text;
}

}  // namespace

QString ModPresets::path()
{
    return FS::PathCombine(APPLICATION->dataRoot(), QStringLiteral("mod-presets.json"));
}

QString ModPresets::providerLabel(const QString& provider)
{
    if (provider == QLatin1String(modprovider::CURSEFORGE))
        return QStringLiteral("CurseForge");
    return QStringLiteral("Modrinth");
}

QList<ModPreset> ModPresets::builtin()
{
    // 只放"高版本基本通用"的模组：性能向与基础实用向。
    // note 是给玩家看的一句话说明，可以直接在「管理预设」里改。
    return {
        makePreset(QStringLiteral("fabric-performance"), QStringLiteral("性能优化（Fabric）"),
                   QStringLiteral("先把帧数稳住：渲染、区块、内存、实体四条线各一个。"),
                   QStringLiteral("fabric"),
                   {
                       entry(QStringLiteral("sodium"), QStringLiteral("Sodium"),
                             QStringLiteral("渲染重写，帧数翻倍；光影也靠它打底")),
                       entry(QStringLiteral("lithium"), QStringLiteral("Lithium"),
                             QStringLiteral("AI、方块更新、碰撞等逻辑优化，不改玩法")),
                       entry(QStringLiteral("ferrite-core"), QStringLiteral("FerriteCore"),
                             QStringLiteral("压缩方块状态的占用，明显降低内存")),
                       entry(QStringLiteral("krypton"), QStringLiteral("Krypton"),
                             QStringLiteral("网络栈优化，联机更稳、延迟更低")),
                       entry(QStringLiteral("c2me-fabric"), QStringLiteral("C2ME"),
                             QStringLiteral("并发加载区块，高速移动时不卡（目前只有 alpha）")),
                       entry(QStringLiteral("modernfix"), QStringLiteral("ModernFix"),
                             QStringLiteral("启动加速 + 内存与兼容性问题修复")),
                       entry(QStringLiteral("entityculling"), QStringLiteral("Entity Culling"),
                             QStringLiteral("跳过看不见的实体渲染，人多时收益大")),
                       entry(QStringLiteral("immediatelyfast"), QStringLiteral("ImmediatelyFast"),
                             QStringLiteral("合批渲染 HUD 与实体，减少绘制调用")),
                   }),
        makePreset(QStringLiteral("fabric-utility"), QStringLiteral("基础实用（Fabric）"),
                   QStringLiteral("菜单、配方查询、苹果皮、小地图 —— 装完就能正常玩。"),
                   QStringLiteral("fabric"),
                   {
                       entry(QStringLiteral("modmenu"), QStringLiteral("Mod Menu"),
                             QStringLiteral("模组列表，能打开各模组自己的设置页")),
                       entry(QStringLiteral("cloth-config"), QStringLiteral("Cloth Config"),
                             QStringLiteral("配置界面库，很多模组的设置页依赖它")),
                       entry(QStringLiteral("rei"), QStringLiteral("REI"),
                             QStringLiteral("配方查询：这个物品怎么合成、用在哪")),
                       entry(QStringLiteral("appleskin"), QStringLiteral("AppleSkin"),
                             QStringLiteral("显示饱食度/饱和度，以及食物能回多少饥饿值")),
                       entry(QStringLiteral("xaeros-minimap"), QStringLiteral("Xaero's Minimap"),
                             QStringLiteral("小地图与路径点，探索时找路用")),
                       entry(QStringLiteral("wthit"), QStringLiteral("WTHIT"),
                             QStringLiteral("准星指着方块/实体时显示它是什么")),
                   }),
        makePreset(QStringLiteral("forge-performance"), QStringLiteral("性能优化（Forge / NeoForge）"),
                   QStringLiteral("Forge 系的等价组合；NeoForge 实例同样适用。"), QStringLiteral("forge"),
                   {
                       entry(QStringLiteral("embeddium"), QStringLiteral("Embeddium"),
                             QStringLiteral("Sodium 的 Forge / NeoForge 移植，渲染优化")),
                       entry(QStringLiteral("modernfix"), QStringLiteral("ModernFix"),
                             QStringLiteral("启动加速 + 内存与兼容性问题修复")),
                       entry(QStringLiteral("ferrite-core"), QStringLiteral("FerriteCore"),
                             QStringLiteral("压缩方块状态的占用，明显降低内存")),
                       entry(QStringLiteral("saturn"), QStringLiteral("Saturn"),
                             QStringLiteral("内存分配优化，减少 GC 卡顿")),
                       entry(QStringLiteral("entityculling"), QStringLiteral("Entity Culling"),
                             QStringLiteral("跳过看不见的实体渲染")),
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
    // schema 1 的旧文件没有 provider 字段，照样能读（默认 Modrinth），所以只挡未来版本
    if (root.value(QStringLiteral("schema")).toInt() > PRESET_SCHEMA)
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
    : Task(parent), m_gameVersion(std::move(gameVersion)), m_loaders(std::move(loaders))
{
    for (const auto& item : entries) {
        Pending pending;
        pending.entry = item;
        m_pending.append(pending);
        m_seen.insert(item.key());
    }

    m_results.resize(m_pending.size());
    for (int i = 0; i < m_pending.size(); ++i)
        m_results[i].entry = m_pending[i].entry;
}

void ModPresetResolveTask::executeTask()
{
    m_index = 0;
    setProgress(0, m_pending.size());
    step();
}

void ModPresetResolveTask::step()
{
    if (m_index >= m_pending.size()) {
        enrichNames();
        return;
    }

    const auto& pending = m_pending.at(m_index);
    const QString label = pending.entry.name.isEmpty() ? pending.entry.project : pending.entry.name;
    setStatus(tr("正在查询 %1 …").arg(label));
    setProgress(m_index, m_pending.size());

    const bool isCurse = pending.entry.provider == QLatin1String(modprovider::CURSEFORGE);
    QUrl url;
    if (isCurse) {
        url = QUrl(QStringLiteral("%1/mods/%2/files").arg(BuildConfig.FLAME_BASE_URL, pending.entry.project));
        QUrlQuery query;
        if (!m_gameVersion.isEmpty())
            query.addQueryItem(QStringLiteral("gameVersion"), m_gameVersion);
        const int loaderType = curseLoaderType(m_loaders);
        if (loaderType > 0)
            query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(loaderType));
        query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("50"));
        url.setQuery(query);
    } else {
        url = QUrl(QStringLiteral("%1/project/%2/version").arg(BuildConfig.MODRINTH_PROD_URL, pending.entry.project));
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
    }

    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    auto job = makeShared<NetJob>(QStringLiteral("ModPreset::Resolve"), APPLICATION->network());
    job->addNetAction(action);
    m_current = job;

    connect(job.get(), &Task::succeeded, this, [this, response, isCurse] {
        if (isCurse)
            parseCurseForge(m_index, *response);
        else
            parseModrinth(m_index, *response);

        emit entryResolved(m_index);
        ++m_index;
        step();
    });
    connect(job.get(), &Task::failed, this, [this](const QString& reason) {
        m_results[m_index].error = reason.isEmpty() ? tr("查询失败") : reason;
        emit entryResolved(m_index);
        ++m_index;
        step();
    });

    job->start();
}

void ModPresetResolveTask::parseModrinth(int index, const QByteArray& data)
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
    result.packName = pack->name;
    result.version = *chosen;
    result.versionLabel = chosen->versionNumber.isEmpty() ? chosen->version : chosen->versionNumber;
    result.ok = true;

    if (!m_resolveDependencies)
        return;

    for (const auto& dependency : chosen->dependencies) {
        if (dependency.type != ModPlatform::DependencyType::REQUIRED)
            continue;
        enqueueDependency(QString::fromLatin1(modprovider::MODRINTH), dependency.addonId.toString(), pack->name, {});
    }
}

void ModPresetResolveTask::parseCurseForge(int index, const QByteArray& data)
{
    auto& result = m_results[index];

    auto document = Json::requireDocument(data, QStringLiteral("CurseForge files"));
    if (!document) {
        result.error = document.error();
        return;
    }

    const auto files = document->object().value(QStringLiteral("data")).toArray();
    const auto mask = loaderMask(m_loaders);

    std::optional<ModPlatform::IndexedVersion> release;
    std::optional<ModPlatform::IndexedVersion> any;

    for (const auto& value : files) {
        auto parsed = FlameMod::loadIndexedPackVersion(value.toObject());
        if (!parsed)
            continue;

        auto version = *parsed;
        // downloadUrl 为空 = 作者禁止第三方分发，装了也下不动，直接跳过
        if (version.downloadUrl.isEmpty())
            continue;
        if (!m_gameVersion.isEmpty() && !version.mcVersion.contains(m_gameVersion))
            continue;
        if (mask != 0 && (version.loaders & mask) == 0)
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
    pack->provider = ModPlatform::ResourceProvider::FLAME;
    pack->slug = result.entry.project;
    pack->name = result.entry.name.isEmpty() ? QStringLiteral("CurseForge #%1").arg(result.entry.project)
                                             : result.entry.name;
    pack->addonId = chosen->addonId;
    pack->websiteUrl = QStringLiteral("https://www.curseforge.com/minecraft/mc-mods/%1").arg(result.entry.project);

    result.pack = pack;
    result.packName = pack->name;
    result.version = *chosen;
    result.versionLabel = chosen->version.isEmpty() ? chosen->fileName : chosen->version;
    result.ok = true;

    if (!m_resolveDependencies)
        return;

    for (const auto& dependency : chosen->dependencies) {
        if (dependency.type != ModPlatform::DependencyType::REQUIRED)
            continue;
        enqueueDependency(QString::fromLatin1(modprovider::CURSEFORGE), dependency.addonId.toString(), pack->name, {});
    }
}

void ModPresetResolveTask::enqueueDependency(const QString& provider, const QString& project, const QString& requiredBy,
                                             const QString& nameHint)
{
    if (project.isEmpty())
        return;
    if (m_pending.size() >= MAX_RESOLUTIONS)
        return;

    const QString key = provider + QLatin1Char(':') + project;
    if (m_seen.contains(key))
        return;
    m_seen.insert(key);

    Pending pending;
    pending.entry.provider = provider;
    pending.entry.project = project;
    pending.entry.name = nameHint;
    pending.dependency = true;
    pending.requiredBy = requiredBy;
    m_pending.append(pending);

    ModPresetResolution placeholder;
    placeholder.entry = pending.entry;
    placeholder.dependency = true;
    placeholder.requiredBy = requiredBy;
    m_results.append(placeholder);

    emit dependencyAdded(m_results.size() - 1);
}

void ModPresetResolveTask::enrichNames()
{
    // 依赖只知道 id，不知道叫什么；这里批量补一次名字与简介，
    // 否则表格里全是 "CurseForge #123" 这种没法读的东西
    m_namePending.clear();
    for (int i = 0; i < m_results.size(); ++i) {
        if (m_results[i].ok && m_results[i].dependency)
            m_namePending.append(i);
    }
    if (m_namePending.isEmpty()) {
        finish();
        return;
    }

    QStringList modrinthIds;
    int curseCount = 0;
    for (int i : m_namePending) {
        if (m_results[i].entry.provider == QLatin1String(modprovider::CURSEFORGE))
            ++curseCount;
        else
            modrinthIds.append(m_results[i].entry.project);
    }

    if (!modrinthIds.isEmpty()) {
        QUrl url(QStringLiteral("%1/projects").arg(BuildConfig.MODRINTH_PROD_URL));
        QUrlQuery query;
        QStringList quoted;
        for (const auto& id : modrinthIds)
            quoted.append(QStringLiteral("\"%1\"").arg(id));
        query.addQueryItem(QStringLiteral("ids"), QStringLiteral("[%1]").arg(quoted.join(QLatin1Char(','))));
        url.setQuery(query);

        auto [action, response] = Net::ApiRequest::makeByteArray(url);
        auto job = makeShared<NetJob>(QStringLiteral("ModPreset::Names"), APPLICATION->network());
        job->addNetAction(action);
        m_current = job;
        connect(job.get(), &Task::succeeded, this, [this, response, curseCount] {
            applyEnriched(*response, QString::fromLatin1(modprovider::MODRINTH));
            if (curseCount > 0)
                enrichCurse();
            else
                finish();
        });
        connect(job.get(), &Task::failed, this, [this, curseCount] {
            if (curseCount > 0)
                enrichCurse();
            else
                finish();
        });
        job->start();
        return;
    }

    if (curseCount > 0) {
        enrichCurse();
        return;
    }

    finish();
}

void ModPresetResolveTask::enrichCurse()
{
    QJsonArray ids;
    for (int i : m_namePending) {
        if (m_results[i].entry.provider != QLatin1String(modprovider::CURSEFORGE))
            continue;
        bool ok = false;
        const int id = m_results[i].entry.project.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    if (ids.isEmpty()) {
        finish();
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("modIds"), ids);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    auto [action, response] =
        Net::ApiRequest::makeByteArray(QUrl(QStringLiteral("%1/mods").arg(BuildConfig.FLAME_BASE_URL)), payload);
    auto job = makeShared<NetJob>(QStringLiteral("ModPreset::Names"), APPLICATION->network());
    job->addNetAction(action);
    m_current = job;
    connect(job.get(), &Task::succeeded, this, [this, response] {
        applyEnriched(*response, QString::fromLatin1(modprovider::CURSEFORGE));
        finish();
    });
    connect(job.get(), &Task::failed, this, [this] { finish(); });
    job->start();
}

void ModPresetResolveTask::applyEnriched(const QByteArray& data, const QString& providerName)
{
    auto document = Json::requireDocument(data, QStringLiteral("mod names"));
    if (!document)
        return;

    const bool curse = providerName == QLatin1String(modprovider::CURSEFORGE);
    const QJsonArray rows = curse ? document->object().value(QStringLiteral("data")).toArray() : document->array();

    for (const auto& value : rows) {
        const auto row = value.toObject();
        const QString id = curse ? QString::number(row.value(QStringLiteral("id")).toInt())
                                 : textOr(row.value(QStringLiteral("id")));
        const QString name =
            curse ? textOr(row.value(QStringLiteral("name"))) : textOr(row.value(QStringLiteral("title")));
        const QString summary =
            curse ? textOr(row.value(QStringLiteral("summary"))) : textOr(row.value(QStringLiteral("description")));

        for (int index : m_namePending) {
            if (m_results[index].entry.provider != providerName || m_results[index].entry.project != id)
                continue;
            if (!name.isEmpty()) {
                m_results[index].packName = name;
                if (m_results[index].pack)
                    m_results[index].pack->name = name;
            }
            if (m_results[index].description.isEmpty())
                m_results[index].description = summary;
        }
    }
}

void ModPresetResolveTask::finish()
{
    setProgress(m_pending.size(), m_pending.size());
    emitSucceeded();
}

// ───────────────────────────── 搜索 ─────────────────────────────

ModSearchTask::ModSearchTask(QString provider, QString query, QString gameVersion, QString loader, QObject* parent)
    : Task(parent)
    , m_provider(std::move(provider))
    , m_query(std::move(query))
    , m_gameVersion(std::move(gameVersion))
    , m_loader(std::move(loader))
{
}

void ModSearchTask::executeTask()
{
    setStatus(tr("正在搜索 %1 …").arg(ModPresets::providerLabel(m_provider)));

    QUrl url;
    if (m_provider == QLatin1String(modprovider::CURSEFORGE)) {
        url = QUrl(QStringLiteral("%1/mods/search").arg(BuildConfig.FLAME_BASE_URL));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("gameId"), QStringLiteral("432"));
        query.addQueryItem(QStringLiteral("classId"), QStringLiteral("6"));  // 6 = Mods
        query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("25"));
        query.addQueryItem(QStringLiteral("sortField"), QStringLiteral("2"));  // 2 = Popularity
        query.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("desc"));
        if (!m_query.isEmpty())
            query.addQueryItem(QStringLiteral("searchFilter"), m_query);
        if (!m_gameVersion.isEmpty())
            query.addQueryItem(QStringLiteral("gameVersion"), m_gameVersion);
        const int loaderType = curseLoaderType({ m_loader });
        if (loaderType > 0)
            query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(loaderType));
        url.setQuery(query);
    } else {
        url = QUrl(QStringLiteral("%1/search").arg(BuildConfig.MODRINTH_PROD_URL));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
        if (!m_query.isEmpty())
            query.addQueryItem(QStringLiteral("query"), m_query);

        QJsonArray facets;
        facets.append(QJsonArray{ QStringLiteral("project_type:mod") });
        if (!m_gameVersion.isEmpty())
            facets.append(QJsonArray{ QStringLiteral("versions:%1").arg(m_gameVersion) });
        const QString loader = loaderFilterFor(m_loader);
        if (!loader.isEmpty())
            facets.append(QJsonArray{ QStringLiteral("categories:%1").arg(loader) });
        query.addQueryItem(QStringLiteral("facets"),
                           QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
        url.setQuery(query);
    }

    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    auto job = makeShared<NetJob>(QStringLiteral("ModPreset::Search"), APPLICATION->network());
    job->addNetAction(action);
    m_current = job;

    connect(job.get(), &Task::succeeded, this, [this, response] {
        parse(*response);
        emitSucceeded();
    });
    connect(job.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });

    job->start();
}

void ModSearchTask::parse(const QByteArray& data)
{
    auto document = Json::requireDocument(data, QStringLiteral("search"));
    if (!document)
        return;

    const bool curse = m_provider == QLatin1String(modprovider::CURSEFORGE);
    const QJsonArray rows = curse ? document->object().value(QStringLiteral("data")).toArray() : document->array();

    for (const auto& value : rows) {
        const auto row = value.toObject();
        ModSearchHit hit;
        hit.provider = m_provider;
        if (curse) {
            hit.project = QString::number(row.value(QStringLiteral("id")).toInt());
            hit.name = textOr(row.value(QStringLiteral("name")));
            hit.description = textOr(row.value(QStringLiteral("summary")));
            hit.downloads = static_cast<qint64>(row.value(QStringLiteral("downloadCount")).toDouble());
            const auto authors = row.value(QStringLiteral("authors")).toArray();
            if (!authors.isEmpty())
                hit.author = textOr(authors.first().toObject().value(QStringLiteral("name")));
        } else {
            hit.project = textOr(row.value(QStringLiteral("slug")), textOr(row.value(QStringLiteral("project_id"))));
            hit.name = textOr(row.value(QStringLiteral("title")));
            hit.description = textOr(row.value(QStringLiteral("description")));
            hit.author = textOr(row.value(QStringLiteral("author")));
            hit.downloads = static_cast<qint64>(row.value(QStringLiteral("downloads")).toDouble());
        }
        if (!hit.project.isEmpty())
            m_hits.append(hit);
    }
}

}  // namespace maps
