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

#include "AboutDialog.h"
#include <QIcon>
#include "Application.h"
#include "BuildConfig.h"
#include "Markdown.h"
#include "StringUtils.h"
#include "ui_AboutDialog.h"

#include <net/NetJob.h>

namespace {
QString getCreditsHtml()
{
    QFile dataFile(":/documents/credits.html");
    if (!dataFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file" << dataFile.fileName() << "for reading:" << dataFile.errorString();
        return {};
    }
    QString fileContent = QString::fromUtf8(dataFile.readAll());
    dataFile.close();

    return fileContent.arg(QObject::tr("%1 Developers").arg(BuildConfig.LAUNCHER_DISPLAYNAME), QObject::tr("MultiMC Developers"),
                           QObject::tr("With special thanks to"));
}

QString getLicenseHtml()
{
    QFile dataFile(":/documents/COPYING.md");
    if (dataFile.open(QIODevice::ReadOnly)) {
        QString output = markdownToHTML(dataFile.readAll());
        dataFile.close();
        return output;
    } else {
        qWarning() << "Failed to open file" << dataFile.fileName() << "for reading:" << dataFile.errorString();
        return QString();
    }
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    QString launcherName = BuildConfig.LAUNCHER_DISPLAYNAME;

    setWindowTitle(tr("About %1").arg(launcherName));

    QString chtml = getCreditsHtml();
    ui->creditsText->setHtml(StringUtils::htmlListPatch(chtml));

    QString lhtml = getLicenseHtml();
    ui->licenseText->setHtml(StringUtils::htmlListPatch(lhtml));

    ui->urlLabel->setOpenExternalLinks(true);

    ui->icon->setPixmap(APPLICATION->logo().pixmap(64));
    ui->title->setText(launcherName);

    /**
     * 上游那句"多实例启动器"的自述跟 Atlas 的定位无关（我们卖的是"点地图直接进图"），
     * 而且这一页是玩家唯一会主动打开、也唯一会看到"这是谁做的"的地方，
     * 所以在这里把**非官方**说清楚：与 Mojang / Microsoft / Prism / ATLauncher 都无关。
     * 措辞只陈述事实，不写免责长文。
     */
    ui->aboutLabel->setText(
        QStringLiteral("<html><head/><body><p>%1</p><p>%2</p></body></html>")
            .arg(tr("以地图为中心的 Minecraft 启动器：导入一张地图，点卡片直接进图。"),
                 tr("非官方产品：与 Mojang、Microsoft、Prism Launcher、PolyMC、MultiMC 及 ATLauncher "
                    "均无关联，也未获其背书。不含任何 Minecraft 游戏文件。")));

    ui->versionLabel->setText(BuildConfig.printableVersionString());

    // "unknown" 是构建时没有传 platform 的占位值，摆出来只会让人以为出错了
    if (!BuildConfig.BUILD_PLATFORM.isEmpty() && BuildConfig.BUILD_PLATFORM != QLatin1String("unknown"))
        ui->platformLabel->setText(tr("Platform") + ": " + BuildConfig.BUILD_PLATFORM);
    else
        ui->platformLabel->setVisible(false);

    if (!BuildConfig.GIT_COMMIT.isEmpty())
        ui->commitLabel->setText(tr("Commit: %1").arg(BuildConfig.GIT_COMMIT));
    else
        ui->commitLabel->setVisible(false);

    if (!BuildConfig.BUILD_DATE.isEmpty())
        ui->buildDateLabel->setText(tr("Build date: %1").arg(BuildConfig.BUILD_DATE));
    else
        ui->buildDateLabel->setVisible(false);

    if (!BuildConfig.VERSION_CHANNEL.isEmpty())
        ui->channelLabel->setText(tr("Channel") + ": " + BuildConfig.VERSION_CHANNEL);
    else
        ui->channelLabel->setVisible(false);

    // 分支没有固定仓库地址时不要显示一个空链接。
    // 地址来自 clone 的 origin（见 program_info/CMakeLists.txt），所以改名/换托管后
    // 重新 configure 一次就会跟着变，不会留下一个 404。
    if (BuildConfig.LAUNCHER_GIT.isEmpty()) {
        ui->urlLabel->setVisible(false);
    } else {
        QString urlText("<html><head/><body><p><a href=\"%1\">%1</a></p>"
                        "<p><a href=\"%2\">%3</a></p></body></html>");
        ui->urlLabel->setText(urlText.arg(BuildConfig.LAUNCHER_GIT, BuildConfig.LAUNCHER_GIT + QStringLiteral("/issues"),
                                          tr("问题反馈")));
    }

    ui->copyLabel->setText(BuildConfig.LAUNCHER_COPYRIGHT);

    connect(ui->closeButton, &QPushButton::clicked, this, &AboutDialog::close);

    connect(ui->aboutQt, &QPushButton::clicked, APPLICATION, &QApplication::aboutQt);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
