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
#include "ImportChoiceDialog.h"

#include <QApplication>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "StringUtils.h"
#include "archive/ArchiveReader.h"
#include "icons/IconList.h"
#include "maps/MapArchiveScanner.h"
#include "maps/MapTheme.h"

ImportChoiceDialog::ImportChoiceDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent), m_filePath(filePath)
{
    setWindowTitle(tr("选择导入方式"));
    setFixedWidth(480);
    setModal(true);

    const QIcon appIcon = QIcon::fromTheme(QStringLiteral("viewfolder"));
    if (!appIcon.isNull()) {
        setWindowIcon(appIcon);
    }

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(12);
    rootLayout->setContentsMargins(16, 16, 16, 16);

    // ── 顶部文件概要 ──────────────────────────────────────────────
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);

    auto* iconLabel = new QLabel(this);
    QIcon fileIcon = QIcon::fromTheme(QStringLiteral("archive"));
    if (fileIcon.isNull()) {
        fileIcon = QIcon::fromTheme(QStringLiteral("viewfolder"));
    }
    if (fileIcon.isNull() && APPLICATION && APPLICATION->icons()) {
        fileIcon = APPLICATION->icons()->getIcon(QStringLiteral("default"));
    }
    iconLabel->setPixmap(fileIcon.pixmap(32, 32));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* fileInfoLayout = new QVBoxLayout();
    fileInfoLayout->setSpacing(2);

    const QFileInfo fi(filePath);
    auto* titleLabel = new QLabel(fi.fileName(), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);
    fileInfoLayout->addWidget(titleLabel);

    auto* sizeLabel = new QLabel(tr("大小: %1").arg(StringUtils::humanReadableFileSize(static_cast<double>(fi.size()))), this);
    sizeLabel->setStyleSheet(QStringLiteral("color: %1;").arg(maps::ui::mutedTextColor().name()));
    fileInfoLayout->addWidget(sizeLabel);

    headerLayout->addLayout(fileInfoLayout, 1);
    rootLayout->addLayout(headerLayout);

    // ── 推荐提示卡片 ──────────────────────────────────────────────
    m_hintBox = new QFrame(this);
    m_hintBox->setObjectName(QStringLiteral("recBox"));
    m_hintBox->setStyleSheet(QStringLiteral(
        "QFrame#recBox { "
        "  background: rgba(127, 127, 127, 20); "
        "  border: 1px solid rgba(127, 127, 127, 55); "
        "  border-radius: 6px; "
        "}"
    ));
    auto* hintLayout = new QVBoxLayout(m_hintBox);
    hintLayout->setContentsMargins(12, 10, 12, 10);
    hintLayout->setSpacing(4);

    m_recTitleLabel = new QLabel(this);
    QFont recFont = m_recTitleLabel->font();
    recFont.setBold(true);
    m_recTitleLabel->setFont(recFont);
    hintLayout->addWidget(m_recTitleLabel);

    m_recDetailLabel = new QLabel(this);
    m_recDetailLabel->setStyleSheet(QStringLiteral("color: %1;").arg(maps::ui::mutedTextColor().name()));
    m_recDetailLabel->setWordWrap(true);
    hintLayout->addWidget(m_recDetailLabel);

    rootLayout->addWidget(m_hintBox);

    // ── 底部三个按钮（地图 / 整合 / 取消，与主界面原生按钮风格统一）────
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    m_mapBtn = new QPushButton(tr("作为地图导入"), this);
    m_mapBtn->setCursor(Qt::PointingHandCursor);
    m_mapBtn->setMinimumHeight(32);

    m_modpackBtn = new QPushButton(tr("作为整合导入"), this);
    m_modpackBtn->setCursor(Qt::PointingHandCursor);
    m_modpackBtn->setMinimumHeight(32);

    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setMinimumHeight(32);
    m_cancelBtn->setMinimumWidth(75);

    btnLayout->addWidget(m_mapBtn, 1);
    btnLayout->addWidget(m_modpackBtn, 1);
    btnLayout->addWidget(m_cancelBtn, 0);

    rootLayout->addLayout(btnLayout);

    connect(m_mapBtn, &QPushButton::clicked, this, &ImportChoiceDialog::onMapClicked);
    connect(m_modpackBtn, &QPushButton::clicked, this, &ImportChoiceDialog::onModpackClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    inspectFile();
}

void ImportChoiceDialog::inspectFile()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto scanned = maps::MapArchiveScanner::scan(m_filePath);
    QApplication::restoreOverrideCursor();

    const bool hasWorld = scanned && !scanned->worlds.isEmpty();

    bool hasModpackManifest = false;
    const QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix == QLatin1String("mrpack")) {
        hasModpackManifest = true;
    } else {
        MMCZip::ArchiveReader reader(m_filePath);
        (void)reader.parse([&hasModpackManifest](MMCZip::ArchiveReader::File* file) -> Result<bool> {
            const QString name = file->filename().toLower();
            if (name == QLatin1String("manifest.json") || name == QLatin1String("modrinth.index.json") ||
                name == QLatin1String("mmc-pack.json") || name == QLatin1String("instance.cfg") ||
                name.endsWith(QLatin1String("/manifest.json")) || name.endsWith(QLatin1String("/modrinth.index.json")) ||
                name.endsWith(QLatin1String("/mmc-pack.json")) || name.endsWith(QLatin1String("/instance.cfg"))) {
                hasModpackManifest = true;
                return true;  // 提前终止扫描
            }
            return false;
        });
    }

    const QString worldName = hasWorld
        ? (scanned->worlds[0].level.levelName.isEmpty() ? scanned->worlds[0].folderLabel : scanned->worlds[0].level.levelName)
        : QString();
    const QString ver = (hasWorld && !scanned->worlds[0].version.mcVersion.isEmpty() &&
                         scanned->worlds[0].version.mcVersion != QLatin1String("unknown"))
        ? scanned->worlds[0].version.mcVersion
        : tr("未知");

    QFont boldFont = font();
    boldFont.setBold(true);
    QFont normalFont = font();
    normalFont.setBold(false);

    if (hasWorld && hasModpackManifest) {
        m_recTitleLabel->setText(tr("检测到同时包含世界存档与整合配置"));
        m_recDetailLabel->setText(tr("包含世界「%1」 · 请选择对应的导入方式").arg(worldName));
        m_mapBtn->setFont(boldFont);
        m_mapBtn->setDefault(true);
        m_mapBtn->setFocus();
        m_modpackBtn->setFont(normalFont);
    } else if (hasWorld) {
        m_recTitleLabel->setText(tr("推荐：作为地图导入"));
        m_recDetailLabel->setText(tr("包含世界「%1」 · 推荐版本: %2").arg(worldName, ver));
        m_mapBtn->setFont(boldFont);
        m_mapBtn->setDefault(true);
        m_mapBtn->setFocus();
        m_modpackBtn->setFont(normalFont);
    } else if (hasModpackManifest) {
        m_recTitleLabel->setText(tr("推荐：作为整合导入"));
        m_recDetailLabel->setText(tr("包含整合配置文件（CurseForge / Modrinth / MultiMC 格式）"));
        m_modpackBtn->setFont(boldFont);
        m_modpackBtn->setDefault(true);
        m_modpackBtn->setFocus();
        m_mapBtn->setFont(normalFont);
    } else {
        m_recTitleLabel->setText(tr("未能明确判定文件特征"));
        m_recDetailLabel->setText(tr("请根据压缩包的实际用途选择导入方式"));
        m_mapBtn->setFont(normalFont);
        m_modpackBtn->setFont(normalFont);
    }
}

void ImportChoiceDialog::onMapClicked()
{
    m_choice = Choice::Map;
    accept();
}

void ImportChoiceDialog::onModpackClicked()
{
    m_choice = Choice::Modpack;
    accept();
}

ImportChoiceDialog::Choice ImportChoiceDialog::ask(const QString& filePath, QWidget* parent)
{
    ImportChoiceDialog dlg(filePath, parent);
    if (dlg.exec() == QDialog::Accepted) {
        return dlg.choice();
    }
    return Choice::Cancel;
}
