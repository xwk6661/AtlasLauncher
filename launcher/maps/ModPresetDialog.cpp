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
#include "ModPresetDialog.h"

#include "ModSearchDialog.h"

#include "modplatform/packwiz/Packwiz.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "ResourceDownloadTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "tasks/ConcurrentTask.h"
#include "ui/dialogs/InstallLoaderDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/ResourceDownloadDialog.h"

namespace maps {
namespace {

struct InstanceTarget {
    QString mcVersion;
    QString loader;
};

InstanceTarget targetFor(MinecraftInstance* instance)
{
    InstanceTarget target;
    auto* profile = instance != nullptr ? instance->getPackProfile() : nullptr;
    if (profile == nullptr)
        return target;

    target.mcVersion = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    // 顺序有讲究：Quilt 继承了 Fabric 的组件，NeoForge 与 Forge 也常同时出现
    if (!profile->getComponentVersion(QStringLiteral("org.quiltmc.quilt-loader")).isEmpty())
        target.loader = QStringLiteral("quilt");
    else if (!profile->getComponentVersion(QStringLiteral("net.neoforged")).isEmpty())
        target.loader = QStringLiteral("neoforge");
    else if (!profile->getComponentVersion(QStringLiteral("net.minecraftforge")).isEmpty())
        target.loader = QStringLiteral("forge");
    else if (!profile->getComponentVersion(QStringLiteral("net.fabricmc.fabric-loader")).isEmpty())
        target.loader = QStringLiteral("fabric");
    return target;
}

QString loaderDisplayName(const QString& loader)
{
    if (loader == QLatin1String("fabric"))
        return QStringLiteral("Fabric");
    if (loader == QLatin1String("quilt"))
        return QStringLiteral("Quilt");
    if (loader == QLatin1String("forge"))
        return QStringLiteral("Forge");
    if (loader == QLatin1String("neoforge"))
        return QStringLiteral("NeoForge");
    return QStringLiteral("Vanilla");
}

/// 查询时带上的 loader 列表：Quilt 能直接跑 Fabric 模组
QStringList queryLoaders(const QString& loader)
{
    if (loader.isEmpty())
        return {};
    if (loader == QLatin1String("quilt"))
        return { QStringLiteral("quilt"), QStringLiteral("fabric") };
    return { loader };
}

}  // namespace

void ModPresetDialog::run(MinecraftInstance* instance, QWidget* parent)
{
    if (instance == nullptr)
        return;

    auto target = targetFor(instance);
    if (target.loader.isEmpty()) {
        const auto answer = QMessageBox::question(
            parent, QObject::tr("需要先装模组加载器"),
            QObject::tr("这个实例是 Vanilla，原版游戏不加载模组。\n要现在装一个加载器吗？装完就能继续用预设。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;

        auto* profile = instance->getPackProfile();
        if (profile == nullptr)
            return;
        InstallLoaderDialog dialog(profile, QString(), parent);
        if (dialog.exec() == 0)
            return;

        target = targetFor(instance);
        if (target.loader.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("没有装上加载器"),
                                 QObject::tr("加载器没有安装成功，先装好再回来用预设。"));
            return;
        }
    }

    ModPresetDialog dialog(instance, parent);
    dialog.exec();
}

ModPresetDialog::ModPresetDialog(MinecraftInstance* instance, QWidget* parent) : QDialog(parent), m_instance(instance)
{
    const auto target = targetFor(instance);
    m_mcVersion = target.mcVersion;
    m_loader = target.loader;

    setWindowTitle(tr("模组预设"));
    resize(640, 520);
    buildUi();
    collectInstalled();
    reloadPresets();
}

void ModPresetDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("预设："), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    presetRow->addWidget(m_presetCombo, 1);

    auto* editButton = new QPushButton(tr("管理预设…"), this);
    connect(editButton, &QPushButton::clicked, this, &ModPresetDialog::editPresets);
    presetRow->addWidget(editButton);
    layout->addLayout(presetRow);

    m_target = new QLabel(this);
    m_target->setEnabled(false);
    layout->addWidget(m_target);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setEnabled(false);
    layout->addWidget(m_description);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("装"), tr("模组"), tr("说明"), tr("版本") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* actionRow = new QHBoxLayout();
    m_selectAll = new QPushButton(tr("全选"), this);
    m_selectNone = new QPushButton(tr("全不选"), this);
    connect(m_selectAll, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (row < m_results.size() && m_results.at(row).ok)
                m_table->item(row, 0)->setCheckState(Qt::Checked);
        }
        updateSummary();
    });
    connect(m_selectNone, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < m_table->rowCount(); ++row)
            m_table->item(row, 0)->setCheckState(Qt::Unchecked);
        updateSummary();
    });
    actionRow->addWidget(m_selectAll);
    actionRow->addWidget(m_selectNone);

    m_browse = new QPushButton(tr("自己挑…"), this);
    m_browse->setToolTip(tr("打开原生模组浏览器：搜索、看详情、逐版本挑选。"));
    connect(m_browse, &QPushButton::clicked, this, &ModPresetDialog::openModBrowser);
    actionRow->addWidget(m_browse);

    actionRow->addStretch(1);

    auto* close = new QPushButton(tr("关闭"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    actionRow->addWidget(close);

    m_installButton = new QPushButton(tr("安装勾选的模组"), this);
    m_installButton->setDefault(true);
    connect(m_installButton, &QPushButton::clicked, this, &ModPresetDialog::install);
    actionRow->addWidget(m_installButton);
    layout->addLayout(actionRow);

    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_syncingChecks || item == nullptr || item->column() != 0)
            return;
        const int row = item->row();
        if (row < 0 || row >= m_results.size())
            return;

        m_syncingChecks = true;
        QSet<QString> visited;
        setDependentsChecked(m_results.at(row).entry.key(), item->checkState() == Qt::Checked, visited);
        m_syncingChecks = false;
        updateSummary();
    });

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { startResolve(); });
}

void ModPresetDialog::reloadPresets()
{
    m_presets = ModPresets::load();
    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    for (const auto& preset : m_presets) {
        if (!ModPresets::matchesLoader(preset.loader, m_loader))
            continue;
        m_presetCombo->addItem(preset.name, preset.id);
    }
    m_presetCombo->blockSignals(false);

    m_target->setText(tr("目标：Minecraft %1 · %2")
                          .arg(m_mcVersion.isEmpty() ? tr("版本未知") : m_mcVersion, loaderDisplayName(m_loader)));

    if (m_presetCombo->count() == 0) {
        m_description->setText(tr("没有适用于 %1 的预设。点「管理预设…」自己建一套，"
                                 "或者先在「模组」页装一个加载器。")
                                  .arg(loaderDisplayName(m_loader)));
        m_table->setRowCount(0);
        m_installButton->setEnabled(false);
        m_status->clear();
        return;
    }

    startResolve();
}

void ModPresetDialog::startResolve()
{
    const QString id = m_presetCombo->currentData().toString();
    m_current = {};
    for (const auto& preset : m_presets) {
        if (preset.id == id) {
            m_current = preset;
            break;
        }
    }
    if (!m_current.isValid()) {
        m_table->setRowCount(0);
        m_installButton->setEnabled(false);
        return;
    }

    // 切换预设时把上一次的解析任务停掉：旧请求还在飞，但它的结果不该再落到这张表上
    ++m_generation;
    if (m_task != nullptr) {
        m_task->disconnect(this);
        m_task->abort();
        m_task->deleteLater();
        m_task = nullptr;
    }

    m_description->setText(m_current.description);

    m_results.clear();
    m_results.resize(m_current.mods.size());
    for (int i = 0; i < m_current.mods.size(); ++i)
        m_results[i].entry = m_current.mods.at(i);

    rebuildTable(true);

    m_status->setText(tr("正在按 %1 / %2 解析版本…").arg(m_mcVersion, loaderDisplayName(m_loader)));
    m_installButton->setEnabled(false);

    const int generation = m_generation;
    QList<ModPresetEntry> entries = m_current.mods;
    m_task = new ModPresetResolveTask(std::move(entries), m_mcVersion, queryLoaders(m_loader), this);

    connect(m_task, &ModPresetResolveTask::entryResolved, this, [this, generation](int index) {
        if (generation != m_generation)
            return;
        refreshRow(index);
    });
    connect(m_task, &Task::succeeded, this, [this, generation] {
        if (generation != m_generation)
            return;
        // 依赖是解析过程中才发现的，完成后整表重建一次，把它们排在主条目后面
        m_results = m_task->results();
        rebuildTable(false);
        m_status->clear();
    });
    connect(m_task, &Task::failed, this, [this, generation](const QString& reason) {
        if (generation != m_generation)
            return;
        m_status->setText(tr("解析失败：%1").arg(reason));
    });
    connect(m_task, &Task::progress, this, [this, generation](qint64 done, qint64 total) {
        if (generation != m_generation)
            return;
        m_status->setText(tr("正在解析版本… %1 / %2").arg(done).arg(total));
    });

    m_task->start();
}

void ModPresetDialog::rebuildTable(bool resolving)
{
    m_syncingChecks = true;

    // 记住用户手动改过的勾选：重建时不能把人刚取消的又勾回去
    QList<int> previousStates;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* item = m_table->item(row, 0);
        previousStates.append(item != nullptr ? static_cast<int>(item->checkState()) : -1);
    }

    m_table->setRowCount(m_results.size());
    for (int row = 0; row < m_results.size(); ++row) {
        const auto& result = m_results.at(row);
        const auto& entry = result.entry;
        const bool installed = isInstalled(entry, result);

        auto* check = new QTableWidgetItem();
        check->setFlags((check->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        const int previous = row < previousStates.size() ? previousStates.at(row) : -1;
        bool checked = previous >= 0 ? previous == static_cast<int>(Qt::Checked) : entry.enabled;
        // 已经装过的默认不勾：预设是"补齐"，不是"重装一遍"
        if (installed && (previous < 0 || resolving))
            checked = false;
        check->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 0, check);

        QString name = result.packName;
        if (name.isEmpty())
            name = entry.name;
        if (name.isEmpty())
            name = entry.project;
        if (result.dependency)
            name += tr("（依赖）");
        if (installed)
            name += tr("（已安装）");
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setToolTip(entry.project);
        m_table->setItem(row, 1, nameItem);

        QString note = entry.note;
        if (note.isEmpty())
            note = result.description;
        if (result.dependency && !result.requiredBy.isEmpty()) {
            const QString owner = tr("由 %1 需要").arg(result.requiredBy);
            note = note.isEmpty() ? owner : owner + QStringLiteral(" · ") + note;
        }
        auto* noteItem = new QTableWidgetItem(note);
        noteItem->setToolTip(note);
        m_table->setItem(row, 2, noteItem);

        auto* version = new QTableWidgetItem();
        if (installed) {
            version->setText(tr("已安装 %1").arg(result.versionLabel.isEmpty() ? entry.project : result.versionLabel));
            version->setToolTip(result.packName);
        } else if (result.ok) {
            version->setText(result.versionLabel);
            version->setToolTip(result.packName);
        } else if (result.error.isEmpty()) {
            version->setText(resolving ? tr("查询中…") : tr("未解析"));
            version->setFlags(version->flags() & ~Qt::ItemIsEnabled);
        } else {
            version->setText(result.error);
            version->setFlags(version->flags() & ~Qt::ItemIsEnabled);
            check->setCheckState(Qt::Unchecked);
            check->setFlags(check->flags() & ~Qt::ItemIsEnabled);
        }
        m_table->setItem(row, 3, version);
    }

    m_syncingChecks = false;
    updateSummary();
}

void ModPresetDialog::setDependentsChecked(const QString& key, bool checked, QSet<QString>& visited)
{
    const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).requiredByKey != key)
            continue;

        auto* item = m_table->item(row, 0);
        if (item != nullptr && item->checkState() != state)
            item->setCheckState(state);

        const QString child = m_results.at(row).entry.key();
        if (visited.contains(child))
            continue;
        visited.insert(child);
        setDependentsChecked(child, checked, visited);
    }
}

void ModPresetDialog::collectInstalled()
{
    m_installed.clear();
    m_installedNames.clear();

    auto* mods = m_instance != nullptr ? m_instance->loaderModList() : nullptr;
    if (mods == nullptr)
        return;

    // 直接读 mods/.index 里的 pw.toml，而不是 ResourceFolderModel 的内存元数据：
    // 模型是异步解析的，刚打开的实例里 metadata() 可能还是空的，那样"已安装"永远标不出来。
    const QDir indexDir(FS::PathCombine(mods->dir().absolutePath(), QStringLiteral(".index")));
    if (!indexDir.exists())
        return;

    const auto files = indexDir.entryList({ QStringLiteral("*.pw.toml") }, QDir::Files);
    for (const auto& name : files) {
        QString stem = name;
        stem.remove(QStringLiteral(".pw.toml"));

        const auto mod = Packwiz::V1::getIndexForMod(indexDir, stem);
        const QString provider = mod.provider == ModPlatform::ResourceProvider::FLAME
                                     ? QString::fromLatin1(modprovider::CURSEFORGE)
                                     : QString::fromLatin1(modprovider::MODRINTH);
        const QString prefix = provider + QLatin1Char(':');

        if (!mod.projectId.toString().isEmpty())
            m_installed.insert(prefix + mod.projectId.toString());
        if (!mod.slug.isEmpty())
            m_installed.insert(prefix + mod.slug);
        m_installed.insert(prefix + stem);

        if (!mod.name.isEmpty())
            m_installedNames.insert(mod.name.trimmed().toLower());
    }
}

bool ModPresetDialog::isInstalled(const ModPresetEntry& entry, const ModPresetResolution& result) const
{
    const QString prefix = entry.provider + QLatin1Char(':');
    if (m_installed.contains(prefix + entry.project))
        return true;
    if (result.pack != nullptr && !result.pack->addonId.isNull() &&
        m_installed.contains(prefix + result.pack->addonId.toString()))
        return true;

    // 跨平台兜底：同一个模组常常两边都发（Sodium 在 Modrinth 是 slug，在 CurseForge 是 394468），
    // 严格按 provider:id 会漏掉，按名字再认一次
    const QString name = !result.packName.isEmpty() ? result.packName : entry.name;
    return !name.isEmpty() && m_installedNames.contains(name.trimmed().toLower());
}

void ModPresetDialog::refreshRow(int index)
{
    if (m_task == nullptr || index < 0 || index >= m_task->results().size() || index >= m_table->rowCount())
        return;

    // 逐条到达时先只改这一行，依赖等结束后统一重建
    const auto result = m_task->results().at(index);
    if (index < m_results.size())
        m_results[index] = result;

    auto* version = m_table->item(index, 3);
    auto* check = m_table->item(index, 0);
    if (version == nullptr || check == nullptr)
        return;

    if (result.ok) {
        version->setText(isInstalled(result.entry, result) ? tr("已安装 %1").arg(result.versionLabel)
                                                           : result.versionLabel);
        version->setFlags(version->flags() | Qt::ItemIsEnabled);
        version->setToolTip(result.packName);
    } else if (!result.error.isEmpty()) {
        version->setText(result.error);
        version->setFlags(version->flags() & ~Qt::ItemIsEnabled);
        check->setCheckState(Qt::Unchecked);
        check->setFlags(check->flags() & ~Qt::ItemIsEnabled);
    }
}

void ModPresetDialog::updateSummary()
{
    if (m_table->rowCount() == 0) {
        m_installButton->setEnabled(false);
        return;
    }

    int ready = 0;
    int selected = 0;
    const bool resolved = std::all_of(m_results.cbegin(), m_results.cend(),
                                      [](const ModPresetResolution& result) { return result.ok || !result.error.isEmpty(); });
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (row < m_results.size() && m_results.at(row).ok)
            ++ready;
        if (m_table->item(row, 0) != nullptr && m_table->item(row, 0)->checkState() == Qt::Checked)
            ++selected;
    }

    m_selectAll->setEnabled(ready > 0);
    m_selectNone->setEnabled(ready > 0);
    m_installButton->setEnabled(resolved && selected > 0);
    m_installButton->setText(selected > 0 ? tr("安装勾选的 %1 个模组").arg(selected) : tr("安装勾选的模组"));
}

void ModPresetDialog::install()
{
    QList<ModPresetResolution> chosen;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (row >= m_results.size())
            break;
        const auto& result = m_results.at(row);
        if (!result.ok)
            continue;
        if (m_table->item(row, 0) == nullptr || m_table->item(row, 0)->checkState() != Qt::Checked)
            continue;
        chosen.append(result);
    }

    if (chosen.isEmpty()) {
        QMessageBox::information(this, tr("没有可装的模组"), tr("先勾选至少一个已经解析成功的模组。"));
        return;
    }

    auto* mods = m_instance != nullptr ? m_instance->loaderModList() : nullptr;
    if (mods == nullptr) {
        QMessageBox::warning(this, tr("找不到模组目录"), tr("这个实例没有可用的 mods 目录。"));
        return;
    }

    ConcurrentTask tasks(tr("安装模组预设"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    for (const auto& result : chosen) {
        tasks.addTask(makeShared<ResourceDownloadTask>(result.pack, result.version, mods, true, QStringLiteral("preset")));
    }

    ProgressDialog progress(this);
    progress.execWithTask(&tasks);

    mods->update();

    if (tasks.getState() != Task::State::Succeeded) {
        QMessageBox::warning(this, tr("部分模组没装上"), tasks.failReason());
        return;
    }

    QStringList warnings = tasks.warnings();
    if (!warnings.isEmpty()) {
        QMessageBox::warning(this, tr("安装完成，但有提示"), warnings.join(QLatin1Char('\n')));
        return;
    }

    QMessageBox::information(this, tr("装好了"),
                             tr("已安装 %1 个模组。启动游戏即可生效。").arg(chosen.size()));
    accept();
}

void ModPresetDialog::openModBrowser()
{
    auto* mods = m_instance != nullptr ? m_instance->loaderModList() : nullptr;
    if (mods == nullptr)
        return;

    auto* dialog = ResourceDownload::ResourceDownloadDialog::createMod(this, mods, m_instance);
    connect(dialog, &QDialog::finished, this, [this, dialog, mods](int result) {
        if (result != 0) {
            // 与模组页同一条路径：把对话框挑好的下载任务一起跑掉
            ConcurrentTask tasks(tr("下载模组"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
            for (auto& task : dialog->getTasks())
                tasks.addTask(task);

            ProgressDialog progress(this);
            progress.execWithTask(&tasks);
            mods->update();
        }
        dialog->deleteLater();
    });
    dialog->open();
}

void ModPresetDialog::editPresets()
{
    ModPresetEditorDialog editor(m_mcVersion, m_loader, this);
    editor.exec();

    // 编辑完可能改名/删了当前预设，重新读一遍
    const QString keep = m_presetCombo->currentData().toString();
    reloadPresets();
    const int index = m_presetCombo->findData(keep);
    if (index >= 0)
        m_presetCombo->setCurrentIndex(index);
}

// ───────────────────────────── 编辑器 ─────────────────────────────

ModPresetEditorDialog::ModPresetEditorDialog(const QString& gameVersion, const QString& loader, QWidget* parent)
    : QDialog(parent), m_gameVersion(gameVersion), m_instanceLoader(loader)
{
    setWindowTitle(tr("管理模组预设"));
    resize(620, 560);

    auto* layout = new QVBoxLayout(this);

    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("预设："), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    presetRow->addWidget(m_presetCombo, 1);

    auto* addButton = new QPushButton(tr("新建"), this);
    connect(addButton, &QPushButton::clicked, this, &ModPresetEditorDialog::addPreset);
    presetRow->addWidget(addButton);

    auto* removeButton = new QPushButton(tr("删除"), this);
    connect(removeButton, &QPushButton::clicked, this, &ModPresetEditorDialog::removeCurrent);
    presetRow->addWidget(removeButton);
    layout->addLayout(presetRow);

    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("名称："), this));
    m_name = new QLineEdit(this);
    nameRow->addWidget(m_name, 1);
    nameRow->addWidget(new QLabel(tr("加载器："), this));
    m_loader = new QComboBox(this);
    m_loader->addItem(tr("只限 Fabric"), QStringLiteral("fabric"));
    m_loader->addItem(tr("只限 Forge"), QStringLiteral("forge"));
    m_loader->addItem(tr("只限 NeoForge"), QStringLiteral("neoforge"));
    m_loader->addItem(tr("只限 Quilt"), QStringLiteral("quilt"));
    m_loader->addItem(tr("不限加载器"), QStringLiteral("any"));
    nameRow->addWidget(m_loader);
    layout->addLayout(nameRow);

    auto* descRow = new QHBoxLayout();
    descRow->addWidget(new QLabel(tr("说明："), this));
    m_description = new QLineEdit(this);
    descRow->addWidget(m_description, 1);
    layout->addLayout(descRow);

    auto* hint = new QLabel(
        tr("每行一个模组：<code>项目 | 显示名 | 说明</code>。Modrinth 填地址最后一段"
           "（<code>https://modrinth.com/mod/sodium</code> → <code>sodium</code>）；"
           "CurseForge 填数字 id 并加前缀 <code>cf:</code>（如 <code>cf:238222</code>）。"
           "后两段可以省略；行首加 <code>-</code> 表示默认不勾选。"),
        this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    hint->setEnabled(false);
    layout->addWidget(hint);

    auto* searchRow = new QHBoxLayout();
    auto* searchButton = new QPushButton(tr("搜索并添加…"), this);
    searchButton->setToolTip(tr("从 Modrinth 或 CurseForge 搜索，勾选后把条目追加到下面的列表。"));
    connect(searchButton, &QPushButton::clicked, this, [this] {
        ModSearchDialog dialog(m_gameVersion, m_instanceLoader, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        QStringList lines;
        for (const auto& entry : dialog.selected()) {
            QString line = entry.provider == QLatin1String(modprovider::CURSEFORGE)
                               ? QStringLiteral("cf:") + entry.project
                               : entry.project;
            QString note = entry.note;
            note.replace(QLatin1Char('\n'), QLatin1Char(' '));  // 简介可能带换行，压成一行
            if (!entry.name.isEmpty() || !note.isEmpty()) {
                line += QStringLiteral(" | ") + entry.name;
                if (!note.isEmpty())
                    line += QStringLiteral(" | ") + note;
            }
            lines.append(line);
        }
        if (lines.isEmpty())
            return;
        if (!m_mods->toPlainText().trimmed().isEmpty())
            m_mods->appendPlainText(QString());
        m_mods->appendPlainText(lines.join(QLatin1Char('\n')));
    });
    searchRow->addWidget(searchButton);
    searchRow->addStretch(1);
    layout->addLayout(searchRow);

    m_mods = new QPlainTextEdit(this);
    m_mods->setPlaceholderText(QStringLiteral("sodium | Sodium | 渲染优化\nlithium\n-ferrite-core"));
    layout->addWidget(m_mods, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* save = buttons->addButton(tr("保存"), QDialogButtonBox::AcceptRole);
    auto* close = buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
    connect(save, &QPushButton::clicked, this, &ModPresetEditorDialog::saveCurrent);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { loadIntoEditors(m_presetCombo->currentData().toString()); });

    reloadPresets();
}

void ModPresetEditorDialog::reloadPresets(const QString& selectId)
{
    m_presets = ModPresets::load();

    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    for (const auto& preset : m_presets)
        m_presetCombo->addItem(preset.name, preset.id);
    m_presetCombo->blockSignals(false);

    const int index = selectId.isEmpty() ? 0 : m_presetCombo->findData(selectId);
    if (m_presetCombo->count() > 0)
        m_presetCombo->setCurrentIndex(index >= 0 ? index : 0);
    loadIntoEditors(m_presetCombo->currentData().toString());
}

void ModPresetEditorDialog::loadIntoEditors(const QString& id)
{
    m_currentId = id;
    m_name->clear();
    m_description->clear();
    m_mods->clear();

    for (const auto& preset : m_presets) {
        if (preset.id != id)
            continue;

        m_name->setText(preset.name);
        m_description->setText(preset.description);
        const int loaderIndex = m_loader->findData(preset.loader);
        m_loader->setCurrentIndex(loaderIndex >= 0 ? loaderIndex : m_loader->count() - 1);

        QStringList lines;
        for (const auto& mod : preset.mods) {
            QString line = mod.provider == QLatin1String(modprovider::CURSEFORGE)
                               ? QStringLiteral("cf:") + mod.project
                               : mod.project;
            if (!mod.name.isEmpty() || !mod.note.isEmpty()) {
                line += QStringLiteral(" | ") + mod.name;
                if (!mod.note.isEmpty())
                    line += QStringLiteral(" | ") + mod.note;
            }
            if (!mod.enabled)
                line.prepend(QLatin1Char('-'));
            lines.append(line);
        }
        m_mods->setPlainText(lines.join(QLatin1Char('\n')));
        return;
    }
}

void ModPresetEditorDialog::saveCurrent()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("缺名称"), tr("给这个预设起个名字。"));
        return;
    }

    ModPreset preset;
    preset.id = m_currentId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_currentId;
    preset.name = name;
    preset.description = m_description->text().trimmed();
    preset.loader = m_loader->currentData().toString();

    for (const auto& raw : m_mods->toPlainText().split(QLatin1Char('\n'))) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        ModPresetEntry entry;
        entry.enabled = true;
        if (line.startsWith(QLatin1Char('-'))) {
            entry.enabled = false;
            line = line.mid(1).trimmed();
        }

        const auto parts = line.split(QLatin1Char('|'));
        entry.project = parts.value(0).trimmed();
        entry.name = parts.value(1).trimmed();
        // 说明里本身可能有竖线，第三段之后一律并回 note
        if (parts.size() > 2)
            entry.note = parts.mid(2).join(QLatin1Char('|')).trimmed();

        if (entry.project.startsWith(QLatin1String("cf:"), Qt::CaseInsensitive)) {
            entry.provider = QString::fromLatin1(modprovider::CURSEFORGE);
            entry.project = entry.project.mid(3).trimmed();
        } else if (!entry.project.isEmpty() && entry.project.toInt() > 0) {
            // 纯数字只可能是 CurseForge 的 modId
            entry.provider = QString::fromLatin1(modprovider::CURSEFORGE);
        }

        if (!entry.project.isEmpty())
            preset.mods.append(entry);
    }

    if (preset.mods.isEmpty()) {
        QMessageBox::warning(this, tr("还没有模组"), tr("至少写一个 slug。"));
        return;
    }

    bool replaced = false;
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id == preset.id) {
            m_presets[i] = preset;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        m_presets.append(preset);

    QString error;
    if (!ModPresets::save(m_presets, &error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return;
    }

    reloadPresets(preset.id);
    QMessageBox::information(this, tr("已保存"), tr("预设「%1」已保存。").arg(preset.name));
}

void ModPresetEditorDialog::removeCurrent()
{
    if (m_currentId.isEmpty())
        return;

    const auto answer = QMessageBox::question(this, tr("删除预设"), tr("确定删除「%1」吗？").arg(m_name->text()),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    for (int i = m_presets.size() - 1; i >= 0; --i) {
        if (m_presets.at(i).id == m_currentId)
            m_presets.removeAt(i);
    }

    QString error;
    if (!ModPresets::save(m_presets, &error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return;
    }
    m_currentId.clear();
    reloadPresets();
}

void ModPresetEditorDialog::addPreset()
{
    m_currentId.clear();
    m_name->setText(tr("新预设"));
    m_description->clear();
    m_mods->clear();
    m_name->setFocus();
    m_name->selectAll();
}

}  // namespace maps
