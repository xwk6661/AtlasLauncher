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

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMargins>
#include <QPushButton>
#include <QScrollBar>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "MapTheme.h"

/*
 * 地图页的排版与控件「积木」。
 *
 * 为什么单独一层：这些页面是纯代码搭的（没有 .ui），间距 / 字号 / 按钮角色
 * 原本散在几十处字面量里（4/6/8/10/12 混用、标题各加各的倍率），改一处就走形。
 * 这里只放**尺度与层级**，颜色仍然由 MapTheme.h 按 palette 决定 ——
 * 亮暗两套主题共用同一份排版，不再各写一套。
 */
namespace maps::ui {

// ── 间距刻度：4 的倍数，和 Fusion / 上游页面的密度对齐 ───────────────────────
inline constexpr int kSpaceXS = 4;
inline constexpr int kSpaceS = 6;
inline constexpr int kSpaceM = 10;
inline constexpr int kSpaceL = 16;
inline constexpr int kSpaceXL = 24;

/// 页面内容边距（页签内部 / 独立页）
inline constexpr int kPageMargin = 12;
/// 卡片内边距
inline constexpr int kCardPadH = 14;
inline constexpr int kCardPadV = 12;

inline QMargins pageMargins()
{
    return QMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
}

inline QMargins cardMargins()
{
    return QMargins(kCardPadH, kCardPadV, kCardPadH, kCardPadV + 2);
}

// ── 字体层级 ────────────────────────────────────────────────────────────────
/// 页面标题：比正文大一档、加粗。不靠巨大的字号撑场面，靠与副标题的对比。
inline QFont pageTitleFont(const QFont& base)
{
    QFont font = base;
    font.setBold(true);
    font.setPointSizeF(base.pointSizeF() * 1.25);
    return font;
}

/// 卡片标题 / 区块标题：正文大小 + 加粗
inline QFont cardTitleFont(const QFont& base)
{
    QFont font = base;
    font.setBold(true);
    return font;
}

/// 次要信息（时间、体积、计数、说明）：小半档、次要色
inline QFont metaFont(const QFont& base)
{
    QFont font = base;
    font.setPointSizeF(qMax(7.5, base.pointSizeF() - 0.5));
    return font;
}

// ── 常用控件 ────────────────────────────────────────────────────────────────
/// 页面主标题
inline QLabel* makePageTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(pageTitleFont(label->font()));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

/// 次要说明文字（放在标题下方或表单旁边）
inline QLabel* makeMuted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(metaFont(label->font()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(mutedTextColor().name()));
    label->setWordWrap(true);
    return label;
}

/// 1px 分隔线（主题无关：两端各取一半透明度的前景色）
inline QFrame* makeSeparator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: rgba(127, 127, 127, 45); border: none;"));
    return line;
}

/// 主操作按钮：用主题强调色，全应用只有「真正要做的那件事」用它
inline void applyPrimaryButton(QPushButton* button)
{
    if (button == nullptr)
        return;
    button->setMinimumHeight(30);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid rgba(0, 0, 0, 45); border-radius: 4px; padding: 4px 16px; font-weight: 600; }"
        "QPushButton:hover { border-color: rgba(255, 255, 255, 60); }"
        "QPushButton:pressed { background-color: palette(dark); }"
        "QPushButton:disabled { background-color: rgba(127, 127, 127, 45);"
        " color: rgba(127, 127, 127, 170); border-color: rgba(127, 127, 127, 45); }"));
}

inline QPushButton* makePrimaryButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    applyPrimaryButton(button);
    return button;
}

/// 次要按钮（「取消 / 稍后 / 打开目录」这类）：保持原生外观，只统一高度
inline void applySecondaryButton(QPushButton* button)
{
    if (button == nullptr)
        return;
    button->setMinimumHeight(28);
}

// ── 列表 / 表格 ────────────────────────────────────────────────────────────
/// 统一行高与表头高度：默认的 20/24px 在 1080p 上偏挤，中文尤其明显
inline void tuneTable(QTableWidget* table, int rowHeight = 30)
{
    if (table == nullptr)
        return;
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->horizontalHeader()->setMinimumHeight(32);
    table->horizontalHeader()->setHighlightSections(false);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->setFrameShape(QFrame::NoFrame);
}

inline void tuneList(QAbstractItemView* view)
{
    if (view == nullptr)
        return;
    view->setFrameShape(QFrame::NoFrame);
}

// ── 滚动区 ────────────────────────────────────────────────────────────────
/**
 * 细滚动条：透明轨道、无上下箭头，只剩一根半透明圆角滑杆（悬停变亮）。
 *
 * 为什么：Windows 原生滚动条会在内容区右缘切出一道带箭头的槽 + 浅色轨道，
 * 深色主题下比内容本身还显眼（大厅「简介」页签里就有一条，2026-11-17 反馈）。
 * 这里**不关闭滚动** —— 只是把"始终存在的槽"换成"需要时才出现的滑杆"，
 * 内容依然能滚、也依然看得出还有内容。
 *
 * 颜色取自 palette（`bodyTextColor`），亮暗两套主题共用。
 */
inline void applyOverlayScrollBar(QAbstractScrollArea* area)
{
    if (area == nullptr)
        return;
    const QColor base = bodyTextColor();
    const auto rgba = [base](int alpha) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(base.red()).arg(base.green()).arg(base.blue()).arg(alpha);
    };

    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->verticalScrollBar()->setStyleSheet(QStringLiteral(R"(
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: %1;
    min-height: 28px;
    border-radius: 3px;
}
QScrollBar::handle:vertical:hover {
    background: %2;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
    border: none;
    background: none;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
}
)")
                                     .arg(rgba(38), rgba(96)));
}

/// 空态提示：居中、次要色，不要用一整张失败面板吓人
inline QLabel* makeEmptyState(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setFont(metaFont(label->font()));
    label->setStyleSheet(QStringLiteral("color: %1; padding: 24px 12px;").arg(mutedTextColor().name()));
    return label;
}

}  // namespace maps::ui
