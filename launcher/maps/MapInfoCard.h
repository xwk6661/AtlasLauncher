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

#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QLayout;
class QVBoxLayout;

namespace maps::ui {

/**
 * 信息卡片：与评论卡片同一套外观（`cardStyle()`），可选标题。
 *
 * 大厅详情与实例「信息」页共用 —— 两处的"简介 / 我的"都长这个样子，
 * 免得改一处忘一处、样式慢慢走偏。
 *
 * @param bodyOut 非空时回填内容布局，调用方往里加控件
 */
QFrame* makeCard(QWidget* parent, const QString& title = {}, QVBoxLayout** bodyOut = nullptr);

/// 表单左侧的字段名：次要色，和值拉开层次
QLabel* makeKeyLabel(const QString& text, QWidget* parent);

/// 清空布局（含嵌套布局里的 widget）。用于"每次刷新都重建"的卡片内容
void clearLayout(QLayout* layout);

}  // namespace maps::ui
