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
#include "MapImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

#include "DataVersionTable.h"
#include "MapTextUtils.h"
#include "MapTheme.h"
#include "MapVersionDetector.h"

namespace maps {

MapImportDialog::MapImportDialog(const MapScanResult& scan, QWidget* parent) : QDialog(parent), m_scan(scan)
{
    setWindowTitle(tr("导入地图"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    const QString stem = QFileInfo(scan.archivePath).completeBaseName();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setText(stem);
    form->addRow(tr("实例名称"), m_nameEdit);

    m_worldCombo = new QComboBox(this);
    // 单世界时不进表单布局；不显式隐藏的话它会以 (0,0) 露在对话框左上角
    m_worldCombo->setVisible(false);
    for (const auto& world : scan.worlds) {
        const QString label = world.level.levelName.isEmpty() ? world.folderLabel : world.level.levelName;
        m_worldCombo->addItem(QStringLiteral("%1（%2）").arg(label, world.folderLabel));
    }
    if (scan.worlds.size() > 1) {
        form->addRow(tr("选择世界"), m_worldCombo);
        m_worldCombo->setVisible(true);
    }

    m_versionCombo = new QComboBox(this);
    m_versionCombo->setEditable(true);
    const auto& table = DataVersionTable::instance();
    for (const auto& release : table.releases())
        m_versionCombo->addItem(release);
    m_loaderCombo = new QComboBox(this);
    m_loaderCombo->addItems({ QStringLiteral("vanilla"), QStringLiteral("forge"), QStringLiteral("fabric"),
                              QStringLiteral("neoforge"), QStringLiteral("quilt") });
    m_loaderVersionEdit = new QLineEdit(this);
    m_loaderVersionEdit->setPlaceholderText(tr("可留空（先用原版启动，之后再补加载器）"));

    form->addRow(tr("游戏版本"), m_versionCombo);
    form->addRow(tr("模组加载器"), m_loaderCombo);
    form->addRow(tr("加载器版本"), m_loaderVersionEdit);

    layout->addLayout(form);

    m_confidenceLabel = new QLabel(this);
    m_confidenceLabel->setWordWrap(true);
    layout->addWidget(m_confidenceLabel);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_evidenceList = new QListWidget(this);
    m_evidenceList->setSelectionMode(QAbstractItemView::NoSelection);
    m_evidenceList->setMaximumHeight(180);
    layout->addWidget(new QLabel(tr("判定依据"), this));
    layout->addWidget(m_evidenceList);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okDefaultText = m_okButton->text();
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 预填判定结果
    if (!scan.worlds.isEmpty()) {
        const auto& primary = scan.worlds.first();
        // 判不出来时不要把字面量 "unknown" 塞进下拉框（用户会以为它是个版本号）
        m_versionCombo->setCurrentText(primary.version.isKnown() ? primary.version.mcVersion : QString());
        m_loaderCombo->setCurrentText(primary.version.recommendedLoader);
        // levelName 里常带 § 格式化码（截图里的"§d..."），实例名不要带
        if (!primary.level.levelName.isEmpty())
            m_nameEdit->setText(text::stripFormattingCodes(primary.level.levelName));
    }

    connect(m_worldCombo, &QComboBox::currentIndexChanged, this, &MapImportDialog::worldChanged);
    connect(m_versionCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        m_versionTouched = true;
        refreshConfirmState();
        updateEvidence();
    });
    // 只有「用户真的在改名」才算改过；程序回填不算（否则切世界时名字就不会跟着变）
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString&) { m_nameTouched = true; });

    // 自动判定失败时，先在对话框里把「必须人工指定版本」说清楚
    if (!m_scan.worlds.isEmpty() && !m_scan.worlds.first().version.isKnown())
        m_versionCombo->setFocus();
    refreshConfirmState();

    updateEvidence();
}

QString MapImportDialog::effectiveVersion() const
{
    if (m_versionTouched)
        return m_versionCombo->currentText().trimmed();
    if (m_scan.worlds.isEmpty())
        return {};
    const int index = std::clamp(m_worldCombo->currentIndex(), 0, static_cast<int>(m_scan.worlds.size()) - 1);
    return m_scan.worlds.at(index).version.mcVersion;
}

void MapImportDialog::refreshConfirmState()
{
    if (m_okButton == nullptr)
        return;
    const bool launchable = MapVersionDetector::isLaunchableVersion(effectiveVersion());
    m_okButton->setEnabled(launchable);
    m_okButton->setText(launchable ? m_okDefaultText : tr("请先选择版本"));
}

void MapImportDialog::worldChanged()
{
    if (m_scan.worlds.isEmpty())
        return;

    const int index = std::clamp(m_worldCombo->currentIndex(), 0, static_cast<int>(m_scan.worlds.size()) - 1);
    const auto& world = m_scan.worlds.at(index);

    // 没被用户改过就跟着所选世界走：一包多世界时每个存档的版本可能不同，
    // 只预填第一个世界的版本会让「选第二个世界、却按第一个的版本建实例」成为可能。
    if (!m_versionTouched) {
        const QSignalBlocker blocker(m_versionCombo);
        m_versionCombo->setCurrentText(world.version.isKnown() ? world.version.mcVersion : QString());
    }
    {
        const QSignalBlocker blocker(m_loaderCombo);
        m_loaderCombo->setCurrentText(world.version.recommendedLoader);
    }
    if (!m_nameTouched && !world.level.levelName.isEmpty())
        m_nameEdit->setText(text::stripFormattingCodes(world.level.levelName));

    updateEvidence();
    refreshConfirmState();
}

void MapImportDialog::updateEvidence()
{
    const int index = std::clamp(m_worldCombo->currentIndex(), 0, std::max(0, static_cast<int>(m_scan.worlds.size()) - 1));
    if (m_scan.worlds.isEmpty())
        return;

    const auto& world = m_scan.worlds.at(index);
    const auto confidence = world.version.confidence;

    m_confidenceLabel->setText(tr("置信度：%1").arg(confidenceToString(confidence)));
    m_confidenceLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(ui::confidenceColor(confidence).name()));

    QStringList extra;
    if (!world.level.enabledFeatures.isEmpty()) {
        extra.append(tr("⚠ 存档带实验性设置：%1（游戏打开时会自己弹一次确认框）")
                         .arg(world.level.enabledFeatures.join(QStringLiteral(", "))));
    }
    // 判据只能是 Forge 写进 level.dat 的模组清单；作者用什么客户端保存过不算
    if (!world.level.requiredMods.isEmpty() && m_scan.bundledMods().isEmpty())
        extra.append(tr("⚠ 存档声明依赖 %1 个模组，但压缩包里没有 mods/ —— 需要自备对应模组")
                         .arg(world.level.requiredMods.size()));
    if (!extra.isEmpty())
        extra.append(QString());

    // 这段是富文本：世界名/模组 id 里的 & 和 < 必须转义，否则标签会被吃掉
    QStringList escaped = extra;
    escaped.append(world.version.warnings);
    for (auto& line : escaped)
        line = line.toHtmlEscaped();

    m_summaryLabel->setText(QStringLiteral("<p>%1</p><p>%2</p>")
                                .arg(escaped.join(QStringLiteral("<br/>")),
                                     tr("%1 个文件 · %2 MiB · 材质包 %3 · 模组 %4")
                                         .arg(world.fileCount)
                                         .arg(QString::number(world.sizeBytes / 1024.0 / 1024.0, 'f', 1))
                                         .arg(m_scan.resourcePacks().size())
                                         .arg(m_scan.bundledMods().size())));

    m_evidenceList->clear();
    for (const auto& item : world.version.evidence) {
        m_evidenceList->addItem(QStringLiteral("[%1] %2").arg(item.kind, item.detail));
    }
    if (m_evidenceList->count() == 0)
        m_evidenceList->addItem(tr("（没有可展示的证据）"));
}

MapImportOptions MapImportDialog::options() const
{
    MapImportOptions options;
    options.displayName = m_nameEdit->text().trimmed();
    options.worldIndex = m_worldCombo->currentIndex();
    options.forcedLoader = m_loaderCombo->currentText();
    options.loaderVersion = m_loaderVersionEdit->text().trimmed();

    // 用户改过版本才记 decidedBy=user；没改就沿用自动判定
    if (m_versionTouched)
        options.forcedVersion = m_versionCombo->currentText().trimmed();

    return options;
}

std::optional<MapImportOptions> MapImportDialog::ask(const MapScanResult& scan, QWidget* parent)
{
    MapImportDialog dialog(scan, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.options();
}

}  // namespace maps
