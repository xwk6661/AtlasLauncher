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
#include "MapPageIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include "MapTheme.h"

namespace maps::pageIcons {

namespace {

constexpr int kCanvas = 48;

QPixmap canvas()
{
    QPixmap pixmap(kCanvas, kCanvas);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QIcon toIcon(const QPixmap& pixmap)
{
    QIcon icon;
    // 两个尺寸就够：侧栏用 16/24，别处（关于页、窗口图标）用大图时也不糊
    icon.addPixmap(pixmap);
    icon.addPixmap(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon.addPixmap(pixmap.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return icon;
}

}  // namespace

QIcon library()
{
    QPixmap pixmap = canvas();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor ink = ui::bodyTextColor();
    const QColor accent = ui::accentColor();

    QPen pen(ink, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // 底板 + 顶盖：一个"抽屉/书架"的轮廓
    painter.drawLine(QPointF(8, 38), QPointF(40, 38));
    painter.drawLine(QPointF(8, 38), QPointF(12, 42));
    painter.drawLine(QPointF(40, 38), QPointF(36, 42));
    painter.drawLine(QPointF(12, 42), QPointF(36, 42));
    painter.drawLine(QPointF(8, 38), QPointF(8, 14));
    painter.drawLine(QPointF(40, 38), QPointF(40, 14));
    painter.drawLine(QPointF(8, 14), QPointF(40, 14));

    // 三本竖着的书脊（中间那本用强调色，避免整图一片灰）
    painter.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(15, 20), QPointF(15, 33));
    painter.drawLine(QPointF(21, 20), QPointF(21, 33));

    painter.setPen(QPen(accent, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(27, 17), QPointF(27, 33));

    painter.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(33, 22), QPointF(33, 33));

    painter.end();
    return toIcon(pixmap);
}

QIcon ctmmaps()
{
    QPixmap pixmap = canvas();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor ink = ui::bodyTextColor();
    const QColor accent = ui::accentColor();

    const QPointF center(24, 22);
    const qreal radius = 14.0;
    const QRectF globe(center.x() - radius, center.y() - radius, radius * 2, radius * 2);

    QPen pen(ink, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(globe);

    // 经线（一个窄椭圆）+ 两条纬线：一眼能认出是地球而不是圆
    painter.drawEllipse(QRectF(center.x() - radius * 0.45, center.y() - radius, radius * 0.9, radius * 2));
    painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x() - radius * 0.88, center.y() - radius * 0.5),
                     QPointF(center.x() + radius * 0.88, center.y() - radius * 0.5));

    // 定位点：地球右下角的水滴 + 实心圆（"站上的条目"）
    QPainterPath pin;
    const QPointF tip(36, 42);
    pin.moveTo(tip);
    pin.cubicTo(QPointF(29, 34), QPointF(28, 30), QPointF(31, 28));
    pin.cubicTo(QPointF(34, 26), QPointF(38, 28), QPointF(38, 31));
    pin.cubicTo(QPointF(38, 34), QPointF(36, 38), QPointF(36, 42));
    painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(accent);
    painter.drawPath(pin);

    painter.end();
    return toIcon(pixmap);
}

}  // namespace maps::pageIcons
