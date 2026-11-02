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

#include "ui/pages/BasePage.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace maps {

/**
 * 全局设置的「资源」页：把 assets / libraries / java 三个目录指到别的盘或别的启动器。
 *
 * 解决的问题：Mojang 官方 Java 运行时（`piston-data.mojang.com/v1/objects/…/jvm.dll`
 * 那一串，400 多个文件）和 assets/libraries 都躺在数据目录里。
 * 数据目录在 C 盘时既占系统盘，换一个启动器又得整份重下。
 */
class MapResourcePage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit MapResourcePage(QWidget* parent = nullptr);
    ~MapResourcePage() override = default;

    QString displayName() const override { return tr("资源"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
    QString id() const override { return QStringLiteral("resource-settings"); }

    bool apply() override;

   private:
    void browse();
    void detectExisting();
    void applyNow();
    void refreshStatus();

    QLineEdit* m_dir = nullptr;
    QCheckBox* m_migrate = nullptr;
    QPushButton* m_apply = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_current = nullptr;
};

}  // namespace maps
