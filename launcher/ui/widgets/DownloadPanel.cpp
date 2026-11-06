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

#include "DownloadPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include "StringUtils.h"
#include "net/DownloadMonitor.h"

namespace {
/// 同时显示几行（再多窗口就太长了）
constexpr int kMaxRows = 6;

auto bytesText(qint64 bytes) -> QString
{
    return StringUtils::humanReadableFileSize(static_cast<double>(bytes));
}

auto speedText(double bytesPerSecond) -> QString
{
    if (bytesPerSecond < 1)
        return QStringLiteral("-");
    return bytesText(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}
}  // namespace

DownloadPanel::DownloadPanel(QWidget* parent) : QWidget(parent)
{
    // 纵向 Fixed：窗口宁可变高，也不能把行压扁（压扁之后文字会互相盖住 —— 2026-11-06 的截图）
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_summary = new QLabel(this);
    m_summary->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    root->addWidget(m_summary);

    for (int i = 0; i < kMaxRows; ++i) {
        Row row;
        row.root = new QWidget(this);
        row.root->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* line = new QHBoxLayout(row.root);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);

        row.name = new QLabel(row.root);
        row.name->setMinimumWidth(100);
        row.name->setMaximumWidth(240);
        row.name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        row.bar = new QProgressBar(row.root);
        row.bar->setTextVisible(false);
        row.bar->setFixedHeight(6);
        row.bar->setRange(0, 1000);
        row.bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        row.info = new QLabel(row.root);
        row.info->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // 按"最长可能出现的文案"定宽，避免数字一变宽就被进度条挤掉（截断）
        const int infoWidth =
            row.info->fontMetrics().horizontalAdvance(QStringLiteral("999.9/999.9 MiB · 9.9 MiB/s · ×32")) + 12;
        row.info->setMinimumWidth(infoWidth);
        row.info->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        line->addWidget(row.name);
        line->addWidget(row.bar, 1);
        line->addWidget(row.info);

        row.root->setVisible(false);
        root->addWidget(row.root);
        m_rows.append(row);
    }

    m_more = new QLabel(this);
    m_more->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_more->setVisible(false);
    root->addWidget(m_more);

    setVisible(false);
}

auto DownloadPanel::refresh() -> bool
{
    auto snapshot = Net::DownloadMonitor::instance()->snapshot();
    m_lastSpeed = snapshot.speed;

    const bool shouldShow = snapshot.active && snapshot.running > 0;
    const bool visibilityChanged = (shouldShow == isHidden());
    setVisible(shouldShow);
    if (!shouldShow) {
        m_lastSpeed = 0;
        for (auto& row : m_rows)
            row.root->setVisible(false);
        m_more->setVisible(false);
        return visibilityChanged;
    }

    // 汇总行：速度已经挪到窗口右上角了，这里改说"几路并行 + 已收多少"，
    // 正好补上"并行是看得见的"这一点，又不和别处重复。
    m_summary->setText(tr("下载中 · %1 个文件并行 · 已收 %2").arg(snapshot.running).arg(bytesText(snapshot.bytesDone)));

    const int shown = qMin(snapshot.items.size(), m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        auto& row = m_rows.at(i);
        if (i >= shown) {
            row.root->setVisible(false);
            continue;
        }
        const auto& item = snapshot.items.at(i);

        // 文件名放不下就中间省略：扩展名和版本号通常更有辨识度
        const int nameWidth = row.name->width() > 40 ? row.name->width() - 4 : 240;
        row.name->setText(row.name->fontMetrics().elidedText(item.name, Qt::ElideMiddle, nameWidth));

        if (item.total > 0) {
            row.bar->setRange(0, 1000);
            row.bar->setValue(int(qMin<qint64>(1000, item.done * 1000 / item.total)));
        } else {
            // 长度还没拿到（等响应头）：走不确定态，至少能看出"这条连接活着"
            row.bar->setRange(0, 0);
        }

        QString info = item.total > 0 ? QStringLiteral("%1/%2").arg(bytesText(item.done), bytesText(item.total))
                                      : bytesText(item.done);
        // 速度还是 0 的时候干脆不写：写成 "· - ·" 会变成一串没意义的点
        if (item.speed >= 1)
            info += QStringLiteral(" · ") + speedText(item.speed);
        if (item.parallel && item.chunks > 1)
            info += QStringLiteral(" · ×%1").arg(item.chunks);
        row.info->setText(info);

        row.root->setVisible(true);
    }

    if (snapshot.items.size() > shown) {
        m_more->setText(tr("还有 %1 个文件在排队").arg(snapshot.items.size() - shown));
        m_more->setVisible(true);
    } else {
        m_more->setVisible(false);
    }

    return visibilityChanged;
}
