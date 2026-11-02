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

#include <optional>

#include "MapTypes.h"

namespace maps {

/**
 * level.dat（GZip(NBT)）-> LevelDatFacts。
 *
 * 与 World::loadFromLevelDat 的关系：那个函数负责"世界列表要展示什么"，本类负责
 * "版本判定需要什么"。刻意不复用，避免一改判定规则就动到世界列表的行为。
 */
class LevelDatReader {
   public:
    /// bytes 可以是 GZip 压缩的，也可以是裸 NBT（少数工具会这么导出）
    static std::optional<LevelDatFacts> fromBytes(const QByteArray& bytes, QString* error = nullptr);
    static std::optional<LevelDatFacts> fromFile(const QString& path, QString* error = nullptr);
};

}  // namespace maps
