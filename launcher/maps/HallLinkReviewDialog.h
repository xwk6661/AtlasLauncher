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

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QList>

#include "HallCache.h"
#include "MapLibrary.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace maps {

/**
 * 「待确认的大厅关联」批量确认页。
 *
 * 背景：`autoLinkUnassigned()` 只写 `verified:false` —— 匹配器再准也只是建议，
 * 一条条在实例页点「关联」是唯一的人工确认入口，17 条关联里因此长期挂着 12 条未确认。
 * 这一页把未确认的条目一次列出来，带上**当前关联**与**匹配器证据**，让玩家批量确认，
 * 也能顺手把关联错的改成正确的候选、或者解除。
 *
 * 三条纪律：
 *   · 默认只勾「证据支持当前关联」的行（当前 slug 就是匹配器第一名且分数够），
 *     其余留空让人自己看 —— 批量确认最容易犯的错是"点一下全确认"；
 *   · 只改 `verified` 时分组不变（`groupKey()` 只看 slug）；改归属会换组，
 *     界面上先写明，不偷偷改；
 *   · 写库走 `MapLibrary::applyHallLinkEdits()`，一次 `QSaveFile`，要么全写、要么不动。
 */
class HallLinkReviewDialog : public QDialog {
    Q_OBJECT

   public:
    explicit HallLinkReviewDialog(MapLibrary* library, QWidget* parent = nullptr);

    /// 实际写进索引的条数（确认 + 改归属 + 解除）
    int changedCount() const { return m_changed; }

    /**
     * 便捷入口：没有未确认条目时只弹一句提示；否则开窗，返回实际改动条数。
     * 调用方在返回后自行 `refresh()` 列表。
     */
    static int review(MapLibrary* library, QWidget* parent = nullptr);

   private:
    void buildRows();
    void applySelection();
    void updateSummary();
    /// 当前关联在站上的名字；缓存里没有就返回空
    QString hallTitle(const QString& slug, bool* cacheValid = nullptr) const;

    MapLibrary* m_library = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_confirmButton = nullptr;
    int m_changed = 0;

    /// 大厅缓存只读一次：3.6 MB 的 JSON，逐条 `HallCache::load()` 会把窗口卡住
    HallSnapshot m_snapshot;
    QHash<QString, QJsonObject> m_snapshotRows;
    /// 每行对应的库条目（表格行号 → 条目；右键/勾选时按行取）
    QList<LibraryItem> m_rows;
};

}  // namespace maps
