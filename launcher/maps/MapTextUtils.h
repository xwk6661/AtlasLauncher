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

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace maps::text {

/**
 * 剥离 Minecraft 格式化码（§a / §l / §x§R§R§G§G§B§B）。
 *
 * 真实样例（Divinity's End）的 Data.Version.Name 是 "§b§l1.19.2"，
 * 不剥离的话任何版本比对都会失败。
 */
QString stripFormattingCodes(const QString& input);

/// 展示用：剥离格式化码 + 折叠空白 + trim；结果为空时返回空串
QString cleanDisplayName(const QString& input);

/**
 * 文本解码：严格 UTF-8 优先，失败则退回系统本地编码（中文 Windows 即 GBK）。
 *
 * 国内地图的 README 大量是 GBK，而版本号旁证依赖可读文本。
 */
QString decodeText(const QByteArray& bytes);

/// 从任意文本里抠出版本号候选（去重，保持出现顺序）
QStringList extractVersionMentions(const QString& text);

/// 粗略判断一段字节像不像文本（用于无扩展名的说明文件）
bool looksLikeText(const QByteArray& bytes);

}  // namespace maps::text
