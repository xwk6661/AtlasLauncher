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

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QPalette>
#include <QString>

#include "MapTypes.h"

/*
 * 置信度 / 提示条的颜色不写死。
 *
 * Prism 的亮暗主题是两套 QPalette（不是两套 QSS），同一个 `#2e7d32` 在深色底上
 * 对比度只有 1.6:1，等于看不见。这里按当前 palette 的明暗各给一套：
 * 深色底用高亮度同色相（Material 200 档），浅色底用深色相（Material 800 档）。
 */
namespace maps::ui {

/// 当前是否深色主题（没有 QApplication 时按浅色处理，测试里也能安全调用）
inline bool isDarkTheme()
{
    if (qobject_cast<QApplication*>(QCoreApplication::instance()) == nullptr)
        return false;
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

/// 置信度对应的前景色（高=绿 / 中=橙 / 低=红）
inline QColor confidenceColor(Confidence confidence)
{
    const bool dark = isDarkTheme();
    switch (confidence) {
        case Confidence::High:
            return dark ? QColor(0x81, 0xc7, 0x84) : QColor(0x2e, 0x7d, 0x32);
        case Confidence::Medium:
            return dark ? QColor(0xff, 0xb7, 0x4d) : QColor(0xef, 0x6c, 0x00);
        case Confidence::Low:
            break;
    }
    return dark ? QColor(0xef, 0x9a, 0x9a) : QColor(0xc6, 0x28, 0x28);
}

/// 评分星的点亮色：浅色底用琥珀，深色底用亮金，避免在深色面板上糊成一团
inline QColor starColor()
{
    return isDarkTheme() ? QColor(0xff, 0xd5, 0x4f) : QColor(0xf5, 0xa6, 0x23);
}

/// 主题的强调色（Prism 的 palette Highlight）
inline QColor accentColor()
{
    return QApplication::palette().color(QPalette::Highlight);
}

/// 次要文字色（等价于 AIO 的 --text-3）
inline QColor mutedTextColor()
{
    return QApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
}

/// 小徽章（AIO 的 .badge：高 17px、2px 圆角、mono 字、半透明底）
inline QString chipStyle(const QColor& foreground)
{
    return QStringLiteral("padding: 0px 5px; border-radius: 2px; font-size: 11px;"
                          "background: rgba(%1, %2, %3, 40); color: %4;")
        .arg(foreground.red())
        .arg(foreground.green())
        .arg(foreground.blue())
        .arg(foreground.name());
}

/// 分组标题（AIO 的 .section-head：上方细线 + 小字 + 次要色）
inline QString sectionHeaderStyle()
{
    return QStringLiteral("border-top: 1px solid rgba(127, 127, 127, 55); padding-top: 6px;"
                          "margin-top: 4px; font-size: 11px;");
}

/// 卡片外观（评论、实例「我的」页共用）：半透明底 + 1px 边框 + 6px 圆角，亮/暗主题通用
inline QString cardStyle()
{
    return QStringLiteral("QFrame#mapCard { background: rgba(127, 127, 127, 16);"
                          " border: 1px solid rgba(127, 127, 127, 58); border-radius: 6px; }");
}

/// 提示条（风险 / 缺依赖）用的颜色名，可直接塞进 QSS
inline QString noticeColorName()
{
    return isDarkTheme() ? QStringLiteral("#ef9a9a") : QStringLiteral("#c62828");
}

}  // namespace maps::ui
