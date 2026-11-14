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

#include <QJsonObject>
#include <QString>

namespace maps::variant {

/**
 * 一张图的「语言变体」。
 *
 * 为什么不是「一个包 = 一张图」：同一张图常有两三个包（英文原版、简中汉化、繁中），
 * 它们是**同一张图的三个版本**，不是三张图。玩家要的是"这张图玩哪个语言"，
 * 而不是"我有 26 个压缩包"。
 *
 * 三种情况都要能表达：
 *   1. 原版：作者发的英文/原文包 —— `kind = Original`；
 *   2. 官中：作者自己发了中文版 —— `kind = Localized` + `source = Author`；
 *   3. 汉化：社区/自己翻的 —— `kind = Localized` + `source = Community|Self`。
 *
 * 载体必须分开记，因为**导入行为完全不同**：
 *   · `Archive`：自带存档（翻译过的区块/资源），导入时建独立实例；
 *   · `ResourcePack`：只是一份资源包，本身没有世界 —— 只能挂到同组某个已装实例上，
 *     往 `resourcepacks/` 放文件并写 `options.txt`，绝不能建实例（建出来是空世界）。
 */
enum class Kind {
    Original,   ///< 这是原版（作者母语版本）
    Localized,  ///< 这是某种语言的本地化版本（官中或汉化）
};

enum class Language {
    Unknown,
    English,
    SimplifiedChinese,
    TraditionalChinese,
    Other,
};

enum class Delivery {
    Archive,       ///< 整包：自带存档，导入即建实例
    ResourcePack,  ///< 纯资源包：必须挂在同组的某个整包上
};

enum class Source {
    Unknown,
    Author,     ///< 官中：作者随图发布
    Community,  ///< 社区汉化（下载来的）
    Self,       ///< 自己翻的（本项目的汉化链路产物）
};

QString kindId(Kind kind);
Kind kindFromId(const QString& id);
QString languageId(Language language);
Language languageFromId(const QString& id);
QString deliveryId(Delivery delivery);
Delivery deliveryFromId(const QString& id);
QString sourceId(Source source);
Source sourceFromId(const QString& id);

/// 「原版」「简中」「繁中」「官中」「未知语言」—— 界面徽章用
QString languageLabel(Language language);
/// 「整包」「资源包」—— 界面徽章用
QString deliveryLabel(Delivery delivery);
/// 「作者发布」「社区汉化」「自译」「来源未知」
QString sourceLabel(Source source);

struct Info {
    Kind kind = Kind::Original;
    Language language = Language::Unknown;
    Delivery delivery = Delivery::Archive;
    Source source = Source::Unknown;
    /// 文件名括号里的原话，例如「简中汉化v1.0」「繁中」；不改写、不翻译
    QString label;
    /// 仅 ResourcePack：挂在哪个包上（同组内某个 Archive 的文件名）
    QString basedOn;
    /// 同一张图的默认变体（双击/启动用哪个）
    bool isDefault = false;

    QJsonObject toJson() const;
    static Info fromJson(const QJsonObject& object);

    /// 一行摘要：「原版」「简中汉化」；判不出来就说「未标注」
    QString summary() const;
    bool isEmpty() const;
};

/**
 * 从文件名与备注推断变体。
 *
 * **只认字面证据，不猜**：括号或备注里出现「繁中/繁體」→ 繁中，「简中/简體/汉化」→ 简中，
 * 「官中/官方中文」→ 官中（source = Author）。什么都没写 → `Language::Unknown`，
 * 留给导入对话框或玩家确认 —— 盘灵古域这种中文原创图就是"没有标注、但本来就是中文"。
 */
Info inferFromFileName(const QString& fileName, const QString& note = {});

/**
 * 归一化图名，给「没有大厅关联时」的本地分组兜底用。
 *
 * 规则：去掉扩展名、开头的 `[1.16.5]`、所有括号内容、`v1.2` / `MC1.21.9` 这类版本 token，
 * 统一小写与分隔符，丢掉 by/the/a 这类噪声词。**保留** `09` 这种章节号（它不是版本号）。
 */
QString normalizeName(const QString& text);

}  // namespace maps::variant
