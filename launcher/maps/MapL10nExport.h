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

#include "MapL10nProbe.h"

namespace maps::l10n {

/// 待译清单里的一条。两种来源共用同一个结构，靠 `kind` 区分。
struct ExportItem {
    /// "key"（语言表的键）或 "literal"（写在数据/存档里的原文）
    QString kind;
    /// key 模式下的语言表键名；literal 为空
    QString key;
    /// 原文
    QString source;
    /// 出现次数（literal 有意义；key 恒为 1）
    int count = 1;
    /// 现有译文（key 模式可能有，为空表示没翻过）
    QString existing;
    /// 这张图里真的有地方引用它吗（没引用的多半是原版键，优先级低）
    bool referenced = false;
};

struct ExportResult {
    QString directory;
    QString jsonPath;
    QString csvPath;
    int total = 0;
    int fromPack = 0;
    int fromWorld = 0;
    /// 区块没扫过时清单会偏少，UI 要如实说明
    bool regionsScanned = false;
    QString error;

    bool ok() const { return error.isEmpty() && total > 0; }
};

/**
 * 把体检结果导成待译清单。
 *
 * 产出两个文件，同一个目录下：
 *   · `strings.json` —— 结构化，给翻译层 / 写回流程用（字段名将来不会删，只会加）
 *   · `strings.csv`  —— UTF-8 **带 BOM**，Excel 直接打开不乱码，给人改
 *
 * 不覆盖已有译文：`zh` 列留空表示"待译"，已翻好的条目不进清单。
 */
ExportResult exportStrings(const ProbeResult& result, const QString& outputDirectory);

}  // namespace maps::l10n
