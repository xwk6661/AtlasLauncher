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
#include "MapGalleryWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QStatusBar>
#include <QToolBar>

#include "HallDialog.h"
#include "MapGalleryPage.h"

namespace maps {

MapGalleryWindow::MapGalleryWindow(InstanceList* instances, QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("地图画廊"));
    resize(1100, 720);

    m_page = new MapGalleryPage(instances, this);
    setCentralWidget(m_page);

    auto* toolbar = addToolBar(tr("地图"));
    toolbar->setObjectName(QStringLiteral("mapToolBar"));
    toolbar->setMovable(false);

    auto* importAction = toolbar->addAction(tr("导入地图…"));
    connect(importAction, &QAction::triggered, this, &MapGalleryWindow::chooseArchive);

    auto* hallAction = toolbar->addAction(tr("地图大厅"));
    connect(hallAction, &QAction::triggered, this, [this] {
        HallDialog dialog(this);
        dialog.exec();
    });

    auto* refreshAction = toolbar->addAction(tr("刷新"));
    connect(refreshAction, &QAction::triggered, m_page, &MapGalleryPage::refresh);

    connect(m_page, &MapGalleryPage::launchRequested, this, &MapGalleryWindow::launchRequested);  // (id, world)
    connect(m_page, &MapGalleryPage::instanceOpenRequested, this, &MapGalleryWindow::instanceOpenRequested);
    connect(m_page, &MapGalleryPage::importRequested, this, [this](const QString& path) {
        if (path.isEmpty())
            chooseArchive();
        else
            emit importRequested(path);
    });

    statusBar()->showMessage(tr("双击卡片直接进图；导入时会先判定版本并要求确认。"));
}

void MapGalleryWindow::chooseArchive()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择地图压缩包"), QString(),
        tr("Minecraft 地图 (*.zip *.rar *.7z *.tar *.gz);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    emit importRequested(path);
}

}  // namespace maps
