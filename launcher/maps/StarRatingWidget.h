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

#include <QWidget>

#include <functional>

namespace maps {

/**
 * 五星评分：内部值 1~10（半星粒度），0 = 未评分。
 *
 * 形态照 AIO 的 `starsHtml`：五颗星，每颗左半=半星、右半=整星；填充靠裁剪宽度实现。
 * 只读模式给右侧卡片用（AIO 的详情栏也是只读的 —— "看一眼再决定进不进"的地方
 * 不该有热区抢点击），可编辑模式给实例窗口的「信息」页用。
 */
class StarRatingWidget : public QWidget {
    Q_OBJECT

   public:
    explicit StarRatingWidget(QWidget* parent = nullptr);

    void setValue(int value);
    int value() const { return m_value; }

    /// 只读时不画热区、不改光标、不响应点击（AIO: `.rate:not(.is-editing) .star { cursor: default }`）
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }

    /// 不用信号，省一层 moc 连接：调用点都是就地回调
    std::function<void(int)> onChanged;

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    int valueAt(int x) const;

    int m_value = 0;
    int m_hover = 0;
    bool m_readOnly = false;
};

}  // namespace maps
