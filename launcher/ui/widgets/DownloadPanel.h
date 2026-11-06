// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas Launcher - Minecraft launcher fork with a map-first workflow
 *  Copyright (C) 2026 Atlas contributors
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

#include <QVector>
#include <QWidget>

class QLabel;
class QProgressBar;

/**
 * 下载面板：把"现在到底在下什么、有多快"摆出来。
 *
 * Prism 原生只给一条"第几个文件 / 共几个"的进度条，弱网下同时下十几个文件时，
 * 那条东⻄几乎是静止的 —— 用户既看不到并行，也判断不出"是慢还是卡了"（06 §41）。
 * 这里补上 HMCL 那种观感：
 *
 *   下载中 · 12.3 MB · 1.8 MB/s · 45 / 320 个文件
 *   forge-1.20.1.jar      [====      ]  3.2 / 8.1 MB   720 KB/s   ×12
 *   ...
 */
class DownloadPanel : public QWidget {
    Q_OBJECT

   public:
    explicit DownloadPanel(QWidget* parent = nullptr);

    /// 拉一次快照并重绘；返回"可见性是否变了"（调用方据此决定要不要重算窗口尺寸）
    auto refresh() -> bool;

    /// 面板是否正在显示下载内容
    auto isActive() const -> bool { return !isHidden(); }
    /// 最近一次刷新算出来的整体速度（B/s），给窗口右上角复用
    auto lastSpeed() const -> double { return m_lastSpeed; }

   private:
    struct Row {
        QWidget* root = nullptr;
        QLabel* name = nullptr;
        QProgressBar* bar = nullptr;
        QLabel* info = nullptr;
    };

    QLabel* m_summary = nullptr;
    QLabel* m_more = nullptr;
    QVector<Row> m_rows;
    double m_lastSpeed = 0;
};
