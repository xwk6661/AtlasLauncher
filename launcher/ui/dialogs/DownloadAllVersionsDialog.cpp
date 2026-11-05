// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "DownloadAllVersionsDialog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "ResourcePaths.h"
#include "RuntimeContext.h"
#include "SysInfo.h"
#include "minecraft/AssetsUtils.h"
#include "minecraft/MojangVersionFormat.h"
#include "net/ApiRequest.h"
#include "net/ChecksumValidator.h"

namespace {

const char* kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

auto humanBytes(qint64 bytes) -> QString
{
    if (bytes >= qint64(1) << 30)
        return QStringLiteral("%1 GiB").arg(double(bytes) / (1 << 30), 0, 'f', 2);
    if (bytes >= qint64(1) << 20)
        return QStringLiteral("%1 MiB").arg(double(bytes) / (1 << 20), 0, 'f', 1);
    return QStringLiteral("%1 KiB").arg(double(bytes) / 1024, 0, 'f', 0);
}

}  // namespace

DownloadAllVersionsDialog::DownloadAllVersionsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("下载全部正式版资源（调试）"));
    resize(860, 600);

    auto* root = new QVBoxLayout(this);

    auto* blurb = new QLabel(tr("把清单里所有正式版的公共资源下载到当前数据目录：客户端 jar / 库 / 素材索引 / 素材对象。\n"
                                "素材按 hash 共享、库按 metacache 去重 —— 已经有的不会重下，所以这个按钮可以反复点。"),
                             this);
    blurb->setWordWrap(true);
    root->addWidget(blurb);

    m_currentLabel = new QLabel(tr("未开始"), this);
    root->addWidget(m_currentLabel);

    m_overall = new QProgressBar(this);
    m_overall->setFormat(tr("版本 %v / %m"));
    root->addWidget(m_overall);

    m_step = new QProgressBar(this);
    m_step->setFormat(tr("当前步骤 %v / %m"));
    root->addWidget(m_step);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(50000);
    root->addWidget(m_logView, 1);

    auto* buttons = new QHBoxLayout();
    m_startButton = new QPushButton(tr("开始下载"), this);
    m_stopButton = new QPushButton(tr("停止"), this);
    m_stopButton->setEnabled(false);
    auto* closeButton = new QPushButton(tr("关闭"), this);
    buttons->addWidget(m_startButton);
    buttons->addWidget(m_stopButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(m_startButton, &QPushButton::clicked, this, &DownloadAllVersionsDialog::start);
    connect(m_stopButton, &QPushButton::clicked, this, [this] {
        m_cancelled = true;
        m_stopButton->setEnabled(false);
        appendLog(tr("已请求停止：取消当前任务…"));
        if (m_job)
            m_job->abort();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    // 无人值守入口：PRISM_DEBUG_DOWNLOAD_ALL=1 打开窗口即自动开始。
    // 只是为了在没有鼠标的环境里压测，正常调试（点按钮）不受影响。
    if (qEnvironmentVariableIsSet("PRISM_DEBUG_DOWNLOAD_ALL"))
        QTimer::singleShot(200, this, &DownloadAllVersionsDialog::start);
}

void DownloadAllVersionsDialog::start()
{
    if (m_running)
        return;

    m_running = true;
    m_cancelled = false;
    m_versions.clear();
    m_seenIndexes.clear();
    m_index = 0;
    m_ok = 0;
    m_bad = 0;
    m_requests = 0;
    m_bytes = 0;
    m_report.clear();
    m_runStartMs = QDateTime::currentMSecsSinceEpoch();

    m_logView->clear();
    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_overall->setRange(0, 0);
    m_step->setRange(0, 0);

    const QString root = resource::assetsDir();
    appendLog(tr("数据目录：%1").arg(QDir::currentPath()));
    appendLog(tr("资源目录：assets=%1").arg(root));
    appendLog(tr("           libraries=%1").arg(resource::librariesDir()));
    appendLog(tr("拉取版本清单…"));

    stepManifest();
}

void DownloadAllVersionsDialog::stepManifest()
{
    auto [request, output] = Net::ApiRequest::makeByteArray(QUrl(kManifestUrl));
    auto job = makeShared<NetJob>(tr("Version manifest"), APPLICATION->network());
    job->addNetAction(request);
    m_requests++;

    runJob(
        job,
        [this, output] {
            QJsonParseError parseError{};
            const auto doc = QJsonDocument::fromJson(*output, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                appendLog(tr("清单解析失败：%1").arg(parseError.errorString()));
                finishRun(false);
                return;
            }

            const auto versions = doc.object().value("versions").toArray();
            for (const auto& value : versions) {
                const auto object = value.toObject();
                if (object.value("type").toString() != QLatin1String("release"))
                    continue;
                m_versions.append({ object.value("id").toString(), QUrl(object.value("url").toString()), object.value("sha1").toString() });
            }
            if (m_versions.isEmpty()) {
                appendLog(tr("清单里没有正式版，结束。"));
                finishRun(false);
                return;
            }

            // 只跑指定版本（排查单个版本的下载问题时很有用），例如 PRISM_DEBUG_DOWNLOAD_ONLY=1.16.5
            const QString only = qEnvironmentVariable("PRISM_DEBUG_DOWNLOAD_ONLY");
            if (!only.isEmpty()) {
                QList<VersionEntry> picked;
                for (const auto& entry : m_versions) {
                    if (entry.id == only)
                        picked.append(entry);
                }
                if (picked.isEmpty()) {
                    appendLog(tr("清单里没有版本 %1").arg(only));
                    finishRun(false);
                    return;
                }
                m_versions = picked;
            }

            // 压测时可以用 PRISM_DEBUG_DOWNLOAD_LIMIT 只跑前 N 个，省得每次都全量。
            const int limit = qEnvironmentVariableIntValue("PRISM_DEBUG_DOWNLOAD_LIMIT");
            if (limit > 0 && limit < m_versions.size())
                m_versions = m_versions.mid(0, limit);

            appendLog(tr("正式版 %1 个：%2 … %3").arg(m_versions.size()).arg(m_versions.first().id, m_versions.last().id));
            m_overall->setRange(0, m_versions.size());
            m_overall->setValue(0);
            stepVersionJson();
        },
        [this](const QString& why) {
            appendLog(tr("清单下载失败：%1").arg(why));
            finishRun(false);
        });
}

void DownloadAllVersionsDialog::stepVersionJson()
{
    if (m_cancelled) {
        finishRun(true);
        return;
    }
    if (m_index >= m_versions.size()) {
        finishRun(false);
        return;
    }

    const auto& version = currentVersion();
    m_versionStartMs = QDateTime::currentMSecsSinceEpoch();
    m_overall->setValue(m_index);
    m_currentLabel->setText(tr("当前版本 %1（%2 / %3）").arg(version.id).arg(m_index + 1).arg(m_versions.size()));

    const QString path = versionJsonPath(version.id);
    if (QFileInfo(path).isFile() && QFileInfo(path).size() > 0) {
        stepVersionPayload();
        return;
    }

    FS::ensureFilePathExists(path);
    auto request = Net::ApiRequest::makeFile(version.url, path);
    if (!version.sha1.isEmpty())
        request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, version.sha1));

    auto job = makeShared<NetJob>(tr("Version json %1").arg(version.id), APPLICATION->network());
    job->addNetAction(request);
    m_requests++;

    runJob(
        job, [this] { stepVersionPayload(); },
        [this](const QString& why) {
            appendLog(tr("%1：版本 json 失败（%2）").arg(currentVersion().id, why));
            m_report.append(tr("%1\tjson-failed\t%2").arg(currentVersion().id, why));
            m_bad++;
            advance();
        });
}

void DownloadAllVersionsDialog::stepVersionPayload()
{
    const auto& version = currentVersion();
    const QString jsonPath = versionJsonPath(version.id);

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(tr("%1：读不了版本 json").arg(version.id));
        m_bad++;
        advance();
        return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        appendLog(tr("%1：版本 json 不是对象").arg(version.id));
        m_bad++;
        advance();
        return;
    }

    const auto raw = doc.object();
    auto parsed = MojangVersionFormat::versionFileFromJson(doc, jsonPath);
    if (!parsed) {
        appendLog(tr("%1：解析失败（%2）").arg(version.id, parsed.error()));
        m_bad++;
        advance();
        return;
    }

    auto job = makeShared<NetJob>(tr("Payload %1").arg(version.id), APPLICATION->network());

    int clientNeeded = 0;
    if (const auto client = raw.value("downloads").toObject().value("client").toObject(); !client.isEmpty()) {
        const QString clientPath = FS::PathCombine(resource::librariesDir(), "com/mojang/minecraft", version.id,
                                                   QStringLiteral("minecraft-%1-client.jar").arg(version.id));
        const qint64 expectedSize = qint64(client.value("size").toDouble());
        const QFileInfo clientInfo(clientPath);
        if (!clientInfo.isFile() || (expectedSize > 0 && clientInfo.size() != expectedSize)) {
            FS::ensureFilePathExists(clientPath);
            auto request = Net::ApiRequest::makeFile(QUrl(client.value("url").toString()), clientPath);
            const auto sha1 = client.value("sha1").toString();
            if (!sha1.isEmpty())
                request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, sha1));
            job->addNetAction(request);
            clientNeeded = 1;
        }
    }

    // 库：走和真实启动完全相同的 Library::getDownloads（metacache 判定，已有的直接跳过）
    RuntimeContext context;
    context.javaArchitecture = QStringLiteral("64");
    context.javaRealArchitecture = QStringLiteral("x86_64");
    QStringList missingLocal;
    int libsNeeded = 0;
    for (const auto& library : parsed.value()->libraries) {
        if (!library)
            continue;
        // 必须按规则过滤：Mojang 的 libraries 里同时列了 Windows / Linux / macOS 的 natives，
        // 不过滤就会把这三个平台全下一遍（第一次跑就踩到了，见 06 §35）。
        if (!library->isActive(context))
            continue;
        const auto downloads = library->getDownloads(context, APPLICATION->metacache(), missingLocal, resource::librariesDir());
        for (const auto& download : downloads) {
            job->addNetAction(download);
            libsNeeded++;
        }
    }

    int indexNeeded = 0;
    QString assetsId;
    if (const auto index = raw.value("assetIndex").toObject(); !index.isEmpty()) {
        assetsId = index.value("id").toString();
        const QString indexPath = FS::PathCombine(resource::assetsDir(), "indexes", assetsId + ".json");
        const bool already = m_seenIndexes.contains(assetsId);
        if (!already || !QFileInfo(indexPath).isFile()) {
            FS::ensureFilePathExists(indexPath);
            auto request = Net::ApiRequest::makeFile(QUrl(index.value("url").toString()), indexPath);
            const auto sha1 = index.value("sha1").toString();
            if (!sha1.isEmpty())
                request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, sha1));
            job->addNetAction(request);
            m_seenIndexes.insert(assetsId);
            indexNeeded = 1;
        }
    }

    m_requests += clientNeeded + libsNeeded + indexNeeded;
    appendLog(tr("%1：需要 %2 项（client %3 / 库 %4 / 索引 %5）")
                  .arg(version.id)
                  .arg(clientNeeded + libsNeeded + indexNeeded)
                  .arg(clientNeeded)
                  .arg(libsNeeded)
                  .arg(indexNeeded));

    if (job->size() == 0) {
        stepAssets();
        return;
    }

    runJob(
        job, [this] { stepAssets(); },
        [this, clientNeeded, libsNeeded, indexNeeded](const QString& why) {
            appendLog(tr("%1：下载失败（%2）").arg(currentVersion().id, why));
            m_report.append(tr("%1\tpayload-failed\t%2\tclient=%3 libs=%4 index=%5")
                                .arg(currentVersion().id, why)
                                .arg(clientNeeded)
                                .arg(libsNeeded)
                                .arg(indexNeeded));
            m_bad++;
            advance();
        });
}

void DownloadAllVersionsDialog::stepAssets()
{
    const auto& version = currentVersion();
    const QString jsonPath = versionJsonPath(version.id);

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_bad++;
        advance();
        return;
    }
    const auto raw = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const QString assetsId = raw.value("assetIndex").toObject().value("id").toString();
    if (assetsId.isEmpty()) {
        appendLog(tr("%1：没有 assetIndex，跳过素材").arg(version.id));
        m_ok++;
        advance();
        return;
    }

    AssetsIndex index;
    const QString indexPath = FS::PathCombine(resource::assetsDir(), "indexes", assetsId + ".json");
    if (!AssetsUtils::loadAssetsIndexJson(assetsId, indexPath, index)) {
        appendLog(tr("%1：素材索引 %2 读不出来").arg(version.id, assetsId));
        m_report.append(tr("%1\tindex-unreadable\t%2").arg(version.id, assetsId));
        m_bad++;
        advance();
        return;
    }

    auto job = index.getDownloadJob();
    const int objectTotal = index.objects.size();
    if (!job) {
        appendLog(tr("%1：素材 %2 全部命中（%3 个对象，0 个请求）").arg(version.id, assetsId).arg(objectTotal));
        m_report.append(tr("%1\tassets-hit\t%2\tobjects=%3 requests=0").arg(version.id, assetsId).arg(objectTotal));
        m_ok++;
        advance();
        return;
    }

    const int objectNeeded = job->size();
    m_requests += objectNeeded;
    appendLog(tr("%1：素材索引 %2，%3 个对象里缺 %4 个").arg(version.id, assetsId).arg(objectTotal).arg(objectNeeded));

    runJob(
        job,
        [this, assetsId, objectTotal, objectNeeded] {
            appendLog(tr("%1：完成，用时 %2 秒").arg(currentVersion().id).arg(currentVersionSeconds(), 0, 'f', 1));
            m_report.append(tr("%1\tok\t%2\tobjects=%3 missing=%4\tseconds=%5")
                                .arg(currentVersion().id, assetsId)
                                .arg(objectTotal)
                                .arg(objectNeeded)
                                .arg(currentVersionSeconds(), 0, 'f', 1));
            m_ok++;
            advance();
        },
        [this, assetsId, objectNeeded](const QString& why) {
            appendLog(tr("%1：素材下载失败（%2）").arg(currentVersion().id, why));
            m_report.append(tr("%1\tassets-failed\t%2\tmissing=%3\t%4").arg(currentVersion().id, assetsId).arg(objectNeeded).arg(why));
            m_bad++;
            advance();
        });
}

void DownloadAllVersionsDialog::advance()
{
    m_index++;
    m_overall->setValue(m_index);
    stepVersionJson();
}

void DownloadAllVersionsDialog::runJob(NetJob::Ptr job, Callback onSucceeded, std::function<void(const QString&)> onFailed)
{
    if (!job || job->size() == 0) {
        if (onSucceeded)
            onSucceeded();
        return;
    }

    job->setAskRetry(false);
    m_job = job;

    m_jobBytes = 0;
    connect(job.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        m_step->setRange(0, int(qMax<qint64>(total, 1)));
        m_step->setValue(int(current));
        if (total > m_jobBytes)
            m_jobBytes = total;
    });

    // 一律延后一步再继续：现在还在这个任务自己的信号里，直接析构它（或立刻换上下一个任务）
    // 是我们在分片那段踩过的坑（06 §33.3）。
    connect(job.get(), &Task::succeeded, this, [this, onSucceeded] {
        QTimer::singleShot(0, this, [this, onSucceeded] {
            m_bytes += m_jobBytes;
            m_jobBytes = 0;
            clearJob();
            if (onSucceeded)
                onSucceeded();
        });
    });
    connect(job.get(), &Task::failed, this, [this, onFailed](const QString& why) {
        QTimer::singleShot(0, this, [this, onFailed, why] {
            clearJob();
            if (onFailed)
                onFailed(why);
        });
    });
    connect(job.get(), &Task::aborted, this, [this] {
        QTimer::singleShot(0, this, [this] {
            clearJob();
            m_cancelled = true;
            finishRun(true);
        });
    });

    job->start();
}

void DownloadAllVersionsDialog::clearJob()
{
    m_job = nullptr;
    m_step->setRange(0, 0);
}

void DownloadAllVersionsDialog::finishRun(bool cancelled)
{
    if (!m_running)
        return;
    m_running = false;
    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_runStartMs;
    const double seconds = qMax<double>(elapsedMs / 1000.0, 0.001);

    const QString summary = tr("%1：成功 %2 / 失败 %3，共 %4 个版本；发出 %5 个请求，%6，%7 秒")
                                .arg(cancelled ? tr("已停止") : tr("完成"))
                                .arg(m_ok)
                                .arg(m_bad)
                                .arg(m_versions.size())
                                .arg(m_requests)
                                .arg(humanBytes(m_bytes))
                                .arg(seconds, 0, 'f', 1);
    appendLog(summary);
    m_currentLabel->setText(summary);

    // 落一份报告，方便事后对日志（qInfo 也会进 logs/Atlas-*.log）
    QDir().mkpath(QStringLiteral("logs"));
    QFile report(QStringLiteral("logs/resources-all-versions.txt"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const auto stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        report.write(QStringLiteral("# %1\n# %2\n# requests=%3 bytes=%4\n").arg(stamp, summary).arg(m_requests).arg(m_bytes).toUtf8());
        for (const auto& line : m_report)
            report.write(line.toUtf8() + '\n');
        report.close();
        appendLog(tr("报告：logs/resources-all-versions.txt"));
    }
}

void DownloadAllVersionsDialog::appendLog(const QString& line)
{
    m_logView->appendPlainText(line);
    qInfo().noquote() << "[ResourceTest]" << line;
}

auto DownloadAllVersionsDialog::versionJsonPath(const QString& id) const -> QString
{
    return FS::PathCombine(QStringLiteral("meta/net.minecraft"), id + QStringLiteral(".json"));
}

auto DownloadAllVersionsDialog::currentVersionSeconds() const -> double
{
    if (m_versionStartMs == 0)
        return 0.0;
    return (QDateTime::currentMSecsSinceEpoch() - m_versionStartMs) / 1000.0;
}
