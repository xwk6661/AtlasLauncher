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

#include <QMainWindow>

class InstanceList;

namespace maps {

class MapGalleryPage;

/**
 * 画廊的独立窗口形态。
 *
 * 设计文档里最终形态是「主窗口中央 QStackedWidget 切换」，但那是 M3 之后的手术
 * （`Application::m_mainWindow` 有 25 处引用）。先给一个独立窗口：
 * 零侵入、可立即使用，逻辑与将来的页面版完全共用。
 */
class MapGalleryWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit MapGalleryWindow(InstanceList* instances, QWidget* parent = nullptr);

    MapGalleryPage* page() const { return m_page; }

    /// 导入对话框选中的压缩包路径（选完就发信号，扫描与确认由宿主负责）
    void chooseArchive();

   signals:
    void importRequested(const QString& archivePath);
    void launchRequested(const QString& instanceId, const QString& worldFolder);
    void instanceOpenRequested(const QString& instanceId);

   private:
    MapGalleryPage* m_page = nullptr;
};

}  // namespace maps
