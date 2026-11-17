// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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

#pragma once
#include <QPointer>
#include <QWidget>

#include <tools/BaseProfiler.h>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/launch/MinecraftTarget.h"

class InstanceWindow;

enum class LaunchDecision { Undecided, Continue, Abort };

class LaunchController : public Task {
    Q_OBJECT
   public:
    void executeTask() override;

    LaunchController();
    ~LaunchController() override = default;

    void setInstance(MinecraftInstance* instance) { m_instance = instance; }

    MinecraftInstance* instance() const { return m_instance; }

    void setLaunchMode(const LaunchMode mode) { m_wantedLaunchMode = mode; }

    void setOfflineName(const QString& offlineName) { m_offlineName = offlineName; }

    void setProfiler(BaseProfilerFactory* profiler) { m_profiler = profiler; }

    void setParentWidget(QWidget* widget) { m_parentWidget = widget; }

    void setTargetToJoin(MinecraftTarget::Ptr targetToJoin) { m_targetToJoin = std::move(targetToJoin); }

    void setAccountToUse(MinecraftAccountPtr accountToUse) { m_accountToUse = std::move(accountToUse); }

    QString id() const { return m_instance->id(); }

    bool abort() override;

    /**
     * 不弹确认框、不跑事件循环的强制结束。
     *
     * `abort()` 会先问一句「Kill Minecraft?」——它对人是合适的，对无人值守的自动流程
     * （地图的「退出即关」回退通道）就是死锁：`exec()` 把 GUI 线程挂在模态框上，
     * 玩家没看见窗口，游戏就永远退不掉。自动流程一律走这条。
     */
    bool forceAbort();

   private:
    /// abort() 与 forceAbort() 的共用实现；askUser=false 时跳过确认
    bool abortInternal(bool askUser);
    void login();
    void launchInstance();
    void decideAccount();
    LaunchDecision decideLaunchMode();
    bool askPlayDemo() const;
    QString askOfflineName(const QString& playerName, bool* ok = nullptr);
    bool reauthenticateAccount(const MinecraftAccountPtr& account, const QString& reason);

   private slots:
    void readyForLaunch();

    void onSucceeded();
    void onFailed(QString reason);
    void onProgressRequested(Task* task) const;

   private:
    LaunchMode m_wantedLaunchMode = LaunchMode::Normal;
    LaunchMode m_actualLaunchMode = LaunchMode::Normal;
    BaseProfilerFactory* m_profiler = nullptr;
    QString m_offlineName;
    MinecraftInstance* m_instance = nullptr;
    /// 用 QPointer：实例窗口 / 主窗口可能在启动过程中被销毁，
    /// 裸指针留着要么悬空、要么让确认框变成没有父窗口的野模态
    QPointer<QWidget> m_parentWidget = nullptr;
    InstanceWindow* m_console = nullptr;
    MinecraftAccountPtr m_accountToUse = nullptr;
    AuthSessionPtr m_session = nullptr;
    LaunchTask* m_launcher = nullptr;
    MinecraftTarget::Ptr m_targetToJoin = nullptr;
};
