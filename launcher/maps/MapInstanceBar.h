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

#include <QMetaObject>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class MinecraftInstance;

namespace maps {

class StarRatingWidget;

/**
 * 实例操作栏里的「地图卡片」。
 *
 * 结构照 AIO 的 `#pane-detail`（`ui/app.js renderDetail` + `style.css .detail-inner`）：
 *
 *   中文名（次要色）
 *   徽章行：版本 / 加载器 / 置信度（小 chip）
 *   动作行：「进入地图」（主）、「复制压缩包路径」
 *   世界下拉（一包多世界才有）
 *   风险提示
 *   ── 我的评分（只读五颗星 + 3.5 / 5）
 *   ── 笔记（预览，悬停看全文）
 *   ── 详情（状态 / 游玩 / 上次 / 存档 / 包体积 / 导入于）
 *
 * 编辑入口在实例窗口的「信息」页 —— 与 AIO 一致：这一栏只负责"看一眼再决定进不进"。
 */
class MapInstanceBar : public QWidget {
    Q_OBJECT

   public:
    explicit MapInstanceBar(QWidget* parent = nullptr);

    /**
     * 跟随主窗口选中实例刷新。
     * @return true 表示这块卡片接管了「启动」这个主操作（调用方应隐藏原生启动按钮）
     */
    bool updateInstance(MinecraftInstance* instance);

   signals:
    void launchRequested(const QString& instanceId, const QString& worldFolder);

   private:
    void buildUi();
    /// 地图实例：map.json + notes.json 的全量信息
    void showMapCard(MinecraftInstance* instance);
    /// 非地图实例但至少有一个存档：轻量卡片，别退回纯原版
    void showWorldCard(MinecraftInstance* instance, const QStringList& worlds);

    void addChip(const QString& text, const QColor& color);
    void clearChips();
    QLabel* addSection(const QString& title);
    void setRow(const QString& key, const QString& value, const QString& tooltip = {});
    void clearRows();
    void refreshLaunchButton(bool running);

    QLabel* m_alias = nullptr;
    QWidget* m_chipBox = nullptr;
    QHBoxLayout* m_chipRow = nullptr;
    QPushButton* m_launch = nullptr;
    QPushButton* m_presets = nullptr;
    QComboBox* m_worldCombo = nullptr;
    QLabel* m_notice = nullptr;

    QLabel* m_ratingHead = nullptr;
    StarRatingWidget* m_stars = nullptr;
    QLabel* m_ratingValue = nullptr;
    QLabel* m_noteHead = nullptr;
    QLabel* m_note = nullptr;
    QLabel* m_detailHead = nullptr;
    QWidget* m_rowsBox = nullptr;
    QGridLayout* m_rows = nullptr;

    QString m_instanceId;
    MinecraftInstance* m_instance = nullptr;
    QString m_selectedWorld;
    bool m_isMapInstance = false;
    QMetaObject::Connection m_runningHook;
};

}  // namespace maps
