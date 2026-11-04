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

#include <QDialog>
#include <QElapsedTimer>
#include <QList>
#include <QUrl>

#include "net/Request.h"

class QLabel;
class QPushButton;

/**
 * 下载源连通性测试。
 *
 * 逐条请求四类真实文件（版本清单 / 游戏资源 / Mojang 库 / Fabric 库），
 * 把**实际用到的地址**显示出来 —— 选了镜像时这里是 `bmclapi2.bangbang93.com/...`，
 * 因此这个窗口同时回答了"换源到底有没有生效"和"网络通不通"两个问题。
 *
 * 只探测、不写任何设置；跑完不保存任何东西。
 */
class DownloadSourceTestDialog : public QDialog {
    Q_OBJECT

   public:
    /// `mirror` 用**当前下拉框的选择**，而不是已保存的设置 —— 用户可能还没点确定
    explicit DownloadSourceTestDialog(bool mirror, QWidget* parent = nullptr);
    ~DownloadSourceTestDialog() override;

   protected:
    void reject() override;

   private:
    struct Probe {
        QString label;
        QUrl official;
        QUrl effective;
        QLabel* status = nullptr;
        QLabel* address = nullptr;
    };

    void buildUi();
    void startAll();
    void startNext();
    void settle(bool ok, const QString& detail);

    bool m_mirror;
    QList<Probe> m_probes;
    int m_index = 0;
    Net::Request::Ptr m_current;
    QElapsedTimer m_timer;
    QPushButton* m_retry = nullptr;
    QLabel* m_summary = nullptr;
    bool m_cancelled = false;
    /// 每轮测试自增。延迟回调只认自己那一轮，避免"重试后旧请求的回调落到新行上"
    int m_generation = 0;
};
