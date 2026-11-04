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
#include "MapSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "HallAPI.h"
#include "HallProxy.h"
#include "MapTheme.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

namespace maps {

MapSettingsWidget::MapSettingsWidget(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    // 实例作用域下 MapAutoEnter / MapSafeExit / MapEnterDelay 都是 OverrideSetting：
    // 未勾选覆盖时 get() 自动回落到全局值，所以这里读的永远是"实际生效"的值。
    m_settings = m_instance != nullptr ? m_instance->settings() : APPLICATION->settings();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 全局页和实例页签都可能很长，统一套一个无边框滚动区（横条永远不需要）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    buildBehaviorGroup(body, layout);
    if (m_instance == nullptr)
        buildGlobalGroups(body, layout);

    layout->addStretch(1);
    scroll->setWidget(body);

    loadSettings();
}

void MapSettingsWidget::buildBehaviorGroup(QWidget* parent, QVBoxLayout* layout)
{
    // 实例页签里这个分组是可勾选的：勾 = 用实例自己的值，不勾 = 跟随全局（和「游戏窗口」等分组一致）
    m_behaviorBox = new QGroupBox(m_instance != nullptr ? tr("覆盖全局的地图启动设置") : tr("地图启动"), parent);
    m_behaviorBox->setCheckable(m_instance != nullptr);

    auto* boxLayout = new QVBoxLayout(m_behaviorBox);
    boxLayout->setSpacing(6);

    m_autoEnter = new QCheckBox(tr("启动时自动跳过主菜单，直接进入这张图的存档"), m_behaviorBox);
    m_autoEnter->setToolTip(tr("关掉后 Agent 仍会注入（退出判定要用），只是不替玩家进存档；\n"
                               "1.20+ 的版本关闭后不再带 --quickPlaySingleplayer。"));
    m_safeExit = new QCheckBox(tr("在游戏里退出存档后自动关闭游戏"), m_behaviorBox);
    m_safeExit->setToolTip(tr("存档落盘后让 JVM 自己退出，启动器再据此判定「正常退出 / 异常退出」。"));
    m_skipPrompt = new QCheckBox(tr("跳过「不支持使用实验性设置的世界」确认页"), m_behaviorBox);
    m_skipPrompt->setToolTip(tr("地图自带改过世界生成的数据包时，1.20+ 每次加载都会先问一次；\n"
                                "勾上后由随包的 Java Agent 直接执行「我知道我在做什么」，不动世界数据。\n"
                                "只对 1.20+ 生效，且只认表里有的版本。"));
    boxLayout->addWidget(m_autoEnter);
    boxLayout->addWidget(m_safeExit);
    boxLayout->addWidget(m_skipPrompt);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    m_enterDelay = new QSpinBox(m_behaviorBox);
    m_enterDelay->setRange(0, 60);
    m_enterDelay->setSuffix(tr(" 秒"));
    m_enterDelay->setToolTip(tr("世界加载完之后再等一会儿才点「进入世界」，机器慢的可以调大。"));
    form->addRow(tr("进图前等待"), m_enterDelay);
    boxLayout->addLayout(form);

    layout->addWidget(m_behaviorBox);
}

void MapSettingsWidget::buildGlobalGroups(QWidget* parent, QVBoxLayout* layout)
{
    // ── 地图库目录 ──
    auto* libraryBox = new QGroupBox(tr("地图库"), parent);
    auto* libraryLayout = new QVBoxLayout(libraryBox);
    libraryLayout->setSpacing(6);

    auto* libraryRow = new QHBoxLayout();
    m_libraryDir = new QLineEdit(libraryBox);
    m_libraryDir->setPlaceholderText(tr("留空 = 启动器数据目录下的 maps"));
    m_libraryDir->setToolTip(tr("导入过的原始压缩包存在这里，重装地图要用；换成别的位置时旧包不会自动搬。"));
    auto* browseLibrary = new QPushButton(tr("浏览…"), libraryBox);
    libraryRow->addWidget(m_libraryDir, 1);
    libraryRow->addWidget(browseLibrary);
    libraryLayout->addLayout(libraryRow);
    connect(browseLibrary, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择地图库目录"), m_libraryDir->text());
        if (!dir.isEmpty())
            m_libraryDir->setText(dir);
    });
    layout->addWidget(libraryBox);

    // ── 大厅 ──
    auto* hallBox = new QGroupBox(tr("地图大厅"), parent);
    auto* hallLayout = new QVBoxLayout(hallBox);
    hallLayout->setSpacing(6);

    auto* hallForm = new QFormLayout();
    hallForm->setContentsMargins(0, 0, 0, 0);

    m_hallEndpoint = new QLineEdit(hallBox);
    m_hallEndpoint->setPlaceholderText(QStringLiteral("https://qtxjbukjyuylqjgiphxs.supabase.co"));
    m_hallEndpoint->setToolTip(tr("留空 = 官方地址；用反向代理时填基地址即可。"));
    hallForm->addRow(tr("数据地址"), m_hallEndpoint);

    /**
     * 传输方式。默认 auto：先走 Qt 网络（有代理时最快），一旦被判"连接被掐断"
     * 就自动改用本机浏览器的无头模式 —— 实测这条线路封的是 SNI，
     * 浏览器走 ECH 能过，Qt 的 TLS 栈过不去（见 BrowserFetch.h）。
     */
    m_hallTransport = new QComboBox(hallBox);
    m_hallTransport->addItem(tr("自动（先直连，被拦就改用本机浏览器）"), QStringLiteral("auto"));
    m_hallTransport->addItem(tr("只用网络（有可用代理时最稳）"), QStringLiteral("network"));
    m_hallTransport->addItem(tr("只用浏览器（启动 Edge/Chrome 取数据，较慢）"), QStringLiteral("browser"));
    hallForm->addRow(tr("通道"), m_hallTransport);

    // 大厅出口：auto 会先探活再用（环境变量里的代理常常是别的程序留下的死地址）
    m_hallProxyMode = new QComboBox(hallBox);
    m_hallProxyMode->addItem(tr("自动（环境变量 → 系统代理，先探活）"), QStringLiteral("auto"));
    m_hallProxyMode->addItem(tr("手动指定"), QStringLiteral("manual"));
    m_hallProxyMode->addItem(tr("始终直连"), QStringLiteral("off"));
    hallForm->addRow(tr("代理"), m_hallProxyMode);

    m_hallProxyUrl = new QLineEdit(hallBox);
    m_hallProxyUrl->setPlaceholderText(QStringLiteral("http://127.0.0.1:7890"));
    hallForm->addRow(tr("代理地址"), m_hallProxyUrl);
    hallLayout->addLayout(hallForm);

    // 「测试连接」：存配置、立刻打一次真查询，把耗时 / 结果 / 走的哪条出口摆出来
    m_hallApi = new HallAPI(this);
    auto* probeRow = new QHBoxLayout();
    m_probeButton = new QPushButton(tr("测试连接"), hallBox);
    m_probeResult = new QLabel(hallBox);
    m_probeResult->setWordWrap(true);
    m_probeResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    probeRow->addWidget(m_probeButton);
    probeRow->addWidget(m_probeResult, 1);
    hallLayout->addLayout(probeRow);
    layout->addWidget(hallBox);

    connect(m_probeButton, &QPushButton::clicked, this, [this] {
        // 先落盘再探测：探测的必须是"这套设置"，不是上一套
        saveSettings();
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
        m_probeResult->setStyleSheet(ok ? QStringLiteral("color: %1;").arg(ui::confidenceColor(Confidence::High).name())
                                        : QStringLiteral("color: %1;").arg(ui::noticeColorName()));
    });

    // ── 控制台页签 ──
    // 一个地图一个实例，世界/服务器/备注/其他日志这四页在启动器里基本用不上，默认收起。
    auto* tabsBox = new QGroupBox(tr("控制台页签（重开实例窗口后生效）"), parent);
    auto* tabsLayout = new QVBoxLayout(tabsBox);
    tabsLayout->setSpacing(6);
    m_showNotes = new QCheckBox(tr("显示「备注」页"), tabsBox);
    m_showWorlds = new QCheckBox(tr("显示「世界」页"), tabsBox);
    m_showServers = new QCheckBox(tr("显示「服务器」页"), tabsBox);
    m_showOtherLogs = new QCheckBox(tr("显示「其他日志」页"), tabsBox);
    for (auto* box : { m_showNotes, m_showWorlds, m_showServers, m_showOtherLogs })
        tabsLayout->addWidget(box);
    layout->addWidget(tabsBox);

    auto* hint = new QLabel(tr("地图库保存导入过的原始压缩包（重装要用）；下载页仍然交给系统浏览器。\n"
                               "大厅接口在部分网络下会被重置 —— 「通道 = 自动」会改用本机浏览器去取（不需要代理）。"),
                            parent);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);
}

void MapSettingsWidget::loadSettings()
{
    const bool overriding = m_instance == nullptr || m_settings->get("OverrideMapBehaviors").toBool();
    if (m_behaviorBox != nullptr && m_behaviorBox->isCheckable()) {
        // 勾选状态变化时让 Qt 自己管子控件的使能，别再手动 setEnabled
        m_behaviorBox->setChecked(overriding);
    }

    m_autoEnter->setChecked(m_settings->get("MapAutoEnter").toBool());
    m_safeExit->setChecked(m_settings->get("MapSafeExit").toBool());
    m_skipPrompt->setChecked(m_settings->get("MapSkipExperimentalPrompt").toBool());
    m_enterDelay->setValue(m_settings->get("MapEnterDelay").toInt());

    if (m_instance != nullptr)
        return;

    m_libraryDir->setText(m_settings->get("MapLibraryDir").toString());
    m_hallEndpoint->setText(m_settings->get("MapHallEndpoint").toString());
    m_hallTransport->setCurrentIndex(
        std::max(0, m_hallTransport->findData(m_settings->get("MapHallTransport").toString())));
    m_hallProxyMode->setCurrentIndex(
        std::max(0, m_hallProxyMode->findData(m_settings->get("MapHallProxyMode").toString())));
    m_hallProxyUrl->setText(m_settings->get("MapHallProxyUrl").toString());

    m_showNotes->setChecked(m_settings->get("MapConsoleShowNotes").toBool());
    m_showWorlds->setChecked(m_settings->get("MapConsoleShowWorlds").toBool());
    m_showServers->setChecked(m_settings->get("MapConsoleShowServers").toBool());
    m_showOtherLogs->setChecked(m_settings->get("MapConsoleShowOtherLogs").toBool());
}

void MapSettingsWidget::saveSettings()
{
    if (m_instance != nullptr) {
        // 未勾选 = 跟随全局：必须 reset，否则实例里会留下"看起来生效、其实没用"的旧值
        const bool overriding = m_behaviorBox->isChecked();
        m_settings->set("OverrideMapBehaviors", overriding);
        if (overriding) {
            m_settings->set("MapAutoEnter", m_autoEnter->isChecked());
            m_settings->set("MapSafeExit", m_safeExit->isChecked());
            m_settings->set("MapSkipExperimentalPrompt", m_skipPrompt->isChecked());
            m_settings->set("MapEnterDelay", m_enterDelay->value());
        } else {
            m_settings->reset("MapAutoEnter");
            m_settings->reset("MapSafeExit");
            m_settings->reset("MapSkipExperimentalPrompt");
            m_settings->reset("MapEnterDelay");
        }
        return;
    }

    m_settings->set("MapAutoEnter", m_autoEnter->isChecked());
    m_settings->set("MapSafeExit", m_safeExit->isChecked());
    m_settings->set("MapSkipExperimentalPrompt", m_skipPrompt->isChecked());
    m_settings->set("MapEnterDelay", m_enterDelay->value());
    m_settings->set("MapLibraryDir", m_libraryDir->text().trimmed());
    m_settings->set("MapHallEndpoint", m_hallEndpoint->text().trimmed());
    m_settings->set("MapHallTransport", m_hallTransport->currentData().toString());
    m_settings->set("MapHallProxyMode", m_hallProxyMode->currentData().toString());
    m_settings->set("MapHallProxyUrl", m_hallProxyUrl->text().trimmed());
    m_settings->set("MapConsoleShowNotes", m_showNotes->isChecked());
    m_settings->set("MapConsoleShowWorlds", m_showWorlds->isChecked());
    m_settings->set("MapConsoleShowServers", m_showServers->isChecked());
    m_settings->set("MapConsoleShowOtherLogs", m_showOtherLogs->isChecked());
    // 改完立刻生效：下一次请求重新探活
    invalidateHallProxyCache();
}

}  // namespace maps
