// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2026 Mark Deneen <mdeneen@gmail.com>
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

#include <QEvent>
#include <QObject>

class ToolTipFilter : public QObject {
    Q_OBJECT
   public:
    /// 必须能接 parent：这是一个装在 Application 上的长命过滤器，
    /// 不挂父对象就是进程退出前都不会回收的一处泄漏。
    explicit ToolTipFilter(QObject* parent = nullptr) : QObject(parent) {}

   protected:
    bool eventFilter(QObject* obj, QEvent* event);
};
