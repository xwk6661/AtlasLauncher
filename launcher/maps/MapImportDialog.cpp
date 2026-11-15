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

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "DataVersionTable.h"
#include "HallMatcher.h"
#include "MapTextUtils.h"
#include "MapTheme.h"
#include "MapStyle.h"
#include "MapVersionDetector.h"

namespace maps {

MapImportDialog::MapImportDialog(const MapScanResult& scan, QWidget* parent) : QDialog(parent), m_scan(scan)
{
    setWindowTitle(tr("导入地图"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::kSpaceL, ui::kSpaceL, ui::kSpaceL, ui::kSpaceL);
    layout->setSpacing(ui::kSpaceM);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setVerticalSpacing(ui::kSpaceS);

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

    // ── 这张图是：关联 CTM 地图 + 语言 ──
    // 同一张图的汉化版常叫「X（简中汉化v1.0）」而原版叫「X」，字符串相等只能配上少数几组；
    // 所以自动匹配只给**建议与理由**，写不写进索引由这里的人工确认决定。
    auto* identityBox = new QGroupBox(tr("这张图是"), this);
    identityBox->setToolTip(tr("关联到 CTM Maps 上的那张图，并标明这是原版还是某个语言的翻译。\n"
                               "关联之后，同一张图的各个语言版本会归到一组，改名、换机器都不会散。"));
    auto* identityForm = new QFormLayout(identityBox);
    identityForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    identityForm->setVerticalSpacing(ui::kSpaceS);

    const QString archiveName = QFileInfo(scan.archivePath).fileName();
    m_inferred = variant::inferFromFileName(archiveName);

    m_hallCombo = new QComboBox(identityBox);
    m_hallCombo->addItem(tr("（不关联）"), QString());

    HallMatchQuery query;
    query.fileName = archiveName;
    if (!scan.worlds.isEmpty() && scan.worlds.first().version.isKnown())
        query.mcVersion = scan.worlds.first().version.mcVersion;

    const auto candidates = HallMatcher::suggest(query, 3);
    for (const auto& candidate : candidates) {
        m_hallCombo->addItem(tr("%1 · %2（%3）").arg(candidate.name, candidate.reason).arg(candidate.score, 0, 'f', 2),
                             candidate.slug);
        m_hallCombo->setItemData(m_hallCombo->count() - 1,
                                 tr("站上名称：%1\nMC 版本：%2\nslug：%3")
                                     .arg(candidate.name, candidate.version, candidate.slug),
                                 Qt::ToolTipRole);
    }
    // 只有"一枝独秀"时才预选：第一名够高、且明显领先第二名。否则留空让人自己挑。
    if (candidates.size() >= 1 && candidates.first().score >= 0.95 &&
        (candidates.size() == 1 || candidates.first().score - candidates.at(1).score >= 0.1))
        m_hallCombo->setCurrentIndex(1);

    m_languageCombo = new QComboBox(identityBox);
    m_languageCombo->addItem(tr("原版（未标注语言）"));
    m_languageCombo->addItem(tr("原版（英文）"));
    m_languageCombo->addItem(tr("简中汉化"));
    m_languageCombo->addItem(tr("繁中汉化"));
    m_languageCombo->addItem(tr("官中（作者发布）"));
    m_languageCombo->addItem(tr("其他语言"));

    m_sourceCombo = new QComboBox(identityBox);
    m_sourceCombo->addItem(tr("来源未知"));
    m_sourceCombo->addItem(tr("作者发布"));
    m_sourceCombo->addItem(tr("社区汉化"));
    m_sourceCombo->addItem(tr("自译"));

    // 默认值来自文件名（只认字面证据，判不出来就是"原版（未标注）"）
    if (m_inferred.kind == variant::Kind::Localized) {
        if (m_inferred.language == variant::Language::TraditionalChinese)
            m_languageCombo->setCurrentIndex(3);
        else if (m_inferred.source == variant::Source::Author)
            m_languageCombo->setCurrentIndex(4);
        else
            m_languageCombo->setCurrentIndex(2);
        m_sourceCombo->setCurrentIndex(int(m_inferred.source));
    } else if (m_inferred.language == variant::Language::English) {
        m_languageCombo->setCurrentIndex(1);
    }

    identityForm->addRow(tr("关联地图"), m_hallCombo);
    identityForm->addRow(tr("语言"), m_languageCombo);
    identityForm->addRow(tr("来源"), m_sourceCombo);

    auto* hallHint = new QLabel(tr("候选来自本地大厅缓存；没匹配到不代表站上没有 —— 导入后还能在实例「地图」页关联。"),
                                identityBox);
    hallHint->setWordWrap(true);
    hallHint->setFont(ui::metaFont(hallHint->font()));
    hallHint->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    identityForm->addRow(hallHint);

    layout->addWidget(identityBox);
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, &MapImportDialog::languageChanged);
    languageChanged();

    buildGameOptionsGroup(layout);

    m_confidenceLabel = new QLabel(this);
    m_confidenceLabel->setWordWrap(true);
    m_confidenceLabel->setFont(ui::cardTitleFont(m_confidenceLabel->font()));
    layout->addWidget(m_confidenceLabel);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(QStringLiteral("color: %1;").arg(ui::warningColorName()));
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setFont(ui::metaFont(m_summaryLabel->font()));
    m_summaryLabel->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    layout->addWidget(m_summaryLabel);

    m_evidenceList = new QListWidget(this);
    m_evidenceList->setSelectionMode(QAbstractItemView::NoSelection);
    m_evidenceList->setMaximumHeight(180);
    m_evidenceList->setFrameShape(QFrame::NoFrame);
    layout->addWidget(ui::makeSeparator(this));
    auto* evidenceTitle = new QLabel(tr("判定依据"), this);
    evidenceTitle->setStyleSheet(ui::sectionHeaderStyle());
    layout->addWidget(evidenceTitle);
    layout->addWidget(m_evidenceList);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okDefaultText = m_okButton->text();
    ui::applyPrimaryButton(m_okButton);
    ui::applySecondaryButton(buttons->button(QDialogButtonBox::Cancel));
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
        m_baseName = m_nameEdit->text();
    } else {
        m_baseName = m_nameEdit->text();
    }
    // 名字定下来之后再套一次语言后缀：同一张图的原版与汉化版实例名必须能区分
    languageChanged();

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

    m_confidenceLabel->setText(tr("置信度：%1").arg(confidenceLabel(confidence)));
    m_confidenceLabel->setToolTip(tr("判定结果原值：%1。\n不确定就手动选一个版本；判不出来时不会允许导入。")
                                      .arg(confidenceToString(confidence)));
    m_confidenceLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(ui::confidenceColor(confidence).name()));

    // 警告与统计分开：警告是「导入前必须知道」，统计是「这包有多大」，
    // 原来两段挤在一个富文本里，没警告时还会多出一个空段落。
    QStringList warnings;
    if (!world.level.enabledFeatures.isEmpty()) {
        warnings.append(tr("⚠ 存档带实验性设置：%1（游戏打开时会自己弹一次确认框）")
                            .arg(world.level.enabledFeatures.join(QStringLiteral(", "))));
    }
    // 判据只能是 Forge 写进 level.dat 的模组清单；作者用什么客户端保存过不算
    if (!world.level.requiredMods.isEmpty() && m_scan.bundledMods().isEmpty()) {
        warnings.append(tr("⚠ 存档声明依赖 %1 个模组，但压缩包里没有 mods/ —— 需要自备对应模组")
                            .arg(world.level.requiredMods.size()));
    }
    warnings.append(world.version.warnings);

    QStringList escapedWarnings = warnings;
    for (auto& line : escapedWarnings)
        line = line.toHtmlEscaped();
    m_warningLabel->setText(escapedWarnings.join(QStringLiteral("<br/>")));
    m_warningLabel->setVisible(!escapedWarnings.isEmpty());

    m_summaryLabel->setText(tr("%1 个文件 · %2 MiB · 材质包 %3 · 模组 %4")
                                .arg(QLocale().toString(static_cast<qlonglong>(world.fileCount)))
                                .arg(QString::number(world.sizeBytes / 1024.0 / 1024.0, 'f', 1))
                                .arg(m_scan.resourcePacks().size())
                                .arg(m_scan.bundledMods().size()));

    m_evidenceList->clear();
    for (const auto& item : world.version.evidence) {
        m_evidenceList->addItem(QStringLiteral("[%1] %2").arg(item.kind, item.detail));
    }
    if (m_evidenceList->count() == 0)
        m_evidenceList->addItem(tr("（没有可展示的证据）"));
}

void MapImportDialog::languageChanged()
{
    if (m_languageCombo == nullptr || m_sourceCombo == nullptr)
        return;

    const int slot = m_languageCombo->currentIndex();
    m_sourceCombo->setEnabled(slot >= 2);
    // 官中 = 语言是简中、但来源必然是作者；别让玩家把它改成"自译"
    if (slot == 4) {
        m_sourceCombo->setCurrentIndex(int(variant::Source::Author));
        m_sourceCombo->setEnabled(false);
    }

    // 实例名跟着语言走：两张"石之恐惧"放在实例列表里根本分不出谁是谁
    if (!m_nameTouched && !m_baseName.isEmpty()) {
        QString suffix;
        switch (slot) {
            case 2:
                suffix = QStringLiteral("（简中汉化）");
                break;
            case 3:
                suffix = QStringLiteral("（繁中汉化）");
                break;
            case 4:
                suffix = QStringLiteral("（官中）");
                break;
            default:
                break;
        }
        m_nameEdit->setText(m_baseName + suffix);
    }
}

variant::Info MapImportDialog::variantFromUi() const
{
    variant::Info info;
    const int slot = m_languageCombo->currentIndex();

    switch (slot) {
        case 0:
            info.kind = variant::Kind::Original;
            info.language = variant::Language::Unknown;
            break;
        case 1:
            info.kind = variant::Kind::Original;
            info.language = variant::Language::English;
            break;
        case 2:
            info.kind = variant::Kind::Localized;
            info.language = variant::Language::SimplifiedChinese;
            break;
        case 3:
            info.kind = variant::Kind::Localized;
            info.language = variant::Language::TraditionalChinese;
            break;
        case 4:
            info.kind = variant::Kind::Localized;
            info.language = variant::Language::SimplifiedChinese;
            info.source = variant::Source::Author;
            break;
        default:
            info.kind = variant::Kind::Localized;
            info.language = variant::Language::Other;
            break;
    }

    // 资源包形态不走这条链路（它没有世界，得挂到已装实例上），这里一律按整包记
    info.delivery = variant::Delivery::Archive;

    if (info.kind == variant::Kind::Localized) {
        if (slot != 4)
            info.source = variant::Source(m_sourceCombo->currentIndex());
        // 保留文件名括号里的原话；用户改成别的语言时丢掉旧 label，免得写着"简中"其实是繁中
        if (m_inferred.kind == variant::Kind::Localized && m_inferred.language == info.language)
            info.label = m_inferred.label;
        if (info.label.isEmpty())
            info.label = variant::languageLabel(info.language);
    } else {
        info.source = variant::Source::Unknown;
    }

    return info;
}

void MapImportDialog::buildGameOptionsGroup(QVBoxLayout* layout)
{
    /**
     * 「导入时的游戏设置」。
     *
     * 这些东西只在**新建实例**时写一次，之后玩家在游戏里怎么改都不会被覆盖 ——
     * 所以决定点必须在"看得见这次导入"的地方（就是这里），而不是设置页里提前替
     * 未来某次导入做主。
     *
     * 标签一律写成「项目：取值」，短到一眼能扫；解释全部放 tooltip。
     * 每个取值都由 options::applyDefaults 按版本落地，版本对不上的项直接跳过
     * （见 MapOptionsTxt.h 的 supportsAutoJump / cloudsKey）。
     */
    auto* box = new QGroupBox(tr("导入时的游戏设置"), this);
    box->setToolTip(tr("这些值只在新建实例时写一次 options.txt；之后在游戏里怎么改都不会被覆盖。\n"
                       "每一项都会按地图版本调整写法，版本对不上的项自动跳过。"));
    auto* grid = new QGridLayout(box);
    grid->setHorizontalSpacing(ui::kSpaceL);
    grid->setVerticalSpacing(ui::kSpaceS);
    grid->setContentsMargins(ui::kSpaceM, ui::kSpaceM, ui::kSpaceM, ui::kSpaceM);

    options::Defaults saved;
    if (const auto* app = APPLICATION)
        saved = options::defaultsFromSettings(app->settings());

    auto make = [box](const QString& label, const QString& tip, bool checked) {
        auto* check = new QCheckBox(label, box);
        check->setToolTip(tip);
        check->setChecked(checked);
        return check;
    };

    m_optChinese = make(tr("语言：简体中文"),
                        tr("把游戏语言写成简体中文（options.txt 的 lang）。\n"
                           "1.13 起是 zh_cn，1.12 及更早是 zh_CN —— 按地图版本自动选。"),
                        saved.setChinese);
    m_optSprintSneak = make(tr("键位：Shift 跑 / Ctrl 蹲"),
                            tr("MC 默认是 Ctrl 奔跑、Shift 蹲下，这一项直接对调。\n"
                               "1.12 及更早写数字键码（29 / 42），1.13 起写 key.keyboard.*。\n"
                               "版本判不出来时整组跳过 —— 写错一代会让蹲/跑静默回默认。"),
                            saved.swapSprintSneak);
    m_optFullscreen = make(tr("全屏进入"),
                           tr("写 fullscreen:true。\n\n"
                              "地图是「一张图就是一个世界」，全屏是它的正常形态。\n"
                              "进游戏后按 F11 随时能切回窗口。"),
                           saved.setFullscreen);
    m_optClouds = make(tr("云：关闭"),
                       tr("关掉云。1.8 起写 renderClouds，1.7 及更早写 clouds。\n"
                          "1.19 起游戏自己写带引号的 \"false\"，这里跟着文件的方言走。\n"
                          "纯观感 / 性能项，不影响存档。"),
                       saved.disableClouds);
    m_optAutoJump = make(tr("自动跳远：关闭"),
                         tr("关掉 autoJump（1.8 起才有这个键，更早的版本自动跳过）。\n"
                            "自动跳远会在你走到方块边缘时替你起跳 —— 在跑酷、压力板、\n"
                            "需要精确落点的地图里常常帮倒忙。"),
                         saved.disableAutoJump);
    m_optPerspective = make(tr("切换视角：V"),
                            tr("把「切换视角」从 F5 改到 V。1.12 及更早写 47（LWJGL2 的 V），\n"
                               "1.13 起写 key.keyboard.v。\n"
                               "F5 常被截图 / 录屏 / 显卡工具占用，改成 V 更少撞键。"),
                            saved.perspectiveOnV);
    m_optViewDistance = make(tr("视野：12 区块"),
                             tr("写 renderDistance:12。\n"
                                "老版本上限 16、新版本 32，超出会被游戏忽略，所以统一夹在 2–32。\n"
                                "大图 + 远视野很吃内存，12 是画质与帧数的折中。"),
                             saved.renderDistance > 0);
    m_optBobView = make(tr("视角摇晃：关闭"),
                        tr("关掉 bobView（走路时画面上下晃动）。\n"
                           "容易晕 3D 的玩家建议关；对存档没有任何影响。"),
                        saved.disableViewBobbing);
    m_optGamma = make(tr("亮度：最亮"),
                      tr("写 gamma:1.0（亮度拉满）。\n"
                         "CTM / RPG 图里常有很暗的洞窟，默认亮度下看不清细节。\n"
                         "不改游戏难度，只是把显示亮度调高。"),
                      saved.brightestGamma);

    grid->addWidget(m_optChinese, 0, 0);
    grid->addWidget(m_optSprintSneak, 0, 1);
    grid->addWidget(m_optFullscreen, 1, 0);
    grid->addWidget(m_optClouds, 1, 1);
    grid->addWidget(m_optAutoJump, 2, 0);
    grid->addWidget(m_optPerspective, 2, 1);
    grid->addWidget(m_optViewDistance, 3, 0);
    grid->addWidget(m_optBobView, 3, 1);
    grid->addWidget(m_optGamma, 4, 0);

    auto* hint = new QLabel(tr("只写这一次；改过的项会记住，下次导入自动带上。"), box);
    hint->setWordWrap(true);
    hint->setFont(ui::metaFont(hint->font()));
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    grid->addWidget(hint, 5, 0, 1, 2);

    layout->addWidget(box);
}

options::Defaults MapImportDialog::gameOptionsFromUi() const
{
    options::Defaults defaults;
    defaults.setChinese = m_optChinese->isChecked();
    defaults.swapSprintSneak = m_optSprintSneak->isChecked();
    defaults.setFullscreen = m_optFullscreen->isChecked();
    defaults.fullscreen = true;
    defaults.disableClouds = m_optClouds->isChecked();
    defaults.disableAutoJump = m_optAutoJump->isChecked();
    defaults.perspectiveOnV = m_optPerspective->isChecked();
    defaults.renderDistance = m_optViewDistance->isChecked() ? 12 : 0;
    defaults.disableViewBobbing = m_optBobView->isChecked();
    defaults.brightestGamma = m_optGamma->isChecked();
    return defaults;
}

MapImportOptions MapImportDialog::options() const
{
    MapImportOptions options;
    options.displayName = m_nameEdit->text().trimmed();
    options.worldIndex = m_worldCombo->currentIndex();
    options.forcedLoader = m_loaderCombo->currentText();
    options.loaderVersion = m_loaderVersionEdit->text().trimmed();
    options.hallSlug = m_hallCombo->currentData().toString();
    options.variant = variantFromUi();
    options.gameOptions = gameOptionsFromUi();
    options.hasGameOptions = true;

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
    const auto options = dialog.options();
    // 记住这次的选择：下一次导入对话框按它预填（"默认值"就是上次的决定）
    if (auto* app = APPLICATION)
        options::saveDefaultsToSettings(app->settings(), options.gameOptions);
    return options;
}

}  // namespace maps
