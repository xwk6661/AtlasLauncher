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

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

/**
 * 地图汉化的**只读体检**。不写任何文件，可以随时中断。
 *
 * 为什么只读也要单独一个模块：这是整个汉化功能的地基。体检数字不准，
 * 后面的"抽文本 / 翻译 / 写回"全是错的。所以它先做、并且要能对着参考实现
 * （`mcsl-handover` 的 `probe`/`analyze`）逐项对表。
 *
 * 扫描分两段，因为代价差三个数量级：
 *   · `scanFast()`   资源包语言表 + 数据包文本 —— 毫秒到秒级，页面一打开就跑；
 *   · `scanRegions()` 区块/实体/数据文件 —— 几十秒级，必须带进度与取消。
 *
 * 规则（与参考实现对齐，差异单独标注）：
 *   1. 只认**文本载体**里的字符串，`Sections`/`Heightmaps` 这类大数组整棵剪掉；
 *   2. 一条字符串先尝试解析成文本组件 JSON，取出 `text`；解析不了就当原文；
 *   3. 只有"散文"进待译表：去掉 § 格式化码与私用区字形后仍有意义、
 *      不是标识符、不是 base64、不是单字符。
 */
namespace maps::l10n {

/// 处理档位：键化（只改资源包）/ 混合 / 注入（要改存档）
enum class Mode { Key, Mixed, Injection };

/// 「完整（资源包）」/「部分」/「需改写存档」
QString modeLabel(Mode mode);
/// 一句话解释这个档位意味着什么（直接进 UI）
QString modeDescription(Mode mode);

/// 一份文本收集结果：唯一文本 → 出现次数，外加 translate 键的引用
struct TextCollector {
    /// 唯一源文本 → 出现次数（去重后才是真正的翻译量）
    QHash<QString, int> texts;
    /// 引用到的 translate 键
    QSet<QString> keys;
    /// translate 引用总次数（同一个键出现多次算多次）
    int refs = 0;
    /// 字面量出现总次数
    int literalOccurrences = 0;
    /// 解析失败的条目（损坏的 JSON / NBT），只用于诊断
    int errors = 0;

    /// 出现次数
    int occurrences() const { return refs + literalOccurrences; }
    /// 唯一文本条数（＝真正要翻的句子数）
    int uniqueTexts() const { return int(texts.size()); }
};

/// 资源包（世界自带的 `resources.zip` 或 `resources/`）的语言表情况
struct PackFacts {
    bool found = false;
    QString path;
    /// 键最多的命名空间（地图一般用自己的 ns，也有直接覆盖 minecraft 的）
    QString namespaceName;
    /// 语言代码 → 键数
    QHash<QString, int> locales;
    int enKeys = 0;
    int zhKeys = 0;
    /// en 有、zh 没有（或与 en 相同的）**散文**条数
    int missingProse = 0;
    /**
     * 语言表里的键是**字形表**，不是句子。
     *
     * 键化图（Final Paradox、Lacuna Sanctum 这类）把字体拆成一张贴图 + 一堆
     * `offset.*` / `<ns>.*` 键，值里全是私用区字形（`\uf800` 那种），
     * 一个可翻的散文都没有。这类表的正确做法是重画字形贴图，
     * **字符串替换一概碰不到** —— 必须在界面上说出来，别让人以为漏导了。
     */
    bool glyphTable = false;
    /// 资源包里自定义字体文件数（`assets/<ns>/font/*.json`）
    int fontFiles = 0;

    /// 语言表里真正需要翻的条数（= missingProse）
    int proseKeys() const { return missingProse; }
};

struct ProbeResult {
    QString root;
    QString mcVersion;
    QString error;

    PackFacts pack;
    TextCollector datapacks;
    TextCollector regions;

    /// 数据包文本文件数
    int datapackFiles = 0;
    /// 区块文件数（region/entities/DIM 全部）
    int regionFiles = 0;
    /// 实际读到的区块数
    int chunks = 0;
    /// 区块是否扫过 —— false 时 `regions` 里的数字没有意义
    bool regionsScanned = false;

    /// 给 UI 的补充说明（风险、不支持的情况），不要在这里放客套话
    QStringList notes;

    /// translate 引用 / (引用 + 字面量)；两端实测 0.99 与 0.00，中间地带很宽
    double keyRatio() const;
    Mode mode() const;
    bool isValid() const { return error.isEmpty(); }
};

/// 快检：只看资源包语言表与数据包文本，不碰区块
ProbeResult scanFast(const QString& worldRoot, const QString& mcVersion);

/**
 * 慢检：扫区块 / 实体 / 数据文件，结果写进 `result.regions`。
 *
 * @param progress 每扫完一个文件回调一次（已扫, 总数）；返回 false 表示取消
 * @return 是否跑完（false = 被取消）
 */
bool scanRegions(ProbeResult& result, const std::function<bool(int done, int total)>& progress);

/// 一条字符串值得翻译吗（散文判定）
bool looksLikeProse(const QString& text);

/**
 * 文本载体的判定规则。
 *
 * 抽取（探针）与写回（`MapL10nApply`）**必须**用同一份规则：否则会出现
 * "抽出来的句子写不回去"或者"写回去把不该动的东西改了"。所以把规则放在这里公开，
 * 两边都调这些函数，而不是各自维护一份 key 列表。
 */
bool isTextKey(const QString& key);
bool isTextListKey(const QString& key);
bool isCommandKey(const QString& key);
bool isSkippedKey(const QString& key);
bool isSkippedSubtree(const QString& key);

/// 文本里已经有中日韩文字了吗（说明它已经是中文了）
bool containsCjk(const QString& text);

/**
 * 这条文本**还需不需要翻**：是散文，而且现在还不是中文。
 *
 * 中文原创地图（Terra Restore 中文版、盘灵古域）通篇都是中文，
 * 把它们的原文当"待译"导出去，清单会瞬间变成几千条噪声，
 * 玩家还会以为自己装错了版本。
 */
bool needsTranslation(const QString& text);

/// 把一条 NBT 字符串拆成"可译单元"：文本组件取 `text`，整条 JSON 字符串还原，否则原样
QStringList translateUnits(const QString& raw);

/// 资源包里的语言表（只取 `en_us` 与 `zh_cn`，用来算"哪些还没翻"）
struct LangTables {
    QHash<QString, QString> en;
    QHash<QString, QString> zh;
};

/**
 * 重新读一遍选中的命名空间的 `en_us` / `zh_cn`。
 *
 * 体检结果里只留了计数（一张 12879 键的表没必要一直挂在内存里），
 * 真正要导出待译清单时再读一次 —— 读一个 zip 只要几十毫秒。
 */
LangTables readLangTables(const PackFacts& pack);

}  // namespace maps::l10n
