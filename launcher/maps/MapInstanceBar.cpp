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
#include "MapInstanceBar.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "AgentBootstrap.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "MapMeta.h"
#include "MapNotes.h"
#include "MapRepository.h"
#include "MapTheme.h"
#include "StarRatingWidget.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

namespace maps {
namespace {

/// 实例 saves/ 下有几个可用存档（有 level.dat 才算）
QStringList scanWorldFolders(const QString& instanceRoot)
{
    QStringList out;
    QDir saves(FS::PathCombine(instanceRoot, ".minecraft", "saves"));
    if (!saves.exists())
        return out;

    const auto entries = saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
        if (entry.fileName().startsWith(QLatin1Char('.')))
            continue;  // .mcml-backup 之类的内部目录
        if (QFile::exists(FS::PathCombine(entry.absoluteFilePath(), "level.dat")))
            out.append(entry.fileName());
    }
    return out;
}

QString formatPlayTime(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("0 分钟");
    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return QStringLiteral("%1 分钟").arg(minutes);
    return QStringLiteral("%1 小时 %2 分").arg(minutes / 60).arg(minutes % 60);
}

QString formatWhen(const QDateTime& when)
{
    if (!when.isValid())
        return QStringLiteral("—");
    return when.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"));
}

QString exitText(const QString& exit)
{
    if (exit == QLatin1String("safe"))
        return QStringLiteral("正常落盘");
    if (exit == QLatin1String("crash"))
        return QStringLiteral("异常退出");
    return QStringLiteral("未判定");
}

}  // namespace

MapInstanceBar::MapInstanceBar(QWidget* parent) : QWidget(parent)
{
    buildUi();
    setVisible(false);
}

void MapInstanceBar::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 4);
    layout->setSpacing(6);

    // ── 中文名（AIO 的 .detail-alias：次要色、居中、不抢主标题） ──
    m_alias = new QLabel(this);
    m_alias->setWordWrap(true);
    m_alias->setAlignment(Qt::AlignHCenter);
    m_alias->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_alias->setEnabled(false);
    m_alias->setVisible(false);
    layout->addWidget(m_alias);

    // ── 徽章行（AIO 的 .badges：版本 / 加载器 / 置信度） ──
    m_chipBox = new QWidget(this);
    m_chipRow = new QHBoxLayout(m_chipBox);
    m_chipRow->setContentsMargins(0, 0, 0, 0);
    m_chipRow->setSpacing(3);
    m_chipRow->addStretch(1);
    layout->addWidget(m_chipBox);

    // ── 主操作（AIO 的 .side-actions 第一行） ──
    m_launch = new QPushButton(tr("▶  进入地图"), this);
    m_launch->setMinimumHeight(30);
    QFont launchFont = m_launch->font();
    launchFont.setBold(true);
    m_launch->setFont(launchFont);
    connect(m_launch, &QPushButton::clicked, this, [this] {
        if (m_instanceId.isEmpty())
            return;
        const QString world = m_worldCombo->isVisible() ? m_worldCombo->currentData().toString() : m_selectedWorld;
        emit launchRequested(m_instanceId, world);
    });
    layout->addWidget(m_launch);

    m_worldCombo = new QComboBox(this);
    m_worldCombo->setVisible(false);
    layout->addWidget(m_worldCombo);

    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setVisible(false);
    layout->addWidget(m_notice);

    // ── 我的评分（只读；编辑在实例窗口的「信息」页） ──
    m_ratingHead = addSection(tr("我的评分"));
    auto* ratingRow = new QHBoxLayout();
    ratingRow->setContentsMargins(0, 0, 0, 0);
    ratingRow->setSpacing(6);
    m_stars = new StarRatingWidget(this);
    m_stars->setReadOnly(true);
    m_ratingValue = new QLabel(this);
    m_ratingValue->setEnabled(false);
    ratingRow->addWidget(m_stars);
    ratingRow->addWidget(m_ratingValue, 1);
    layout->addLayout(ratingRow);

    // ── 笔记（预览，悬停看全文） ──
    m_noteHead = addSection(tr("笔记"));
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // AIO 的 .note-view 是 max-height:168px + 滚动；侧栏里没有滚动条，
    // 所以折成"最多 6 行 + 悬停看全文"，别让一条长笔记把下面的 KV 顶出可视区
    m_note->setMaximumHeight(96);
    layout->addWidget(m_note);

    // ── 详情（AIO 的 .kv-list） ──
    m_detailHead = addSection(tr("详情"));
    m_rowsBox = new QWidget(this);
    m_rows = new QGridLayout(m_rowsBox);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setHorizontalSpacing(8);
    m_rows->setVerticalSpacing(1);
    m_rows->setColumnMinimumWidth(0, 52);
    m_rows->setColumnStretch(1, 1);
    layout->addWidget(m_rowsBox);
}

QLabel* MapInstanceBar::addSection(const QString& title)
{
    auto* label = new QLabel(title, this);
    label->setStyleSheet(ui::sectionHeaderStyle());
    static_cast<QVBoxLayout*>(layout())->addWidget(label);
    return label;
}

void MapInstanceBar::addChip(const QString& text, const QColor& color)
{
    auto* chip = new QLabel(text, m_chipBox);
    chip->setStyleSheet(ui::chipStyle(color));
    chip->setToolTip(text);
    // 插在末尾的 stretch 之前，整行居中
    m_chipRow->insertWidget(m_chipRow->count() - 1, chip);
}

void MapInstanceBar::clearChips()
{
    while (m_chipRow->count() > 1) {
        QLayoutItem* item = m_chipRow->takeAt(0);
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void MapInstanceBar::setRow(const QString& key, const QString& value, const QString& tooltip)
{
    const int row = m_rows->rowCount();
    auto* keyLabel = new QLabel(key, m_rowsBox);
    keyLabel->setEnabled(false);
    auto* valueLabel = new QLabel(value, m_rowsBox);
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!tooltip.isEmpty())
        valueLabel->setToolTip(tooltip);

    m_rows->addWidget(keyLabel, row, 0, Qt::AlignTop);
    m_rows->addWidget(valueLabel, row, 1, Qt::AlignTop);
}

void MapInstanceBar::clearRows()
{
    while (QLayoutItem* item = m_rows->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void MapInstanceBar::refreshLaunchButton(bool running)
{
    if (running) {
        m_launch->setText(tr("游戏运行中…"));
        m_launch->setEnabled(false);
    } else {
        m_launch->setText(tr("▶  进入地图"));
        m_launch->setEnabled(true);
    }
}

bool MapInstanceBar::updateInstance(MinecraftInstance* instance)
{
    if (m_runningHook)
        QObject::disconnect(m_runningHook);

    m_instanceId.clear();
    m_selectedWorld.clear();

    if (instance == nullptr)
        return false;

    const QString root = instance->instanceRoot();
    const bool isMapInstance = QFile::exists(FS::PathCombine(root, MapMeta::fileName()));
    const QStringList worlds = scanWorldFolders(root);

    // 既不是导入出来的地图、又一个存档都没有 —— 不打扰原生操作栏
    if (!isMapInstance && worlds.isEmpty())
        return false;

    m_instanceId = instance->id();
    m_isMapInstance = isMapInstance;

    clearRows();
    clearChips();
    m_worldCombo->clear();
    m_worldCombo->setVisible(false);
    m_notice->setVisible(false);
    m_alias->setVisible(false);

    if (isMapInstance)
        showMapCard(instance);
    else
        showWorldCard(instance, worlds);

    // 运行状态实时反映到主按钮上，避免"点了没反应"的错觉
    m_runningHook = connect(instance, &BaseInstance::runningStatusChanged, this,
                            [this](bool running) { refreshLaunchButton(running); });
    refreshLaunchButton(instance->isRunning());
    if (!instance->isRunning() && !instance->canLaunch()) {
        // 缺核心文件 / Java 没配好：直接说清楚，别让玩家点了没反应
        m_launch->setEnabled(false);
        m_launch->setText(tr("需要先补全游戏文件"));
    }

    return true;
}

void MapInstanceBar::showMapCard(MinecraftInstance* instance)
{
    const MapEntry entry = MapRepository::describe(instance);
    const auto& meta = entry.meta;
    const auto& notes = entry.notes;

    // 中文名：有才显示（没有时上面原生名字就是标题）
    if (!notes.alias.isEmpty()) {
        m_alias->setText(notes.alias);
        m_alias->setVisible(true);
    }

    // 徽章：版本 / 加载器 / 置信度
    addChip(meta.version.isKnown() ? meta.version.mcVersion : tr("版本未知"), ui::accentColor());
    // 加载器取实例实际装的组件：Vanilla 也要显示 —— 玩家靠这一格判断"要不要先装 Forge"
    addChip(entry.loaderVersion.isEmpty()
                ? entry.loaderName
                : QStringLiteral("%1 %2").arg(entry.loaderName, entry.loaderVersion),
            ui::mutedTextColor());
    addChip(confidenceToString(entry.confidence), ui::confidenceColor(entry.confidence));

    // 世界下拉：一包多世界才出现
    for (const auto& world : meta.worlds)
        m_worldCombo->addItem(world.folder, world.folder);
    m_worldCombo->setVisible(meta.worlds.size() > 1);
    if (!meta.worlds.isEmpty())
        m_selectedWorld = meta.worlds.first().folder;

    // 我的评分 / 笔记：与 AIO 详情栏同款（只读）
    m_stars->setValue(notes.rating);
    m_ratingValue->setText(notes.rating > 0 ? notes.ratingText() : tr("未评分"));
    if (notes.note.trimmed().isEmpty()) {
        m_note->setText(tr("还没有笔记 —— 在实例窗口的「信息」页里写"));
        m_note->setEnabled(false);
        m_note->setToolTip({});
    } else {
        QString preview = notes.note.trimmed();
        if (preview.size() > 240)
            preview = preview.left(240) + QStringLiteral("…");
        m_note->setText(preview);
        m_note->setEnabled(true);
        m_note->setToolTip(notes.note);
    }

    // 详情：KV 行
    const auto& stats = meta.stats;
    setRow(tr("状态"), exitText(stats.lastExit));
    setRow(tr("游玩"), formatPlayTime(stats.playSeconds), tr("累计游玩时长（含本次）"));
    setRow(tr("上次"), formatWhen(stats.lastPlayed));
    setRow(tr("存档"), tr("%1 个").arg(meta.worlds.size()));
    if (meta.archive.size > 0) {
        setRow(tr("包体积"), QStringLiteral("%1 MB").arg(meta.archive.size / 1024.0 / 1024.0, 0, 'f', 1),
               meta.archive.path);
    }
    if (meta.importedAt.isValid())
        setRow(tr("导入于"), formatWhen(meta.importedAt));

    // 风险提示：能提前说清楚的都不要等到进游戏才发现
    QStringList notices;
    if (!meta.experimentalFeatures.isEmpty()) {
        notices.append(tr("⚠ 存档带实验性设置（%1），游戏会先弹一次确认框")
                           .arg(meta.experimentalFeatures.join(QStringLiteral(", "))));
    }
    if (meta.loader.type != QLatin1String("vanilla") && meta.loader.version.isEmpty()) {
        notices.append(tr("⚠ 判定为 %1 存档，但没装加载器 —— 模组方块会被原版删掉").arg(meta.loader.type));
    }
    if (!meta.requiredMods.isEmpty())
        notices.append(tr("⚠ 存档需要 %1 个模组").arg(meta.requiredMods.size()));
    if (meta.requiresExternalModpack)
        notices.append(tr("⚠ 压缩包里没带 mods/，需要自备整合包"));
    if (!meta.version.isKnown()) {
        notices.append(tr("⚠ 版本未判定，进游戏前请先确认版本"));
    } else if (!instance->traits().contains(QStringLiteral("feature:is_quick_play_singleplayer")) &&
               !AgentBootstrap::hasSymbols(meta.version.mcVersion)) {
        notices.append(tr("⚠ %1 没有离线符号表，只能停在主菜单手动选存档").arg(meta.version.mcVersion));
    }
    if (!notices.isEmpty()) {
        m_notice->setText(notices.join(QStringLiteral("\n")));
        m_notice->setStyleSheet(QStringLiteral("color: %1;").arg(ui::noticeColorName()));
        m_notice->setVisible(true);
    }
}

void MapInstanceBar::showWorldCard(MinecraftInstance* instance, const QStringList& worlds)
{
    // 不是导入出来的实例：版本和加载器同样从组件读（derive 一次就够）
    const MapEntry entry = MapRepository::describe(instance);
    QString version = instance->getPackProfile() != nullptr
                          ? instance->getPackProfile()->getComponentVersion(QStringLiteral("net.minecraft"))
                          : QString();
    if (version.isEmpty())
        version = tr("版本未知");

    addChip(version, ui::accentColor());
    addChip(entry.loaderVersion.isEmpty()
                ? entry.loaderName
                : QStringLiteral("%1 %2").arg(entry.loaderName, entry.loaderVersion),
            ui::mutedTextColor());

    for (const auto& world : worlds)
        m_worldCombo->addItem(world, world);
    m_worldCombo->setVisible(worlds.size() > 1);
    m_selectedWorld = worlds.first();

    m_stars->setValue(0);
    m_ratingValue->setText(tr("未评分"));
    m_note->setText(tr("这个实例不是通过「导入地图」创建的：没有版本置信度与游玩统计。"));
    m_note->setEnabled(false);
    m_note->setToolTip({});

    setRow(tr("存档"), tr("%1 个").arg(worlds.size()));

    QStringList notices;
    if (!instance->traits().contains(QStringLiteral("feature:is_quick_play_singleplayer")) &&
        !AgentBootstrap::hasSymbols(version)) {
        notices.append(tr("⚠ %1 没有离线符号表，启动后会停在主菜单，需要手动选存档").arg(version));
    }
    if (!notices.isEmpty()) {
        m_notice->setText(notices.join(QStringLiteral("\n")));
        m_notice->setStyleSheet(QStringLiteral("color: %1;").arg(ui::noticeColorName()));
        m_notice->setVisible(true);
    }
}

}  // namespace maps
