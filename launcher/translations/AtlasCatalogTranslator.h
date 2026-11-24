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
#include <QString>
#include <QTranslator>

/**
 * 「源串 → 译文」的补充词表。
 *
 * 为什么需要它：Atlas 新增的界面（`launcher/maps/`）按项目惯例把**中文写在 tr() 源串里**
 * —— 中文用户看到的永远是对的，但切到英文时 Qt 没有可查的译文，界面就中英混杂。
 *
 * 上游走的是 Weblate 的 `mmc_<lang>.qm`：那套的源串是英文，改不动也不该改。
 * 这里退一步：不动任何源串，只给"源串是中文"的这批文案配一份可翻译的 JSON 词表，
 * 按**源串文本**查找（不看 context），中英两边各得其所：
 *
 *   · 中文界面：不装（源串本来就是中文）；
 *   · 英文 / 其他语言：装 `atlas_<lang>.json`，没有该语言时退回 `atlas_en.json`。
 *
 * 词表在 qrc 里（`:/i18n/`），随二进制走 —— 便携包少拷一个文件就少一片译文的坑，
 * 在 `jars/` 那次已经吃过一遍（[06 §99]）。
 *
 * 局限：只处理 `tr()` 的源串；日志（`qDebug` / `qWarning`）本来就不该翻译。
 */
class AtlasCatalogTranslator : public QTranslator {
    Q_OBJECT

   public:
    explicit AtlasCatalogTranslator(QObject* parent = nullptr);

    /**
     * 按语言代码装载词表（`en_US` / `zh_CN` / `ja`…）。
     *
     * 中文直接返回 false（源串就是中文，装了反而会把简体转成别的写法）；
     * 其它语言先找 `atlas_<完整代码>.json`，再找 `atlas_<语言>.json`，
     * 最后退到 `atlas_en.json` —— 宁可英文，也不要让英文界面里冒出一段中文。
     */
    bool loadForLanguage(const QString& langCode);

    QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override;
    bool isEmpty() const override;

   private:
    bool loadCatalog(const QString& path);

    QHash<QString, QString> m_entries;
};
