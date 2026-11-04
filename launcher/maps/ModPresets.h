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
#include <QString>
#include <QStringList>

#include "modplatform/ModIndex.h"
#include "tasks/Task.h"

namespace maps {

/**
 * 预设里的一条模组。
 *
 * 只存 Modrinth 的 project slug（玩家在浏览器里看到的那段地址），不存版本号：
 * 版本必须按**每个实例自己的 MC 版本 + 加载器**现查，存死了就会装错。
 */
struct ModPresetEntry {
    QString project;
    /// 玩家可改的显示名；空则用 Modrinth 返回的名字
    QString name;
    QString note;
    /// 打开预设时默认是否勾选（"可选模组"可以先不装）
    bool enabled = true;
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
};

/// 一条模组的解析结果
struct ModPresetResolution {
    ModPresetEntry entry;
    bool ok = false;
    QString error;
    QString packName;
    QString versionLabel;
    ModPlatform::IndexedPack::Ptr pack;
    ModPlatform::IndexedVersion version;
};

/**
 * 把预设里的 slug 逐个解析成「这个实例能装的那个文件」。
 *
 * 串行请求（一条失败不影响后面）：Modrinth 的版本接口一次只能查一个项目，
 * 并发发十条既容易触发限流，也会让失败原因难以归到某一行。
 */
class ModPresetResolveTask : public Task {
    Q_OBJECT

   public:
    ModPresetResolveTask(QList<ModPresetEntry> entries, QString gameVersion, QStringList loaders, QObject* parent = nullptr);

    const QList<ModPresetResolution>& results() const { return m_results; }

    void executeTask() override;

   signals:
    /// 每解析完一条就发一次，界面可以逐行更新，不用等全部完成
    void entryResolved(int index);

   private:
    void fetchNext();
    void parseResponse(int index, const QByteArray& data);

    QList<ModPresetEntry> m_entries;
    QList<ModPresetResolution> m_results;
    QString m_gameVersion;
    QStringList m_loaders;
    int m_index = 0;
    Task::Ptr m_current;
};

}  // namespace maps
