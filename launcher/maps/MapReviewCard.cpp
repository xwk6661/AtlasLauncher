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
#include "MapReviewCard.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>

#include "HallTypes.h"
#include "MapTextUtils.h"
#include "MapTheme.h"

namespace maps {

QWidget* makeReviewCard(QWidget* parent, const HallReview& review, QLabel** nameLabel)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("mapCard"));
    // 纵向 Maximum：评论只有一两条时卡片不许被拉高（否则头像和正文之间会出现一大片空白）
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    card->setStyleSheet(ui::cardStyle());

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    // 首行只放名字 + 星级：日期/时长另起一行，窄面板下才不会被挤出横向滚动条
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);

    const QString displayName = review.userName.isEmpty() ? QObject::tr("匿名玩家") : review.userName;
    auto* name = new QLabel(displayName, card);
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    head->addWidget(name);
    if (nameLabel != nullptr)
        *nameLabel = name;

    if (review.rating > 0) {
        auto* stars = new QLabel(card);
        // 评论的 rating 是**1~5 的整数**（不是列表里那种 0~5 的平均分），别再除 2
        const int full = std::clamp(review.rating, 0, 5);
        stars->setText(QString(full, QChar(0x2605)) + QString(5 - full, QChar(0x2606)));
        stars->setStyleSheet(QStringLiteral("color: %1;").arg(ui::starColor().name()));
        head->addWidget(stars);
    }
    head->addStretch(1);
    layout->addLayout(head);

    QStringList trailing;
    if (review.playtimeMinutes > 0)
        trailing.append(text::formatPlaytime(review.playtimeMinutes));
    if (!review.playedDate.isEmpty())
        trailing.append(review.playedDate.left(10));
    if (!trailing.isEmpty()) {
        auto* meta = new QLabel(trailing.join(QStringLiteral(" · ")), card);
        meta->setEnabled(false);
        QFont metaFont = meta->font();
        if (metaFont.pointSizeF() > 0)
            metaFont.setPointSizeF(metaFont.pointSizeF() * 0.92);
        meta->setFont(metaFont);
        layout->addWidget(meta);
    }

    QString bodyText = review.text.trimmed();
    if (bodyText.isEmpty()) {
        // 只打了分没写字：给一行占位，别留空白卡片
        bodyText = QObject::tr("（只评分，没写内容）");
    }
    // 超长评论折叠：侧栏宽度有限，全展开会把下面的按钮顶走
    const int limit = 480;
    const bool clipped = bodyText.size() > limit;
    auto* body = new QLabel(clipped ? bodyText.left(limit) + QStringLiteral("…") : bodyText, card);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    if (clipped) {
        body->setToolTip(bodyText);
        body->setCursor(Qt::WhatsThisCursor);
    }
    layout->addWidget(body);

    return card;
}

}  // namespace maps
