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
#include "MapSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "HallAPI.h"
#include "HallProxy.h"
#include "MapTheme.h"
#include "settings/SettingsObject.h"

namespace maps {

MapSettingsDialog::MapSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("地图设置"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* settings = APPLICATION->settings();

    m_autoEnter = new QCheckBox(tr("自动跳过主菜单，直接进这张图的存档"), this);
    m_autoEnter->setChecked(settings->get("MapAutoEnter").toBool());
    form->addRow(QString(), m_autoEnter);

    m_safeExit = new QCheckBox(tr("在游戏里退出存档后自动关闭游戏"), this);
    m_safeExit->setChecked(settings->get("MapSafeExit").toBool());
    form->addRow(QString(), m_safeExit);

    m_enterDelay = new QSpinBox(this);
    m_enterDelay->setRange(0, 60);
    m_enterDelay->setSuffix(tr(" 秒"));
    m_enterDelay->setValue(settings->get("MapEnterDelay").toInt());
    form->addRow(tr("进图前等待"), m_enterDelay);

    m_libraryDir = new QLineEdit(this);
    m_libraryDir->setPlaceholderText(tr("留空 = 启动器数据目录下的 maps"));
    m_libraryDir->setText(settings->get("MapLibraryDir").toString());
    form->addRow(tr("地图库目录"), m_libraryDir);

    m_hallEndpoint = new QLineEdit(this);
    m_hallEndpoint->setPlaceholderText(QStringLiteral("https://qtxjbukjyuylqjgiphxs.supabase.co"));
    m_hallEndpoint->setText(settings->get("MapHallEndpoint").toString());
    form->addRow(tr("大厅数据地址"), m_hallEndpoint);

    /**
     * 传输方式。默认 auto：先走 Qt 网络（有代理时最快），一旦被判"连接被掐断"
     * 就自动改用本机浏览器的无头模式 —— 实测这条线路封的是 SNI，
     * 浏览器走 ECH 能过，Qt 的 TLS 栈过不去（见 BrowserFetch.h）。
     */
    m_hallTransport = new QComboBox(this);
    m_hallTransport->addItem(tr("自动（先直连，被拦就改用本机浏览器）"), QStringLiteral("auto"));
    m_hallTransport->addItem(tr("只用网络（有可用代理时最稳）"), QStringLiteral("network"));
    m_hallTransport->addItem(tr("只用浏览器（启动 Edge/Chrome 取数据，较慢）"), QStringLiteral("browser"));
    m_hallTransport->setCurrentIndex(
        std::max(0, m_hallTransport->findData(settings->get("MapHallTransport").toString())));
    form->addRow(tr("大厅通道"), m_hallTransport);

    // 大厅出口：auto 会先探活再用（环境变量里的代理常常是别的程序留下的死地址）
    m_hallProxyMode = new QComboBox(this);
    m_hallProxyMode->addItem(tr("自动（环境变量 → 系统代理，先探活）"), QStringLiteral("auto"));
    m_hallProxyMode->addItem(tr("手动指定"), QStringLiteral("manual"));
    m_hallProxyMode->addItem(tr("始终直连"), QStringLiteral("off"));
    m_hallProxyMode->setCurrentIndex(
        std::max(0, m_hallProxyMode->findData(settings->get("MapHallProxyMode").toString())));
    form->addRow(tr("大厅代理"), m_hallProxyMode);

    m_hallProxyUrl = new QLineEdit(this);
    m_hallProxyUrl->setPlaceholderText(QStringLiteral("http://127.0.0.1:7890"));
    m_hallProxyUrl->setText(settings->get("MapHallProxyUrl").toString());
    form->addRow(tr("代理地址"), m_hallProxyUrl);

    // ── 控制台页签 ──
    // 一个地图一个实例，世界/服务器/备注/其他日志这四页在启动器里基本用不上，
    // 默认收起；改这里要重开实例窗口才生效（页面是在窗口构造时批量建的）。
    auto* tabsBox = new QGroupBox(tr("控制台页签（重开实例窗口后生效）"), this);
    auto* tabsLayout = new QVBoxLayout(tabsBox);
    m_showNotes = new QCheckBox(tr("显示「备注」页"), tabsBox);
    m_showNotes->setChecked(settings->get("MapConsoleShowNotes").toBool());
    m_showWorlds = new QCheckBox(tr("显示「世界」页"), tabsBox);
    m_showWorlds->setChecked(settings->get("MapConsoleShowWorlds").toBool());
    m_showServers = new QCheckBox(tr("显示「服务器」页"), tabsBox);
    m_showServers->setChecked(settings->get("MapConsoleShowServers").toBool());
    m_showOtherLogs = new QCheckBox(tr("显示「其他日志」页"), tabsBox);
    m_showOtherLogs->setChecked(settings->get("MapConsoleShowOtherLogs").toBool());
    tabsLayout->addWidget(m_showNotes);
    tabsLayout->addWidget(m_showWorlds);
    tabsLayout->addWidget(m_showServers);
    tabsLayout->addWidget(m_showOtherLogs);
    layout->addWidget(tabsBox);

    // 「测试连接」：照 AIO 设置页的 probe —— 存配置、立刻打一次真查询，
    // 把 耗时 / 结果 / 走的哪条出口 摆出来，不用回大厅页面猜。
    m_hallApi = new HallAPI(this);
    m_probeButton = new QPushButton(tr("测试连接"), this);
    form->addRow(QString(), m_probeButton);

    m_probeResult = new QLabel(this);
    m_probeResult->setWordWrap(true);
    m_probeResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_probeResult);

    layout->addLayout(form);

    connect(m_probeButton, &QPushButton::clicked, this, [this] {
        // 先落盘再探测：探测的必须是"这套设置"，不是上一套
        save();
        m_hallApi->setEndpoint(m_hallEndpoint->text().trimmed());
        m_probeResult->setText(tr("测试中…"));
        m_probeButton->setEnabled(false);
        m_hallApi->probe();
    });
    connect(m_hallApi, &HallAPI::probeDone, this, [this](bool ok, int elapsedMs, const QString& detail) {
        m_probeButton->setEnabled(true);
        m_probeResult->setText(QStringLiteral("%1 %2 ms · %3")
                                   .arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"))
                                   .arg(elapsedMs)
                                   .arg(detail));
        m_probeResult->setStyleSheet(
            ok ? QStringLiteral("color: %1;").arg(ui::confidenceColor(Confidence::High).name())
               : QStringLiteral("color: %1;").arg(ui::noticeColorName()));
    });

    m_hint = new QLabel(tr("关掉「自动跳过主菜单」后仍然会注入 Agent（落盘判定要用），只是不替玩家进存档。\n"
                           "1.20+ 的版本关闭后不再带 --quickPlaySingleplayer。\n"
                           "地图库保存导入过的原始压缩包（重装要用）；下载页仍然交给系统浏览器。\n"
                           "大厅数据地址留空 = 用官方地址；换成反代时填基地址即可。\n"
                           "大厅接口在部分网络下会被重置 —— 「大厅通道 = 自动」会改用本机浏览器去取（不需要代理）。"),
                        this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MapSettingsDialog::save()
{
    auto* settings = APPLICATION->settings();
    settings->set("MapAutoEnter", m_autoEnter->isChecked());
    settings->set("MapSafeExit", m_safeExit->isChecked());
    settings->set("MapEnterDelay", m_enterDelay->value());
    settings->set("MapLibraryDir", m_libraryDir->text().trimmed());
    settings->set("MapHallEndpoint", m_hallEndpoint->text().trimmed());
    settings->set("MapHallTransport", m_hallTransport->currentData().toString());
    settings->set("MapHallProxyMode", m_hallProxyMode->currentData().toString());
    settings->set("MapHallProxyUrl", m_hallProxyUrl->text().trimmed());
    settings->set("MapConsoleShowNotes", m_showNotes->isChecked());
    settings->set("MapConsoleShowWorlds", m_showWorlds->isChecked());
    settings->set("MapConsoleShowServers", m_showServers->isChecked());
    settings->set("MapConsoleShowOtherLogs", m_showOtherLogs->isChecked());
    // 改完立刻生效：下一次请求重新探活
    invalidateHallProxyCache();
}

}  // namespace maps
