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

#include <QWidget>

class QLabel;

namespace maps {

struct HallReview;

/**
 * 一条评论的卡片：头像 + 名字 + 星级 + 时长/日期 + 正文，外面套一层边框。
 *
 * 大厅详情（`MapHallPage`）和实例「信息 → 评论」（`MapInstancePage`）共用同一份，
 * 免得两处样式慢慢走偏。颜色全部走 palette 的半透明灰，亮/暗主题都不写死色号。
 *
 * @param nameLabel 非空时回填名字 `QLabel*`，供异步补用户名用（users 表大量 username 为 null）
 */
QWidget* makeReviewCard(QWidget* parent, const HallReview& review, QLabel** nameLabel = nullptr);

}  // namespace maps
