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
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "modplatform/ModIndex.h"
#include "tasks/Task.h"

namespace maps {

/// 预设条目来自哪个平台
namespace modprovider {
inline constexpr const char* MODRINTH = "modrinth";
inline constexpr const char* CURSEFORGE = "curseforge";
}  // namespace modprovider

/**
 * 预设里的一条模组。
 *
 * 只存平台与项目 id（Modrinth 用 slug，CurseForge 用数字 modId），不存版本号：
 * 版本必须按**每个实例自己的 MC 版本 + 加载器**现查，存死了就会装错。
 */
struct ModPresetEntry {
    /// modrinth / curseforge
    QString provider = QString::fromLatin1(::maps::modprovider::MODRINTH);
    /// Modrinth slug（或 project id）/ CurseForge modId
    QString project;
    /// 玩家可改的显示名；空则用平台返回的名字
    QString name;
    /// 一句话说明这个模组干什么（玩家可改；搜索添加时自动填平台的简介）
    QString note;
    /// 打开预设时默认是否勾选（"可选模组"可以先不装）
    bool enabled = true;

    /// 去重用的唯一键
    QString key() const { return provider + QLatin1Char(':') + project; }
};

/// 一组模组
struct ModPreset {
    QString id;
    QString name;
    QString description;
    /// fabric / forge / neoforge / quilt / any；"forge" 同时覆盖 NeoForge
    QString loader = QStringLiteral("any");
    QList<ModPresetEntry> mods;

    bool isValid() const { return !name.isEmpty() && !mods.isEmpty(); }
};

/**
 * 预设的存取：`<数据目录>/mod-presets.json`。
 *
 * 为什么放数据目录而不是随包 qrc：这是**玩家的**东西，要能改、能备份、能跟着
 * 数据目录迁移。第一次运行且文件不存在时写入内置的三套，之后完全以文件为准。
 */
class ModPresets {
   public:
    static QString path();

    /// 读；文件不存在时写入内置默认并返回（写失败也返回默认，不阻塞使用）
    static QList<ModPreset> load();
    static bool save(const QList<ModPreset>& presets, QString* error = nullptr);

    /// 内置默认：性能（Fabric）、实用（Fabric）、性能（Forge / NeoForge）
    static QList<ModPreset> builtin();

    /// 预设的加载器是否适用于这个实例（any 总是适用）
    static bool matchesLoader(const QString& presetLoader, const QString& instanceLoader);

    /// 平台显示名："Modrinth" / "CurseForge"
    static QString providerLabel(const QString& provider);
};

/// 一条模组的解析结果
struct ModPresetResolution {
    ModPresetEntry entry;
    bool ok = false;
    /// 这条是被别的模组拉进来的依赖，不是玩家直接选的
    bool dependency = false;
    /// 依赖方显示名（"Fabric API" / "Sodium"）
    QString requiredBy;
    QString error;
    QString packName;
    /// 平台简介（预设没写 note 时用来填「说明」列）
    QString description;
    QString versionLabel;
    ModPlatform::IndexedPack::Ptr pack;
    ModPlatform::IndexedVersion version;
};

/**
 * 把预设里的条目逐个解析成「这个实例能装的那个文件」。
 *
 * - Modrinth 走 `/project/<slug>/version`，CurseForge 走 `/mods/<id>/files`；
 * - 串行请求（一条失败不影响后面），因为一次查十几条并发发出去既容易触发限流，
 *   也会让失败原因难以归到某一行；
 * - **严格匹配**：版本/加载器对不上就标错，绝不拿近似版本凑数；
 * - 默认递归安装 required 依赖（可关），依赖的依赖也装，带环检测与数量上限。
 */
class ModPresetResolveTask : public Task {
    Q_OBJECT

   public:
    ModPresetResolveTask(QList<ModPresetEntry> entries, QString gameVersion, QStringList loaders, QObject* parent = nullptr);

    const QList<ModPresetResolution>& results() const { return m_results; }

    /// 关掉之后只解析玩家选的条目（不拉依赖）
    void setResolveDependencies(bool on) { m_resolveDependencies = on; }

    void executeTask() override;

   signals:
    /// 每解析完一条就发一次，界面可以逐行更新；依赖会在解析过程中追加行
    void entryResolved(int index);
    /// 依赖被追加进结果列表（index 指向新行）
    void dependencyAdded(int index);

   private:
    struct Pending {
        ModPresetEntry entry;
        bool dependency = false;
        QString requiredBy;
    };

    void step();
    void finish();
    void parseModrinth(int index, const QByteArray& data);
    void parseCurseForge(int index, const QByteArray& data);
    void enrichNames();
    void enrichCurse();
    void applyEnriched(const QByteArray& data, const QString& providerName);

    void enqueueDependency(const QString& provider, const QString& project, const QString& requiredBy,
                           const QString& nameHint);

    QList<Pending> m_pending;
    QList<ModPresetResolution> m_results;
    QSet<QString> m_seen;
    QString m_gameVersion;
    QStringList m_loaders;
    int m_index = 0;
    bool m_resolveDependencies = true;
    Task::Ptr m_current;

    /// 名字待补的条目下标（依赖只知道 id，不知道叫什么）
    QList<int> m_namePending;
};

/**
 * 平台搜索：给「搜索并添加」用。
 *
 * 结果只保留装模组需要的字段，不引入整个 ResourceAPI 的索引模型 —— 这里不需要
 * 分页、分类树、版本切换那一套。
 */
struct ModSearchHit {
    QString provider;
    QString project;
    QString name;
    QString author;
    QString description;
    qint64 downloads = 0;
};

class ModSearchTask : public Task {
    Q_OBJECT

   public:
    ModSearchTask(QString provider, QString query, QString gameVersion, QString loader, QObject* parent = nullptr);

    const QList<ModSearchHit>& hits() const { return m_hits; }

    void executeTask() override;

   private:
    void parse(const QByteArray& data);

    QString m_provider;
    QString m_query;
    QString m_gameVersion;
    QString m_loader;
    QList<ModSearchHit> m_hits;
    Task::Ptr m_current;
};

}  // namespace maps
