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
#include "OptiFineDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "MapRepository.h"
#include "MapTheme.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

namespace maps {
namespace {

/// BMCLAPI 的 OptiFine 接口（列表 + 下载都是它）
constexpr const char* BMCL_BASE = "https://bmclapi2.bangbang93.com";
/// 官网两条链路：adloadx 是广告页（里面才有 downloadx 链接），downloadx 才是 jar
constexpr const char* OPTIFINE_HOME = "https://optifine.net";

/// `C5` / `D1` / `I6`：字母越靠后越新，同字母比数字（照 AIO 的排序）
int patchKey(const QString& type, const QString& patch)
{
    Q_UNUSED(type)
    const auto match = QRegularExpression(QStringLiteral("^([A-Za-z])(\\d+)$")).match(patch);
    if (!match.hasMatch())
        return 0;
    return match.captured(1).at(0).toUpper().toLatin1() * 1000 + match.captured(2).toInt();
}

QString humanError(QNetworkReply* reply)
{
    const QString detail = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return status > 0 ? QStringLiteral("%1（HTTP %2）").arg(detail).arg(status) : detail;
}

}  // namespace

OptiFineDialog::OptiFineDialog(MinecraftInstance* instance, QWidget* parent) : QDialog(parent), m_instance(instance)
{
    setWindowTitle(tr("安装 OptiFine"));
    setMinimumWidth(560);

    m_nam = APPLICATION->network();

    auto* layout = new QVBoxLayout(this);

    m_loaderHint = new QLabel(this);
    m_loaderHint->setWordWrap(true);
    // 可选中：玩家常要把版本号发给别人
    m_loaderHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_loaderHint);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("OptiFine 版本"), this));
    m_buildCombo = new QComboBox(this);
    m_buildCombo->setMinimumWidth(240);
    row->addWidget(m_buildCombo, 1);
    m_reloadButton = new QPushButton(tr("重新获取"), this);
    row->addWidget(m_reloadButton);
    layout->addLayout(row);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    auto* buttons = new QDialogButtonBox(this);
    m_installButton = buttons->addButton(tr("下载并安装到 mods/"), QDialogButtonBox::AcceptRole);
    m_closeButton = buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(m_reloadButton, &QPushButton::clicked, this, [this] {
        m_buildCombo->clear();
        fetchList();
    });
    connect(m_installButton, &QPushButton::clicked, this, &OptiFineDialog::startInstall);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    resolveInstance();
    fetchList();
}

QString OptiFineDialog::modsDir() const
{
    return m_instance != nullptr ? FS::PathCombine(m_instance->instanceRoot(), QStringLiteral("mods")) : QString();
}

void OptiFineDialog::resolveInstance()
{
    if (m_instance == nullptr || m_instance->getPackProfile() == nullptr) {
        m_loaderHint->setText(tr("这个实例没有版本信息，装不了 OptiFine。"));
        m_installButton->setEnabled(false);
        return;
    }

    m_mcVersion = m_instance->getPackProfile()->getComponentVersion(QStringLiteral("net.minecraft"));
    // 加载器取实例实际组件（与右侧卡片同一套判定），不看 map.json
    m_loaderName = MapRepository::describe(m_instance).loaderName;

    if (m_mcVersion.isEmpty()) {
        m_loaderHint->setText(tr("读不出游戏版本，先到「版本」页把 Minecraft 组件加回来。"));
        m_installButton->setEnabled(false);
        return;
    }

    if (m_loaderName == QLatin1String("Forge") || m_loaderName == QLatin1String("NeoForge")) {
        m_loaderHint->setText(tr("目标：Minecraft %1 · %2。OptiFine 会作为模组放进 mods/，"
                                 "启动一次后游戏里就有 OptiFine 的选项了。")
                                  .arg(m_mcVersion, m_loaderName));
    } else if (m_loaderName == QLatin1String("Fabric") || m_loaderName == QLatin1String("Quilt")) {
        m_loaderHint->setText(tr("目标：Minecraft %1 · %2。OptiFine 本身不是 Fabric 模组，"
                                 "下完还要在「模组」页装 OptiFabric 才能被加载。")
                                  .arg(m_mcVersion, m_loaderName));
    } else {
        m_loaderHint->setText(tr("目标：Minecraft %1 · 原版。原版不认 mods/ 里的 OptiFine —— "
                                 "先在「版本」页装 Forge（或 Fabric + OptiFabric），否则装了也不会生效。")
                                  .arg(m_mcVersion));
    }
}

void OptiFineDialog::fetchList(int attempt)
{
    if (m_mcVersion.isEmpty())
        return;

    setBusy(true);
    setStatus(tr("正在获取 OptiFine %1 的版本列表…").arg(m_mcVersion));

    const QUrl url(QStringLiteral("%1/optifine/%2").arg(QString::fromLatin1(BMCL_BASE), m_mcVersion));
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);

    auto* reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
        onListReply(reply, attempt);
        reply->deleteLater();
    });
}

void OptiFineDialog::onListReply(QNetworkReply* reply, int attempt)
{
    // 变量名不能叫 data：QWidget 有个 data 成员，MSVC 会报 C4458
    const auto payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        // 网络错误不能伪装成"没有这个版本"（照 AIO 的处理），重试一次再报
        if (attempt < 2) {
            setStatus(tr("拉取版本列表失败（%1），重试一次…").arg(humanError(reply)));
            QTimer::singleShot(800, this, [this, attempt] { fetchList(attempt + 1); });
            return;
        }
        setBusy(false);
        setStatus(tr("拉取 OptiFine 版本列表失败：%1\n\n这个列表来自 BMCLAPI，通常在国内可直连。"
                     "如果一直失败，可以直接去 optifine.net 下载 jar，再手动放进 mods/。")
                      .arg(humanError(reply)),
                  true);
        return;
    }

    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        setBusy(false);
        setStatus(tr("OptiFine 版本列表的返回格式看不懂（不是 JSON 数组），站点可能改了接口。"), true);
        return;
    }

    m_builds.clear();
    for (const auto& value : document.array()) {
        const auto object = value.toObject();
        Build build;
        build.type = object.value(QStringLiteral("type")).toString();
        build.patch = object.value(QStringLiteral("patch")).toString();
        build.filename = object.value(QStringLiteral("filename")).toString();
        if (build.filename.isEmpty())
            continue;
        // 预览版（`preview_...`）的 type 里还嵌着版本号（HD_U_J1 + pre10），
        // 既排不了序也没法拼下载地址 —— 稳定版已经够用，直接滤掉
        if (build.filename.startsWith(QLatin1String("preview_")))
            continue;
        m_builds.append(build);
    }

    if (m_builds.isEmpty()) {
        setBusy(false);
        setStatus(tr("OptiFine 没有 %1 的版本（这个版本可能太新或太旧）。").arg(m_mcVersion), true);
        return;
    }

    std::sort(m_builds.begin(), m_builds.end(), [](const Build& a, const Build& b) {
        return patchKey(a.type, a.patch) > patchKey(b.type, b.patch);
    });

    m_buildCombo->clear();
    for (const auto& build : m_builds)
        m_buildCombo->addItem(QStringLiteral("%1  ·  %2_%3").arg(build.filename, build.type, build.patch));

    setBusy(false);
    setStatus(tr("共 %1 个版本，默认选最新的 %2_%3。")
                  .arg(m_builds.size())
                  .arg(m_builds.first().type, m_builds.first().patch));
}

void OptiFineDialog::startInstall()
{
    const int index = m_buildCombo->currentIndex();
    if (index < 0 || index >= m_builds.size())
        return;

    m_pending = m_builds.at(index);
    m_payload.clear();

    // 原版 / Fabric 也允许下载：玩家可能要自己配 OptiFabric，或者只是想把 jar 存着
    if (m_loaderName != QLatin1String("Forge") && m_loaderName != QLatin1String("NeoForge")) {
        auto* box = new QMessageBox(this);
        box->setWindowTitle(tr("当前不是 Forge 实例"));
        box->setIcon(QMessageBox::Warning);
        box->setText(tr("当前实例是 %1，OptiFine 作为模组只有 Forge 会加载。").arg(m_loaderName));
        box->setInformativeText(tr("仍然可以下载并放进 mods/：\n"
                                   "· Fabric / Quilt：再装一个 OptiFabric，它会把 OptiFine 接进 Fabric；\n"
                                   "· 原版：装了也不会生效，建议先到「版本」页装 Forge。\n\n继续吗？"));
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->setDefaultButton(QMessageBox::No);
        const bool go = box->exec() == QMessageBox::Yes;
        box->deleteLater();
        if (!go)
            return;
    }

    setBusy(true);
    m_progress->setVisible(true);
    m_progress->setValue(0);
    trySource(0, 1);
}

void OptiFineDialog::trySource(int source, int attempt)
{
    m_payload.clear();
    m_progress->setValue(0);

    if (source == 0) {
        setStatus(tr("第 %1/2 次尝试：BMCLAPI 下载 %2…").arg(attempt).arg(m_pending.filename));
        const QUrl url(QStringLiteral("%1/optifine/%2/%3/%4")
                           .arg(QString::fromLatin1(BMCL_BASE), m_mcVersion, m_pending.type, m_pending.patch));
        QNetworkRequest request(url);
        // 90 秒：BMCLAPI 会 302 到一个常常连不上的下载节点（实测 sakura.sld.tw 直接 reset），
        // 5 分钟的默认值会让"失败"看起来像"卡住"
        request.setTransferTimeout(90000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        auto* reply = m_nam->get(request);
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
            if (total > 0)
                m_progress->setValue(static_cast<int>(done * 100 / total));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, source, attempt] {
            onDownloadReply(reply, source, attempt);
            reply->deleteLater();
        });
        return;
    }

    // 官网第一步：从广告页里抠 downloadx 链接
    // （2026-11-01 实测：BMCLAPI 的 302 目标在本机连不上，官网 7.3 MB 约几秒就下完）
    setStatus(tr("BMCLAPI 下载节点连不上（常见：302 到一个不通的节点），改走 OptiFine 官网…"));
    const QUrl adUrl(QStringLiteral("%1/adloadx?f=%2")
                         .arg(QString::fromLatin1(OPTIFINE_HOME), QString::fromLatin1(QUrl::toPercentEncoding(m_pending.filename))));
    QNetworkRequest request(adUrl);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
        onAdPage(reply, attempt);
        reply->deleteLater();
    });
}

void OptiFineDialog::onAdPage(QNetworkReply* reply, int attempt)
{
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("官网页面打不开：%1").arg(humanError(reply)));
        return;
    }

    const QString html = QString::fromUtf8(reply->readAll());
    const QRegularExpression pattern(QStringLiteral("downloadx\\?f=[^'\"\\s]+"));
    const auto match = pattern.match(html);
    if (!match.hasMatch()) {
        fail(tr("官网页面里找不到下载链接（可能改版了，或需要过人机验证）。\n"
                "可以手动打开 https://optifine.net/downloads 下载 %1，再放进 mods/。")
                 .arg(m_pending.filename));
        return;
    }

    QString link = match.captured(0);
    link.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    const QUrl jarUrl(QStringLiteral("%1/%2").arg(QString::fromLatin1(OPTIFINE_HOME), link));

    setStatus(tr("官网：正在下载 %1…").arg(m_pending.filename));
    QNetworkRequest request(jarUrl);
    // 不带 Referer 官网会回 403 —— AIO 在这上面踩过
    request.setRawHeader("Referer", QStringLiteral("%1/adloadx?f=%2")
                                        .arg(QString::fromLatin1(OPTIFINE_HOME), m_pending.filename)
                                        .toUtf8());
    request.setTransferTimeout(300000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* jarReply = m_nam->get(request);
    connect(jarReply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (total > 0)
            m_progress->setValue(static_cast<int>(done * 100 / total));
    });
    connect(jarReply, &QNetworkReply::finished, this, [this, jarReply, attempt] {
        onDownloadReply(jarReply, 1, attempt);
        jarReply->deleteLater();
    });
}

void OptiFineDialog::onDownloadReply(QNetworkReply* reply, int source, int attempt)
{
    if (reply->error() == QNetworkReply::NoError) {
        m_payload = reply->readAll();
        if (m_payload.size() < 10 * 1024) {
            // 几 MB 的 jar 不可能这么小：多半是错误页 / 空文件
            const int size = m_payload.size();
            m_payload.clear();
            reply->setProperty("failure", tr("下载到的文件只有 %1 字节，明显不是 jar").arg(size));
        }
    }

    if (m_payload.isEmpty()) {
        const QString why = reply->error() != QNetworkReply::NoError
                                ? humanError(reply)
                                : reply->property("failure").toString();
        const QString sourceName = source == 0 ? tr("BMCLAPI") : tr("官网");
        if (attempt < 2) {
            setStatus(tr("%1 第 %2 次失败（%3），重试…").arg(sourceName).arg(attempt).arg(why));
            QTimer::singleShot(800, this, [this, source, attempt] { trySource(source, attempt + 1); });
            return;
        }
        if (source == 0) {
            trySource(1, 1);
            return;
        }
        fail(tr("两个源都下不到 %1。\n\n官网：%2\n\n"
                "提示：OptiFine 官网在国内常常能直连（BMCLAPI 的下载节点反而常连不上）。"
                "如果两边都不通，换一条网络（例如手机热点）再试，或手动下载后放进 mods/。")
                 .arg(m_pending.filename, why));
        return;
    }

    // 落盘：先写 .part 再改名，避免中断留下半个 jar 被 Forge 当成坏模组
    const QString dir = modsDir();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        fail(tr("建不出 mods 目录：%1").arg(dir));
        return;
    }
    const QString target = FS::PathCombine(dir, m_pending.filename);
    const QString temp = target + QStringLiteral(".part");

    QFile file(temp);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(tr("写不进 %1：%2").arg(temp, file.errorString()));
        return;
    }
    const qint64 written = file.write(m_payload);
    file.close();
    if (written != m_payload.size()) {
        QFile::remove(temp);
        fail(tr("写入不完整（%1 / %2 字节），已删除半成品。").arg(written).arg(m_payload.size()));
        return;
    }
    QFile::remove(target);
    if (!QFile::rename(temp, target)) {
        QFile::remove(temp);
        fail(tr("改名失败：%1").arg(target));
        return;
    }

    finishOk(m_pending.filename, source == 0 ? QStringLiteral("BMCLAPI") : tr("官网"));
}

void OptiFineDialog::finishOk(const QString& file, const QString& via)
{
    setBusy(false);
    m_progress->setValue(100);
    setStatus(tr("✓ %1 已放进 mods/（来源：%2）。\n启动游戏后 OptiFine 的视频设置会出现在「选项 → 视频设置」里。")
                  .arg(file, via));
}

void OptiFineDialog::fail(const QString& message)
{
    setBusy(false);
    setStatus(message, true);
}

void OptiFineDialog::setBusy(bool busy)
{
    m_installButton->setEnabled(!busy && !m_builds.isEmpty());
    m_reloadButton->setEnabled(!busy);
    m_buildCombo->setEnabled(!busy);
    if (!busy)
        m_progress->setVisible(false);
}

void OptiFineDialog::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    // 颜色跟主题走：深色底上写死 #c62828 几乎看不见
    m_status->setStyleSheet(error ? QStringLiteral("color: %1;").arg(ui::noticeColorName()) : QString());
}

}  // namespace maps
