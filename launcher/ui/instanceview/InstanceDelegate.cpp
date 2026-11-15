// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "InstanceDelegate.h"
#include <QApplication>
#include <QDebug>
#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

#include <QIcon>
#include <QTextEdit>
#include "BaseInstance.h"
#include "InstanceList.h"
#include "maps/SeriesNames.h"

namespace {

/// 标签药丸的内边距与和名字之间的间隙
constexpr int kTagPadH = 5;
constexpr int kTagPadV = 1;
constexpr int kTagGap = 2;

/**
 * 实例名里的「系列标签」：`[US1] Lacuna Sanctum` → 标签 `US1`、名字 `Lacuna Sanctum`。
 *
 * 只认「1~4 个字母 + 1~4 位数字」的括号前缀。这条写法是玩家自己约定出来的
 * （`[US1]` / `[RC3]` / `[SP03]`），而不是放开的 `^\[.*\]` —— 后者会把
 * `[1.8.9] 某图`、`[测试] 某图` 也吃掉，那些括号是名字的一部分，拆掉等于改名。
 */
struct InstanceTag {
    QString tag;     ///< 括号内原样文本（US1）
    QString name;    ///< 去掉标签后的名字
    bool valid = false;
};

InstanceTag splitInstanceTag(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*\\[([A-Za-z]{1,4}\\d{1,4})\\]\\s*(.+)$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return InstanceTag{ {}, text, false };

    InstanceTag out;
    out.tag = match.captured(1);
    out.name = match.captured(2).trimmed();
    out.valid = !out.name.isEmpty();
    return out;
}

QFont tagFont(const QFont& base)
{
    QFont font = base;
    // 比名字小两档：标签是"补充说明"，不该和名字抢注意力
    font.setPointSizeF(qMax(7.0, base.pointSizeF() - 2.0));
    return font;
}

/// 标签块占的高度（含与名字之间的间隙）。没有标签时是 0。
int tagBlockHeight(const QFont& base)
{
    const QFontMetrics metrics(tagFont(base));
    return metrics.height() + 2 * kTagPadV + kTagGap + 1;
}

/**
 * 画标签药丸。位置在文字区水平居中、**名字下方**（`top` 是名字最后一行的底边）。
 *
 * 放在名字下面而不是上面：图标的视觉重心本来就在上半格，标签压在上面会把名字往下推，
 * 一排实例看起来像"标签是主体、名字是注脚"；放下面等于给名字加一条脚注。
 */
void drawInstanceTag(QPainter* painter, const QStyleOptionViewItem& opt, const InstanceTag& tag, const QRect& textRect, int top)
{
    if (!tag.valid)
        return;

    const QFont font = tagFont(opt.font);
    const QFontMetrics metrics(font);

    // 统一走次要色，不按系列上色：一排彩色小字会把图标压下去，而"是哪个系列"现在由文字
    // （`Ragecraft 3`）说明，颜色不再是唯一的区分手段。
    QColor textColor;
    QColor borderColor;
    QColor fillColor;
    if (opt.state & QStyle::State_Selected) {
        textColor = opt.palette.color(QPalette::HighlightedText);
        borderColor = textColor;
        fillColor = textColor;
    } else {
        const bool dark = opt.palette.color(QPalette::Window).lightness() < 128;
        textColor = opt.palette.color(QPalette::WindowText);
        textColor.setAlpha(dark ? 190 : 165);
        borderColor = textColor;
        borderColor.setAlpha(dark ? 90 : 75);
        fillColor = textColor;
        fillColor.setAlpha(dark ? 18 : 14);
    }
    borderColor.setAlpha(qMin(borderColor.alpha(), 160));
    fillColor.setAlpha(qMin(fillColor.alpha(), 40));

    // 表里的系列名可能比"代码"长（`Untold Stories 9`）：放不下就省略，绝不撑破格子
    const int maxTextWidth = qMax(12, textRect.width() - 2 * kTagPadH - 4);
    const QString text = metrics.elidedText(maps::series::displayLabel(tag.tag), Qt::ElideRight, maxTextWidth);

    const int width = qMin(metrics.horizontalAdvance(text) + 2 * kTagPadH, textRect.width() - 2);
    const int height = metrics.height() + 2 * kTagPadV;
    const QRect pill(textRect.center().x() - width / 2, top + 1, width, height);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(borderColor, 1.0));
    painter->setBrush(fillColor);
    painter->drawRoundedRect(QRectF(pill).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    painter->setFont(font);
    painter->setPen(textColor);
    painter->drawText(pill, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace

// Origin: Qt
static void viewItemTextLayout(QTextLayout& textLayout, int lineWidth, qreal& height, qreal& widthUsed)
{
    height = 0;
    widthUsed = 0;
    textLayout.beginLayout();
    QString str = textLayout.text();
    while (true) {
        QTextLine line = textLayout.createLine();
        if (!line.isValid())
            break;
        if (line.textLength() == 0)
            break;
        line.setLineWidth(lineWidth);
        line.setPosition(QPointF(0, height));
        height += line.height();
        widthUsed = qMax(widthUsed, line.naturalTextWidth());
    }
    textLayout.endLayout();
}

ListViewDelegate::ListViewDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void drawSelectionRect(QPainter* painter, const QStyleOptionViewItem& option, const QRect& rect)
{
    if ((option.state & QStyle::State_Selected))
        painter->fillRect(rect, option.palette.brush(QPalette::Highlight));
    else {
        QColor backgroundColor = option.palette.color(QPalette::Window);
        backgroundColor.setAlpha(160);
        painter->fillRect(rect, QBrush(backgroundColor));
    }
}

void drawBadges(QPainter* painter, const QStyleOptionViewItem& option, BaseInstance* instance, QIcon::Mode mode, QIcon::State state)
{
    QList<QString> pixmaps;
    if (instance->isRunning()) {
        pixmaps.append("status-running");
    } else if (instance->hasCrashed() || instance->hasVersionBroken()) {
        pixmaps.append("status-bad");
    }

    static const int itemSide = 24;
    static const int spacing = 1;
    const int itemsPerRow = qMax(1, qFloor(double(option.rect.width() + spacing) / double(itemSide + spacing)));
    const int rows = qCeil((double)pixmaps.size() / (double)itemsPerRow);
    QListIterator<QString> it(pixmaps);
    painter->translate(option.rect.topLeft());
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < itemsPerRow; ++x) {
            if (!it.hasNext()) {
                return;
            }
            // FIXME: inject this.
            auto icon = QIcon::fromTheme(it.next());
            // opt.icon.paint(painter, iconbox, Qt::AlignCenter, mode, state);
            const QPixmap pixmap;
            // itemSide
            QRect badgeRect(option.rect.width() - x * itemSide + qMax(x - 1, 0) * spacing - itemSide,
                            y * itemSide + qMax(y - 1, 0) * spacing, itemSide, itemSide);
            icon.paint(painter, badgeRect, Qt::AlignCenter, mode, state);
        }
    }
    painter->translate(-option.rect.topLeft());
}

static QSize viewItemTextSize(const QStyleOptionViewItem* option)
{
    QStyle* style = option->widget ? option->widget->style() : QApplication::style();
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout textLayout;
    textLayout.setTextOption(textOption);
    textLayout.setFont(option->font);
    textLayout.setText(option->text);
    const int textMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, option, option->widget) + 1;
    QRect bounds(0, 0, 100 - 2 * textMargin, 600);
    qreal height = 0, widthUsed = 0;
    viewItemTextLayout(textLayout, bounds.width(), height, widthUsed);
    const QSize size(qCeil(widthUsed), qCeil(height));
    return QSize(size.width() + 2 * textMargin, size.height());
}

void ListViewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 名字里的 `[US1]` 拆出来单独画成标签，名字本身只写「Lacuna Sanctum」。
    // 实例名（数据）一个字都不改：标签仍然在名字里，改名/搜索/拖拽都按原名走。
    const InstanceTag tag = splitInstanceTag(index.data().toString());

    painter->save();
    painter->setClipRect(opt.rect);

    opt.features |= QStyleOptionViewItem::WrapText;
    opt.text = tag.valid ? tag.name : index.data().toString();
    opt.textElideMode = Qt::ElideRight;
    opt.displayAlignment = Qt::AlignTop | Qt::AlignHCenter;

    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();

    const int iconSize = 48;
    QRect iconbox = opt.rect;
    const int textMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, 0, opt.widget) + 1;
    QRect textRect = opt.rect;
    QRect textHighlightRect = textRect;
    // clip the decoration on top, remove width padding
    textRect.adjust(textMargin, iconSize + textMargin + 5, -textMargin, 0);

    textHighlightRect.adjust(0, iconSize + 5, 0, 0);

    // draw background
    drawSelectionRect(painter, opt, textHighlightRect);

    // icon mode and state, also used for badges
    QIcon::Mode mode = QIcon::Normal;
    if (!(opt.state & QStyle::State_Enabled))
        mode = QIcon::Disabled;
    else if (opt.state & QStyle::State_Selected)
        mode = QIcon::Selected;
    QIcon::State state = opt.state & QStyle::State_Open ? QIcon::On : QIcon::Off;

    // draw the icon
    {
        iconbox.setHeight(iconSize);
        opt.icon.paint(painter, iconbox, Qt::AlignCenter, mode, state);
    }
    // set the text colors
    QPalette::ColorGroup cg = opt.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
    if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active))
        cg = QPalette::Inactive;
    if (opt.state & QStyle::State_Selected) {
        painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
    } else {
        painter->setPen(opt.palette.color(cg, QPalette::Text));
    }

    // draw the text
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    textOption.setTextDirection(opt.direction);
    textOption.setAlignment(QStyle::visualAlignment(opt.direction, opt.displayAlignment));
    QTextLayout textLayout;
    textLayout.setTextOption(textOption);
    textLayout.setFont(opt.font);
    textLayout.setText(opt.text);

    qreal width, height;
    viewItemTextLayout(textLayout, textRect.width(), height, width);

    const int lineCount = textLayout.lineCount();

    const QRect layoutRect = QStyle::alignedRect(opt.direction, opt.displayAlignment, QSize(textRect.width(), int(height)), textRect);
    const QPointF position = layoutRect.topLeft();
    for (int i = 0; i < lineCount; ++i) {
        const QTextLine line = textLayout.lineAt(i);
        line.draw(painter, position);
    }

    // 标签画在名字**下方**：名字先落地，系列作为脚注跟一行
    drawInstanceTag(painter, opt, tag, textRect, int(layoutRect.top() + height) + kTagGap);

    // FIXME: this really has no business of being here. Make generic.
    auto instance = (BaseInstance*)index.data(InstanceList::InstancePointerRole).value<void*>();
    if (instance) {
        drawBadges(painter, opt, instance, mode, state);
    }

    painter->restore();
}

QSize ListViewDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const InstanceTag tag = splitInstanceTag(index.data().toString());

    opt.features |= QStyleOptionViewItem::WrapText;
    opt.text = tag.valid ? tag.name : index.data().toString();
    opt.textElideMode = Qt::ElideRight;
    opt.displayAlignment = Qt::AlignTop | Qt::AlignHCenter;

    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    const int textMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, opt.widget) + 1;
    int height = 48 + textMargin * 2 + 5;  // TODO: turn constants into variables
    QSize szz = viewItemTextSize(&opt);
    height += szz.height();
    // 标签占的那一行也要算进格子高度，否则下一行的图标会压到标签上
    if (tag.valid)
        height += tagBlockHeight(option.font);
    // FIXME: maybe the icon items could scale and keep proportions?
    QSize sz(100, height);
    return sz;
}

class NoReturnTextEdit : public QTextEdit {
    Q_OBJECT
   public:
    explicit NoReturnTextEdit(QWidget* parent) : QTextEdit(parent)
    {
        setTextInteractionFlags(Qt::TextEditorInteraction);
        setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    }
    bool event(QEvent* event) override
    {
        auto eventType = event->type();
        if (eventType == QEvent::KeyPress || eventType == QEvent::KeyRelease) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            auto key = keyEvent->key();
            if ((key == Qt::Key_Return || key == Qt::Key_Enter) && eventType == QEvent::KeyPress) {
                emit editingDone();
                return true;
            }
            if (key == Qt::Key_Tab) {
                return true;
            }
        }
        return QTextEdit::event(event);
    }
   signals:
    void editingDone();
};

void ListViewDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int iconSize = 48;
    // 标签在名字下方，改名的输入框仍然从名字那一行开始（和绘制时的文字位置对齐）
    QRect textRect = option.rect;
    textRect.adjust(0, iconSize + 5, 0, 0);
    editor->setGeometry(textRect);
}

void ListViewDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto text = index.data(Qt::EditRole).toString();
    QTextEdit* realEditor = qobject_cast<NoReturnTextEdit*>(editor);
    realEditor->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    realEditor->append(text);
    realEditor->selectAll();
    realEditor->document()->clearUndoRedoStacks();
}

void ListViewDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    QTextEdit* realEditor = qobject_cast<NoReturnTextEdit*>(editor);
    QString text = realEditor->toPlainText();
    const auto before = model->data(index).toString();
    if (model->setData(index, text)) {
        emit textChanged(before, model->data(index).toString());
    }
}

QWidget* ListViewDelegate::createEditor(QWidget* parent,
                                        [[maybe_unused]] const QStyleOptionViewItem& option,
                                        [[maybe_unused]] const QModelIndex& index) const
{
    auto editor = new NoReturnTextEdit(parent);
    connect(editor, &NoReturnTextEdit::editingDone, this, &ListViewDelegate::editingDone);
    return editor;
}

void ListViewDelegate::editingDone()
{
    NoReturnTextEdit* editor = qobject_cast<NoReturnTextEdit*>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}

#include "InstanceDelegate.moc"
