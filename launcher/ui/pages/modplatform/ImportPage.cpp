// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
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

#include "ImportPage.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QMimeDatabase>
#include <QValidator>
#include <utility>

#include "Application.h"
#include "InstanceImportTask.h"
#include "Json.h"
#include "archive/ArchiveReader.h"
#include "maps/MapArchiveScanner.h"
#include "maps/MapImportDialog.h"
#include "maps/MapImportTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "net/NetJob.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui_ImportPage.h"

class UrlValidator : public QValidator {
   public:
    using QValidator::QValidator;

    State validate(QString& in, [[maybe_unused]] int& pos) const
    {
        const QUrl url(in);
        if (url.isValid() && !url.isRelative() && !url.isEmpty()) {
            return Acceptable;
        } else if (QFile::exists(in)) {
            return Acceptable;
        } else {
            return Intermediate;
        }
    }
};

ImportPage::ImportPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), ui(new Ui::ImportPage), dialog(dialog)
{
    ui->setupUi(this);
    setAcceptDrops(true);

    ui->modpackEdit->setValidator(new UrlValidator(ui->modpackEdit));
    connect(ui->modpackEdit, &QLineEdit::textChanged, this, &ImportPage::updateState);
    connect(ui->mapEdit, &QLineEdit::textChanged, this, &ImportPage::updateState);

    ui->modpackHintWidget->setStyleSheet(
        QStringLiteral("background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 4px; padding: 4px;"));
    ui->mapHintWidget->setStyleSheet(
        QStringLiteral("background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 4px; padding: 4px;"));
    ui->mapCardFrame->setStyleSheet(
        QStringLiteral("QFrame#mapCardFrame { background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 6px; padding: 8px; }"));
    ui->mapCardFrame->setVisible(false);
}

ImportPage::~ImportPage()
{
    delete ui;
}

bool ImportPage::shouldDisplay() const
{
    return true;
}

void ImportPage::retranslate()
{
    ui->retranslateUi(this);
}

void ImportPage::openedImpl()
{
    updateState();
}

void ImportPage::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ImportPage::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) {
        return;
    }
    const QString localPath = urls.first().toLocalFile();
    if (ui->radioMap->isChecked()) {
        ui->mapEdit->setText(localPath);
    } else {
        ui->modpackEdit->setText(localPath);
    }
    event->acceptProposedAction();
}

void ImportPage::on_radioModpack_toggled(bool checked)
{
    if (checked) {
        ui->stackedWidget->setCurrentIndex(0);
        updateState();
    }
}

void ImportPage::on_radioMap_toggled(bool checked)
{
    if (checked) {
        ui->stackedWidget->setCurrentIndex(1);
        updateState();
    }
}

void ImportPage::setMode(Mode mode)
{
    if (mode == Mode::Map) {
        ui->radioMap->setChecked(true);
    } else {
        ui->radioModpack->setChecked(true);
    }
}

void ImportPage::on_btnSwitchToMap_clicked()
{
    const QString path = ui->modpackEdit->text().trimmed();
    ui->radioMap->setChecked(true);
    ui->mapEdit->setText(path);
}

void ImportPage::on_btnSwitchToModpack_clicked()
{
    const QString path = ui->mapEdit->text().trimmed();
    ui->radioModpack->setChecked(true);
    ui->modpackEdit->setText(path);
}

void ImportPage::updateState()
{
    if (!isOpened) {
        return;
    }

    if (ui->radioMap->isChecked()) {
        updateMapState();
    } else {
        updateModpackState();
    }
}

void ImportPage::updateModpackState()
{
    if (ui->modpackEdit->hasAcceptableInput()) {
        QString input = ui->modpackEdit->text().trimmed();
        auto url = QUrl::fromUserInput(input);
        if (url.isLocalFile()) {
            QFileInfo fi(input);

            bool isZip = QMimeDatabase().mimeTypeForUrl(url).suffixes().contains("zip");
            bool isMRPack = fi.suffix().compare(QLatin1String("mrpack"), Qt::CaseInsensitive) == 0;

            if (fi.exists() && (isZip || isMRPack)) {
                if (isZip) {
                    const auto scanned = maps::MapArchiveScanner::scan(input);
                    if (scanned && !scanned->worlds.isEmpty()) {
                        const QString wName = scanned->worlds[0].level.levelName.isEmpty()
                                                  ? scanned->worlds[0].folderLabel
                                                  : scanned->worlds[0].level.levelName;
                        ui->modpackHintLabel->setText(tr("检测到该文件包含 Minecraft 地图存档（世界：%1）。建议切换为地图导入。").arg(wName));
                        ui->modpackHintWidget->setVisible(true);
                    } else {
                        ui->modpackHintWidget->setVisible(false);
                    }
                } else {
                    ui->modpackHintWidget->setVisible(false);
                }

                auto extra_info = QMap(m_extra_info);
                qDebug() << "Pack Extra Info" << extra_info << m_extra_info;
                dialog->setSuggestedPack(fi.completeBaseName(), new InstanceImportTask(url, false, this, std::move(extra_info)));
                dialog->setSuggestedIcon("default");
            } else {
                ui->modpackHintWidget->setVisible(false);
                dialog->setSuggestedPack();
            }
        } else if (url.scheme() == "curseforge") {
            ui->modpackHintWidget->setVisible(false);
            QUrlQuery query(url);
            if (query.allQueryItemValues("addonId").isEmpty() || query.allQueryItemValues("fileId").isEmpty()) {
                qDebug() << "Invalid curseforge link:" << url;
                dialog->setSuggestedPack();
                return;
            }
            auto addonId = query.allQueryItemValues("addonId")[0];
            auto fileId = query.allQueryItemValues("fileId")[0];

            auto [job, array] = FlameAPI::getFile(addonId, fileId);

            connect(job.get(), &NetJob::failed, this, [this](const QString& reason) {
                CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
            });
            connect(job.get(), &NetJob::succeeded, this, [this, array, addonId, fileId] {
                qDebug() << "Returned CFURL Json:\n" << array->toStdString().c_str();
                auto doc = Json::requireDocument(*array);
                if (!doc) {
                    CustomMessageBox::selectable(this, tr("Error"), doc.error(), QMessageBox::Critical)->show();
                    return;
                }
                auto data = doc->object()["data"].toObject();
                auto fileName = data["fileName"].toString();
                if (fileName.endsWith(".zip")) {
                    auto dl_url = QUrl(data["downloadUrl"].toString(""));
                    if (!dl_url.isValid()) {
                        CustomMessageBox::selectable(
                            this, tr("Error"),
                            tr("The modpack %1 is blocked for third-parties! Please download it manually.").arg(fileName),
                            QMessageBox::Critical)
                            ->show();
                        return;
                    }

                    QFileInfo dl_file(dl_url.fileName());
                    QString pack_name = data["displayName"].toString(dl_file.completeBaseName());

                    QMap<QString, QString> extra_info;
                    extra_info.insert("pack_id", addonId);
                    extra_info.insert("pack_version_id", fileId);

                    dialog->setSuggestedPack(pack_name, new InstanceImportTask(dl_url, false, this, std::move(extra_info)));
                    dialog->setSuggestedIcon("default");

                } else {
                    CustomMessageBox::selectable(this, tr("Error"), tr("This url isn't a valid modpack !"), QMessageBox::Critical)->show();
                }
            });
            ProgressDialog dlUrlDialod(this);
            dlUrlDialod.showSkipButton();
            dlUrlDialod.execWithTask(job.get());
            return;
        } else {
            ui->modpackHintWidget->setVisible(false);
            if (input.endsWith("?client=y")) {
                input.chop(9);
                input.append("/file");
                url = QUrl::fromUserInput(input);
            }
            QFileInfo fi(url.fileName());
            auto extra_info = QMap(m_extra_info);
            dialog->setSuggestedPack(fi.completeBaseName(), new InstanceImportTask(url, false, this, std::move(extra_info)));
            dialog->setSuggestedIcon("default");
        }
    } else {
        ui->modpackHintWidget->setVisible(false);
        dialog->setSuggestedPack();
    }
}

void ImportPage::updateMapState()
{
    const QString input = ui->mapEdit->text().trimmed();
    if (input.isEmpty() || !QFile::exists(input)) {
        m_currentMapScan.reset();
        m_hasCustomMapOptions = false;
        ui->mapHintWidget->setVisible(false);
        ui->mapCardFrame->setVisible(false);
        dialog->setSuggestedPack();
        return;
    }

    const QFileInfo fi(input);
    const QString localPath = fi.absoluteFilePath();

    if (!m_currentMapScan || m_currentMapScan->archivePath != localPath) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto scanned = maps::MapArchiveScanner::scan(localPath);
        QApplication::restoreOverrideCursor();
        if (scanned) {
            m_currentMapScan = *scanned;
        } else {
            m_currentMapScan.reset();
        }
        m_hasCustomMapOptions = false;
    }

    if (!m_currentMapScan || m_currentMapScan->worlds.isEmpty()) {
        ui->mapCardFrame->setVisible(true);
        ui->mapTitleLabel->setText(tr("❌ 未检测到有效的世界存档"));
        ui->mapNameLabel->setText(tr("未能从该文件中找到包含 level.dat 的 Minecraft 世界。"));
        ui->mapFolderLabel->setText(QStringLiteral("-"));
        ui->mapVersionLabel->setText(QStringLiteral("-"));
        ui->mapResourcesLabel->setText(QStringLiteral("-"));
        ui->btnMapOptions->setEnabled(false);

        // 探测是否可能是整合配置
        bool isModpack = fi.suffix().compare(QLatin1String("mrpack"), Qt::CaseInsensitive) == 0;
        if (!isModpack) {
            MMCZip::ArchiveReader reader(localPath);
            (void)reader.parse([&isModpack](MMCZip::ArchiveReader::File* file) -> Result<bool> {
                const QString name = file->filename().toLower();
                if (name == QLatin1String("manifest.json") || name == QLatin1String("modrinth.index.json") ||
                    name == QLatin1String("mmc-pack.json") || name == QLatin1String("instance.cfg") ||
                    name.endsWith(QLatin1String("/manifest.json")) || name.endsWith(QLatin1String("/modrinth.index.json")) ||
                    name.endsWith(QLatin1String("/mmc-pack.json")) || name.endsWith(QLatin1String("/instance.cfg"))) {
                    isModpack = true;
                    return true;
                }
                return false;
            });
        }
        if (isModpack) {
            ui->mapHintLabel->setText(tr("检测到该文件包含整合配置文件。建议切换为整合导入。"));
            ui->mapHintWidget->setVisible(true);
        } else {
            ui->mapHintWidget->setVisible(false);
        }

        dialog->setSuggestedPack();
        return;
    }

    ui->mapHintWidget->setVisible(false);
    ui->mapCardFrame->setVisible(true);
    ui->btnMapOptions->setEnabled(true);

    const auto& scan = *m_currentMapScan;
    const auto& primaryWorld = scan.worlds[0];

    if (!m_hasCustomMapOptions) {
        m_currentMapOptions = maps::MapImportOptions();
        m_currentMapOptions.displayName = primaryWorld.level.levelName.isEmpty() ? fi.completeBaseName() : primaryWorld.level.levelName;
        m_currentMapOptions.worldIndex = 0;
        m_currentMapOptions.deployBundledResources = true;
    }

    ui->mapTitleLabel->setText(tr("已就绪的地图信息"));
    ui->mapNameLabel->setText(m_currentMapOptions.displayName);
    ui->mapFolderLabel->setText(primaryWorld.folderLabel);

    QString ver = m_currentMapOptions.forcedVersion.isEmpty() ? primaryWorld.version.mcVersion : m_currentMapOptions.forcedVersion;
    if (ver.isEmpty() || ver == QLatin1String("unknown")) {
        ver = tr("未知");
    }
    ui->mapVersionLabel->setText(tr("%1 (置信度: %2)").arg(ver, maps::confidenceToString(primaryWorld.version.confidence)));

    QString resStr = tr("材质包: %1 个").arg(scan.resourcePacks().size());
    if (!scan.bundledMods().isEmpty()) {
        resStr += tr(" | 内置模组: %1 个").arg(scan.bundledMods().size());
    }
    ui->mapResourcesLabel->setText(resStr);

    dialog->setSuggestedPack(m_currentMapOptions.displayName, new maps::MapImportTask(localPath, scan, m_currentMapOptions));
    dialog->setSuggestedIcon("default");
}

void ImportPage::setUrl(const QString& url)
{
    ui->radioModpack->setChecked(true);
    ui->modpackEdit->setText(url);
    updateState();
}

void ImportPage::setMapFile(const QString& path)
{
    ui->radioMap->setChecked(true);
    ui->mapEdit->setText(path);
    updateState();
}

void ImportPage::setExtraInfo(const QMap<QString, QString>& extra_info)
{
    m_extra_info = extra_info;
    updateState();
}

void ImportPage::on_modpackBtn_clicked()
{
    const QMimeType zip = QMimeDatabase().mimeTypeForName("application/zip");
    auto filter = tr("Supported files") + QString(" (%1 *.mrpack)").arg(zip.globPatterns().join(" "));
    filter += ";;" + zip.filterString();
    filter += ";;" + tr("Modrinth pack") + " (*.mrpack)";
    const QUrl url = QFileDialog::getOpenFileUrl(this, tr("Choose modpack"), modpackUrl(), filter);
    if (url.isValid()) {
        if (url.isLocalFile()) {
            ui->modpackEdit->setText(url.toLocalFile());
        } else {
            ui->modpackEdit->setText(url.toString());
        }
    }
}

void ImportPage::on_mapBtn_clicked()
{
    const QString filter = tr("Minecraft 地图 (*.zip *.rar *.7z *.tar *.gz *.mcworld);;所有文件 (*)");
    const QString file = QFileDialog::getOpenFileName(this, tr("选择地图压缩包"), ui->mapEdit->text(), filter);
    if (!file.isEmpty()) {
        ui->mapEdit->setText(file);
    }
}

void ImportPage::on_btnMapOptions_clicked()
{
    if (!m_currentMapScan || m_currentMapScan->worlds.isEmpty()) {
        return;
    }

    const auto options = maps::MapImportDialog::ask(*m_currentMapScan, this);
    if (options) {
        m_currentMapOptions = *options;
        m_hasCustomMapOptions = true;
        updateMapState();
    }
}

QUrl ImportPage::modpackUrl() const
{
    const QUrl url(ui->modpackEdit->text());
    if (url.isValid() && !url.isRelative() && !url.host().isEmpty()) {
        return url;
    } else {
        return QUrl::fromLocalFile(ui->modpackEdit->text());
    }
}
