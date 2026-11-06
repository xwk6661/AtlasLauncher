/// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PrismLaucher - Minecraft Launcher
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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

#include "ProgressDialog.h"
#include <QPoint>
#include "ui_ProgressDialog.h"

#include <QDebug>
#include <QKeyEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>

#include "StringUtils.h"
#include "tasks/Task.h"

#include "net/DownloadMonitor.h"
#include "ui/widgets/DownloadPanel.h"
#include "ui/widgets/SubTaskProgressBar.h"

// map a value in a numeric range of an arbitrary type to between 0 and INT_MAX
// for getting the best precision out of the qt progress bar
template <typename T>
std::tuple<int, int> map_int_zero_max(T current, T range_max, T range_min)
    requires(std::is_arithmetic_v<T>)
{
    int int_max = std::numeric_limits<int>::max();

    auto type_range = range_max - range_min;
    if (type_range == 0) {
        return { 0, int_max };
    }
    double percentage = static_cast<double>(current - range_min) / static_cast<double>(type_range);
    int mapped_current = percentage * int_max;

    return { mapped_current, int_max };
}

ProgressDialog::ProgressDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    ui->taskProgressScrollArea->setHidden(true);
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setAttribute(Qt::WidgetAttribute::WA_QuitOnClose, true);
    changeProgress(0, 100);
    updateSize(true);
    hideSkipButton();

    // 下载面板插在总进度条和步骤列表之间：网络阶段它最有用，其它任务整块隐藏。
    auto* panel = new DownloadPanel(this);
    m_downloadPanel = panel;
    if (auto* box = qobject_cast<QVBoxLayout*>(ui->verticalLayout)) {
        const int barIndex = box->indexOf(ui->globalProgressBar);
        box->insertWidget(barIndex + 1, panel);
    }

    m_downloadTimer = new QTimer(this);
    m_downloadTimer->setInterval(500);
    connect(m_downloadTimer, &QTimer::timeout, this, &ProgressDialog::tickDownloadPanel);
}

void ProgressDialog::tickDownloadPanel()
{
    auto* panel = static_cast<DownloadPanel*>(m_downloadPanel);
    if (panel == nullptr)
        return;

    const bool wasActive = panel->isActive();
    const int beforeHeight = wasActive ? panel->sizeHint().height() : 0;

    panel->refresh();

    const bool active = panel->isActive();
    const int afterHeight = active ? panel->sizeHint().height() : 0;

    // 右上角：下载中显示"整体速度"（所有并行文件加起来），没在下载就还给任务自己的 details
    const bool activeChanged = (active != m_downloadActive);
    m_downloadActive = active;
    if (active) {
        const double speed = panel->lastSpeed();
        ui->globalStatusDetailsLabel->setText(
            speed >= 1 ? tr("↓ %1/s").arg(StringUtils::humanReadableFileSize(speed)) : QString());
    } else if (activeChanged) {
        ui->globalStatusDetailsLabel->setText(m_task != nullptr ? m_task->getDetails() : QString());
    }
    ui->globalStatusDetailsLabel->adjustSize();

    // 下载面板在的时候让"步骤条"让位：两者说的是同一件事（当前阶段 + 进度），
    // 而步骤条会占掉一大块高度，把真正要看的文件列表挤扁（2026-11-06 的截图）。
    ui->taskProgressScrollArea->setHidden(active || !m_is_multi_step);

    // 需要重算尺寸的两种时机：面板出现/消失，或者行数变了（面板变高）
    if (activeChanged || afterHeight > beforeHeight) {
        updateSize();
        changeProgress(ui->globalProgressBar->value(), ui->globalProgressBar->maximum());
    }
}

void ProgressDialog::stopDownloadPanel()
{
    if (m_downloadTimer)
        m_downloadTimer->stop();
    if (auto* panel = static_cast<DownloadPanel*>(m_downloadPanel)) {
        if (!panel->isHidden()) {
            panel->hide();
            ui->taskProgressScrollArea->setHidden(!m_is_multi_step);
            updateSize();
        }
    }
    // 右上角还给任务自己的 details
    m_downloadActive = false;
    ui->globalStatusDetailsLabel->setText(m_task != nullptr ? m_task->getDetails() : QString());
    ui->globalProgressBar->setFormat(QString());
}

void ProgressDialog::hideSkipButton()
{
    ui->skipButton->setEnabled(false);
    ui->skipButton->setVisible(false);
    updateSize();
}

void ProgressDialog::showSkipButton(const QString& label)
{
    ui->skipButton->setAutoDefault(false);
    ui->skipButton->setDefault(false);
    ui->skipButton->setFocusPolicy(Qt::ClickFocus);
    ui->skipButton->setEnabled(true);
    ui->skipButton->setVisible(true);
    ui->skipButton->setText(label);
    updateSize();
}

void ProgressDialog::on_skipButton_clicked(bool checked)
{
    Q_UNUSED(checked);
    if (ui->skipButton->isEnabled())  // prevent other triggers from aborting
        m_task->abort();
}

ProgressDialog::~ProgressDialog()
{
    for (auto conn : this->m_taskConnections) {
        disconnect(conn);
    }
    delete ui;
}

void ProgressDialog::updateSize(bool recenterParent)
{
    QSize lastSize = this->size();
    QPoint lastPos = this->pos();
    int minHeight = ui->globalStatusDetailsLabel->minimumSize().height() + (ui->verticalLayout->spacing() * 2);
    minHeight += ui->globalProgressBar->minimumSize().height() + ui->verticalLayout->spacing();
    if (!ui->taskProgressScrollArea->isHidden())
        minHeight += ui->taskProgressScrollArea->minimumSizeHint().height() + ui->verticalLayout->spacing();
    if (m_downloadPanel != nullptr && !m_downloadPanel->isHidden())
        minHeight += m_downloadPanel->sizeHint().height() + ui->verticalLayout->spacing();
    if (ui->skipButton->isVisible())
        minHeight += ui->skipButton->height() + ui->verticalLayout->spacing();
    minHeight = std::max(minHeight, 60);
    const int minWidth = (m_downloadPanel != nullptr && !m_downloadPanel->isHidden()) ? 620 : 480;
    QSize minSize = QSize(minWidth, minHeight);

    setMinimumSize(minSize);
    adjustSize();

    QSize newSize = this->size();
    // if the current window is a different size
    auto parent = this->parentWidget();
    if (recenterParent && parent) {
        auto newX = std::max(0, parent->x() + ((parent->width() - newSize.width()) / 2));
        auto newY = std::max(0, parent->y() + ((parent->height() - newSize.height()) / 2));
        this->move(newX, newY);
    } else if (lastSize != newSize) {
        // center on old position after resize
        QSize sizeDiff = lastSize - newSize;  // last size was smaller, the results should be negative
        auto newX = std::max(0, lastPos.x() + (sizeDiff.width() / 2));
        auto newY = std::max(0, lastPos.y() + (sizeDiff.height() / 2));
        this->move(newX, newY);
    }
}

int ProgressDialog::execWithTask(Task* task)
{
    this->m_task = task;

    if (!task) {
        qDebug() << "Programmer error: Progress dialog created with null task.";
        return QDialog::DialogCode::Accepted;
    }

    QDialog::DialogCode result{};
    if (handleImmediateResult(result)) {
        return result;
    }

    // Connect signals.
    this->m_taskConnections.push_back(connect(task, &Task::started, this, &ProgressDialog::onTaskStarted));
    this->m_taskConnections.push_back(connect(task, &Task::failed, this, &ProgressDialog::onTaskFailed));
    this->m_taskConnections.push_back(connect(task, &Task::succeeded, this, &ProgressDialog::onTaskSucceeded));
    this->m_taskConnections.push_back(connect(task, &Task::status, this, &ProgressDialog::changeStatus));
    this->m_taskConnections.push_back(connect(task, &Task::details, this, &ProgressDialog::changeStatus));
    this->m_taskConnections.push_back(connect(task, &Task::stepProgress, this, &ProgressDialog::changeStepProgress));
    this->m_taskConnections.push_back(connect(task, &Task::progress, this, &ProgressDialog::changeProgress));
    this->m_taskConnections.push_back(connect(task, &Task::aborted, this, &ProgressDialog::hide));
    this->m_taskConnections.push_back(connect(task, &Task::abortStatusChanged, ui->skipButton, &QPushButton::setEnabled));
    this->m_taskConnections.push_back(connect(task, &Task::abortButtonTextChanged, ui->skipButton, &QPushButton::setText));
    this->m_taskConnections.push_back(connect(task, &Task::finished, this, [this] { stopDownloadPanel(); }));

    m_is_multi_step = task->isMultiStep();
    ui->taskProgressScrollArea->setHidden(!m_is_multi_step);
    updateSize();

    // 下载观测是旁路：任务链里有没有 NetJob、在第几层，这里都不用管
    if (m_downloadTimer != nullptr)
        m_downloadTimer->start();

    // It's a good idea to start the task after we entered the dialog's event loop :^)
    if (!task->isRunning()) {
        QMetaObject::invokeMethod(task, &Task::start, Qt::QueuedConnection);
    } else {
        changeStatus(task->getStatus());
        changeProgress(task->getProgress(), task->getTotalProgress());
    }

    return QDialog::exec();
}

// TODO: only provide the unique_ptr overloads
int ProgressDialog::execWithTask(std::unique_ptr<Task>&& task)
{
    connect(this, &ProgressDialog::destroyed, task.get(), &Task::deleteLater);
    return execWithTask(task.release());
}
int ProgressDialog::execWithTask(std::unique_ptr<Task>& task)
{
    connect(this, &ProgressDialog::destroyed, task.get(), &Task::deleteLater);
    return execWithTask(task.release());
}

bool ProgressDialog::handleImmediateResult(QDialog::DialogCode& result)
{
    if (m_task->isFinished()) {
        if (m_task->wasSuccessful()) {
            result = QDialog::Accepted;
        } else {
            result = QDialog::Rejected;
        }
        return true;
    }
    return false;
}

Task* ProgressDialog::getTask()
{
    return m_task;
}

void ProgressDialog::onTaskStarted() {}

void ProgressDialog::onTaskFailed([[maybe_unused]] QString failure)
{
    reject();
    hide();
}

void ProgressDialog::onTaskSucceeded()
{
    accept();
    hide();
}

void ProgressDialog::changeStatus([[maybe_unused]] const QString& status)
{
    ui->globalStatusLabel->setText(m_task->getStatus());
    ui->globalStatusLabel->adjustSize();
    // 下载中右上角显示的是总速度（由下载面板每 500ms 刷新），
    // 单个请求的 details（"78.16 KiB / 78.16 KiB 63.39 KiB/s"）和面板重复，不再覆盖它
    if (!m_downloadActive)
        ui->globalStatusDetailsLabel->setText(m_task->getDetails());
    ui->globalStatusDetailsLabel->adjustSize();

    updateSize();
}

void ProgressDialog::addTaskProgress(const TaskStepProgress& progress)
{
    SubTaskProgressBar* task_bar = new SubTaskProgressBar(this);
    taskProgress.insert(progress.uid, task_bar);
    ui->taskProgressLayout->addWidget(task_bar);
}

void ProgressDialog::changeStepProgress(const TaskStepProgress& task_progress)
{
    m_is_multi_step = true;
    // 下载面板正在显示时不展开步骤条：面板已经把"在做什么、多快"说清楚了，
    // 再放一条步骤条只会把窗口撑高、把文件列表挤扁。
    const bool panelActive = m_downloadPanel != nullptr && !m_downloadPanel->isHidden();
    if (!panelActive && ui->taskProgressScrollArea->isHidden()) {
        ui->taskProgressScrollArea->setHidden(false);
        updateSize();
    }

    if (!taskProgress.contains(task_progress.uid))
        addTaskProgress(task_progress);
    auto task_bar = taskProgress.value(task_progress.uid);

    const auto [mapped_current, mapped_total] = map_int_zero_max<qint64>(task_progress.current, task_progress.total, 0);
    if (task_progress.total <= 0) {
        task_bar->setRange(0, 0);
    } else {
        task_bar->setRange(0, mapped_total);
    }

    task_bar->setValue(mapped_current);
    task_bar->setStatus(task_progress.status);
    task_bar->setDetails(task_progress.details);
    task_bar->setVisible(!task_progress.isDone());
}

void ProgressDialog::changeProgress(qint64 current, qint64 total)
{
    ui->globalProgressBar->setMaximum(total);
    ui->globalProgressBar->setValue(current);

    // 下载阶段把"第几个文件"直接写进条上：原生只显示百分比，看不出还剩多少项
    if (m_downloadPanel != nullptr && !m_downloadPanel->isHidden())
        ui->globalProgressBar->setFormat(tr("%v / %m 个文件（%p%）"));
    else
        ui->globalProgressBar->setFormat(QString());
}

void ProgressDialog::keyPressEvent(QKeyEvent* e)
{
    if (ui->skipButton->isVisible()) {
        if (e->key() == Qt::Key_Escape) {
            on_skipButton_clicked(true);
            return;
        } else if (e->key() == Qt::Key_Tab) {
            ui->skipButton->setFocusPolicy(Qt::StrongFocus);
            ui->skipButton->setFocus();
            ui->skipButton->setAutoDefault(true);
            ui->skipButton->setDefault(true);
            return;
        }
    }
    QDialog::keyPressEvent(e);
}

void ProgressDialog::closeEvent(QCloseEvent* e)
{
    if (m_task && m_task->isRunning()) {
        e->ignore();
    } else {
        QDialog::closeEvent(e);
    }
}
