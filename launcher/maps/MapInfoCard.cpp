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
#include "MapInfoCard.h"

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "MapTheme.h"

namespace maps::ui {

QFrame* makeCard(QWidget* parent, const QString& title, QVBoxLayout** bodyOut)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("mapCard"));
    card->setStyleSheet(cardStyle());
    // 高度按内容走：卡片之间靠 spacing 分隔，不要让某一张把剩余空间吃掉
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);
    if (!title.isEmpty()) {
        auto* head = new QLabel(title, card);
        QFont headFont = head->font();
        headFont.setBold(true);
        head->setFont(headFont);
        outer->addWidget(head);
    }
    auto* body = new QVBoxLayout();
    body->setSpacing(6);
    outer->addLayout(body);
    if (bodyOut != nullptr)
        *bodyOut = body;
    return card;
}

QLabel* makeKeyLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(mutedTextColor().name()));
    return label;
}

void clearLayout(QLayout* layout)
{
    if (layout == nullptr)
        return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (auto* widget = item->widget())
            widget->deleteLater();
        if (auto* child = item->layout())
            clearLayout(child);
        delete item;
    }
}

}  // namespace maps::ui
