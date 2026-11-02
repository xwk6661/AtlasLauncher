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
#include "StarRatingWidget.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

#include "MapNotes.h"
#include "MapTheme.h"

namespace maps {
namespace {
constexpr int STAR_SIZE = 18;
constexpr int STAR_COUNT = 5;
}  // namespace

StarRatingWidget::StarRatingWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolTip(tr("点左半边 = 半星，右半边 = 整星；再点同一个位置取消评分"));
}

void StarRatingWidget::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    setCursor(readOnly ? Qt::ArrowCursor : Qt::PointingHandCursor);
    setToolTip(readOnly ? QString() : tr("点左半边 = 半星，右半边 = 整星；再点同一个位置取消评分"));
    update();
}

void StarRatingWidget::setValue(int value)
{
    m_value = std::clamp(value, 0, MapNotes::MAX_RATING);
    update();
}

QSize StarRatingWidget::sizeHint() const
{
    return { STAR_COUNT * STAR_SIZE + 2, STAR_SIZE + 2 };
}

void StarRatingWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont starFont = font();
    starFont.setPixelSize(STAR_SIZE - 4);
    painter.setFont(starFont);

    const QColor emptyColor = palette().color(QPalette::Disabled, QPalette::WindowText);
    const QColor fillColor = ui::starColor();
    // 悬停预览：鼠标扫过就能看到会打几分（不落盘）
    const int shown = m_readOnly ? m_value : (m_hover > 0 ? m_hover : m_value);

    for (int i = 0; i < STAR_COUNT; ++i) {
        const QRect cell(i * STAR_SIZE, 0, STAR_SIZE, STAR_SIZE);
        painter.setPen(emptyColor);
        painter.drawText(cell, Qt::AlignCenter, QString(QChar(0x2605)));

        const int fill = std::clamp(shown - i * 2, 0, 2);
        if (fill == 0)
            continue;
        painter.save();
        QRect clip = cell;
        clip.setWidth(cell.width() * fill / 2);  // 1 = 半格，2 = 整格
        painter.setClipRect(clip);
        painter.setPen(fillColor);
        painter.drawText(cell, Qt::AlignCenter, QString(QChar(0x2605)));
        painter.restore();
    }
}

void StarRatingWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_readOnly || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int value = valueAt(static_cast<int>(event->position().x()));
    setValue(value == m_value ? 0 : value);  // 再点自己 = 取消
    if (onChanged)
        onChanged(m_value);
}

void StarRatingWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_readOnly)
        return;
    const int value = valueAt(static_cast<int>(event->position().x()));
    if (value != m_hover) {
        m_hover = value;
        update();
    }
}

void StarRatingWidget::leaveEvent(QEvent* event)
{
    if (!m_readOnly) {
        m_hover = 0;
        update();
    }
    QWidget::leaveEvent(event);
}

int StarRatingWidget::valueAt(int x) const
{
    const int index = std::clamp(x / STAR_SIZE, 0, STAR_COUNT - 1);
    const int offset = x - index * STAR_SIZE;
    return offset < STAR_SIZE / 2 ? index * 2 + 1 : index * 2 + 2;
}

}  // namespace maps
