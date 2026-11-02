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

#include "MapRepository.h"

class QLabel;
class QComboBox;
class QListView;
class QPushButton;
class QStandardItemModel;
class InstanceList;

namespace maps {

/**
 * 地图画廊：左边一排封面卡片，右边是只读详情栏。
 *
 * 只发命令，不干重活：启动、打开实例、导入都由宿主（MainWindow / MapGalleryWindow）接。
 * 这样这块 UI 既能塞进主窗口的 QStackedWidget，也能单独开窗，不用改两份逻辑。
 */
class MapGalleryPage : public QWidget {
    Q_OBJECT

   public:
    explicit MapGalleryPage(InstanceList* instances, QWidget* parent = nullptr);

    /// 重新扫描实例列表（导入/删除实例后调用）
    void refresh();

    QString selectedInstanceId() const;

   signals:
    /// worldFolder 为空表示「用主世界」（一包多世界时由详情栏的下拉框选择）
    void launchRequested(const QString& instanceId, const QString& worldFolder);
    void instanceOpenRequested(const QString& instanceId);
    /// archivePath 为空表示「让宿主弹文件选择框」
    void importRequested(const QString& archivePath);

   protected:
    // 外部文件拖进来 = 导入地图（实例页能拖，地图页也要能拖）
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private slots:
    void updateDetails();

   private:
    void buildUi();

    InstanceList* m_instances = nullptr;
    QListView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;

    QLabel* m_cover = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_confidence = nullptr;
    QLabel* m_stats = nullptr;
    QLabel* m_evidence = nullptr;
    /// 一包多世界时让玩家选进哪个（单世界时隐藏）
    QComboBox* m_worldCombo = nullptr;
    QPushButton* m_launchButton = nullptr;
    QPushButton* m_openButton = nullptr;

    QList<MapEntry> m_entries;
};

}  // namespace maps
