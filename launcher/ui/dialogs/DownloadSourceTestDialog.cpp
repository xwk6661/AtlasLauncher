// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas Launcher - Minecraft launcher fork with a map-first workflow
 *  Copyright (C) 2026 Atlas contributors
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

#include "DownloadSourceTestDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "net/ApiRequest.h"
#include "net/DownloadSource.h"

namespace {

/**
 * 探测清单。挑的都是"长期不会消失"的地址：
 * - 版本清单是固定路径；
 * - brigadier 1.3.10 是多年不变的老库；
 * - 资源对象按内容哈希寻址，落地即永久；
 * - Fabric Loader 的 maven 制品从不删除。
 *
 * 全部写**官方**地址：实际请求前会按当前选择过一次 DownloadSource，所以窗口里
 * 显示的地址就是"这次真正会打的地址"。
 */
struct Target {
    const char* label;
    const char* url;
};

constexpr Target TARGETS[] = {
    { QT_TRANSLATE_NOOP("DownloadSourceTestDialog", "版本清单"), "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json" },
    { QT_TRANSLATE_NOOP("DownloadSourceTestDialog", "游戏资源"),
      "https://resources.download.minecraft.net/b9/b9f15e2d6073ed2f28bea176dad96df73bc6200f" },
    { QT_TRANSLATE_NOOP("DownloadSourceTestDialog", "Mojang 库"),
      "https://libraries.minecraft.net/com/mojang/brigadier/1.3.10/brigadier-1.3.10.jar" },
    { QT_TRANSLATE_NOOP("DownloadSourceTestDialog", "Fabric 库"),
      "https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.20.1/fabric-loader-0.20.1.jar" },
};

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    return QObject::tr("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

}  // namespace

DownloadSourceTestDialog::DownloadSourceTestDialog(bool mirror, QWidget* parent) : QDialog(parent), m_mirror(mirror)
{
    setWindowTitle(tr("测试下载源连通性"));
    resize(620, 320);
    buildUi();
    startAll();
}

DownloadSourceTestDialog::~DownloadSourceTestDialog() = default;

void DownloadSourceTestDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("下面四条分别测一个真实文件的下载。选中镜像时，地址会显示成镜像上的地址 —— "
           "能同时确认「换源生效」和「这个源通不通」。"),
        this);
    intro->setWordWrap(true);
    intro->setEnabled(false);
    layout->addWidget(intro);

    auto* grid = new QVBoxLayout();
    grid->setSpacing(6);
    for (const auto& target : TARGETS) {
        Probe probe;
        probe.label = tr(target.label);
        probe.official = QUrl(QString::fromLatin1(target.url));
        probe.effective = Net::DownloadSource::rewriteFor(probe.official, m_mirror);

        auto* row = new QHBoxLayout();

        auto* name = new QLabel(probe.label, this);
        name->setMinimumWidth(90);
        name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        row->addWidget(name);

        auto* address = new QLabel(probe.effective.toString(), this);
        address->setTextInteractionFlags(Qt::TextSelectableByMouse);
        address->setToolTip(probe.effective.toString());
        address->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(address, 1);

        auto* status = new QLabel(tr("等待中…"), this);
        status->setMinimumWidth(150);
        status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(status);

        probe.address = address;
        probe.status = status;
        m_probes.append(probe);
        grid->addLayout(row);
    }
    layout->addLayout(grid);

    m_summary = new QLabel(QString(), this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_retry = buttons->addButton(tr("重新测试"), QDialogButtonBox::ActionRole);
    connect(m_retry, &QPushButton::clicked, this, &DownloadSourceTestDialog::startAll);
    connect(buttons, &QDialogButtonBox::rejected, this, &DownloadSourceTestDialog::reject);
    layout->addWidget(buttons);
}

void DownloadSourceTestDialog::startAll()
{
    if (m_current != nullptr) {
        m_current->abort();
        m_current.reset();
    }
    m_cancelled = false;
    ++m_generation;
    m_index = 0;
    m_summary->clear();
    for (auto& probe : m_probes) {
        probe.status->setText(tr("等待中…"));
        probe.status->setStyleSheet(QString());
    }
    if (m_retry != nullptr)
        m_retry->setEnabled(false);
    startNext();
}

void DownloadSourceTestDialog::startNext()
{
    if (m_cancelled)
        return;

    if (m_index >= m_probes.size()) {
        if (m_retry != nullptr)
            m_retry->setEnabled(true);
        if (m_summary->text().isEmpty())
            m_summary->setText(tr("全部通过。"));
        return;
    }

    auto& probe = m_probes[m_index];
    probe.status->setText(tr("测试中…"));
    probe.status->setStyleSheet(QString());

    // NoSourceRewrite：地址已经在这里算好了，别再让 Request 按**已保存**的设置改一遍
    // —— 否则"下拉框选官方、但设置里还是镜像"时会测错对象。
    auto [request, output] = Net::ApiRequest::makeByteArray(probe.effective, Net::Request::Option::NoSourceRewrite);
    m_current = request;
    m_timer.start();
    const int generation = m_generation;

    // 注意：不能在信号槽里直接 m_current.reset() —— 那会在对象自己发信号的过程中析构它。
    // 所以先把要用的数字取出来，再排队到下一轮事件循环去收尾。
    connect(request.get(), &Task::succeeded, this, [this, output, generation] {
        const qint64 elapsed = m_timer.elapsed();
        const int code = m_current != nullptr ? m_current->replyStatusCode() : -1;
        const qint64 bytes = output != nullptr ? output->size() : 0;
        QMetaObject::invokeMethod(
            this,
            [this, generation, code, elapsed, bytes] {
                if (generation != m_generation)
                    return;
                settle(true, tr("✓ %1 · %2 · %3").arg(code).arg(tr("%1 ms").arg(elapsed)).arg(humanSize(bytes)));
            },
            Qt::QueuedConnection);
    });
    connect(request.get(), &Task::failed, this, [this, generation](const QString& reason) {
        const QString text = reason.isEmpty() ? tr("失败") : reason;
        QMetaObject::invokeMethod(
            this,
            [this, generation, text] {
                if (generation != m_generation)
                    return;
                settle(false, tr("✗ %1").arg(text));
            },
            Qt::QueuedConnection);
    });

    request->start();
}

void DownloadSourceTestDialog::settle(bool ok, const QString& detail)
{
    if (m_index >= m_probes.size())
        return;

    auto& probe = m_probes[m_index];
    probe.status->setText(detail);
    probe.status->setStyleSheet(ok ? QStringLiteral("color: #2e7d32;") : QStringLiteral("color: #c62828;"));

    if (!ok) {
        // 一条失败就停：后面的多半也是同一个原因，继续跑只会让人等
        m_summary->setText(m_mirror ? tr("「%1」失败。可以再试一次，或者切回官方源对比。").arg(probe.label)
                                    : tr("「%1」失败。这与启动器无关，是这条线路打不通；可以试试镜像源。").arg(probe.label));
        m_summary->setStyleSheet(QStringLiteral("color: #c62828;"));
        if (m_retry != nullptr)
            m_retry->setEnabled(true);
        m_current.reset();
        return;
    }

    m_current.reset();
    ++m_index;
    startNext();
}

void DownloadSourceTestDialog::reject()
{
    m_cancelled = true;
    if (m_current != nullptr) {
        m_current->abort();
        m_current.reset();
    }
    QDialog::reject();
}
