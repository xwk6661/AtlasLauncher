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
#include <QTimer>
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
 * - Fabric 的版本清单是固定路径，且镜像有对应的 /fabric-meta。
 *
 * 刻意避开 2 MB 级别的 jar：这个窗口是用来判断"通不通"的，不是测带宽。
 * 之前用 fabric-loader.jar 时，弱网下一条要等几十秒，看起来就像卡死。
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
    { QT_TRANSLATE_NOOP("DownloadSourceTestDialog", "Fabric 元数据"), "https://meta.fabricmc.net/v2/versions/loader" },
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
        probe.attempts = 0;
        probe.passed = false;
        probe.lastError.clear();
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
        finishAll();
        return;
    }

    auto& probe = m_probes[m_index];
    ++probe.attempts;
    probe.status->setStyleSheet(QString());
    probe.status->setText(probe.attempts > 1 ? tr("重试中（第 %1 次）…").arg(probe.attempts - 1) : tr("测试中…"));

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
        // 状态码比 errorString 有用得多：镜像的典型失败是「302 之后节点断开」，
        // 光看 "Connection closed" 分不出是被墙还是节点挂了。
        const int code = m_current != nullptr ? m_current->replyStatusCode() : -1;
        const QString text = code > 0 ? tr("%1 %2").arg(code).arg(reason.isEmpty() ? tr("失败") : reason)
                                      : (reason.isEmpty() ? tr("失败") : reason);
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
    m_current.reset();

    if (ok) {
        probe.passed = true;
        probe.status->setText(detail);
        probe.status->setStyleSheet(QStringLiteral("color: #2e7d32;"));
        ++m_index;
        startNext();
        return;
    }

    probe.lastError = detail;

    // 节点抖动就再来一次 —— BMCLAPI 每次请求 302 到不同节点，重试几乎总能绕开
    if (probe.attempts < MAX_ATTEMPTS) {
        probe.status->setText(tr("换一个节点重试…"));
        probe.status->setStyleSheet(QStringLiteral("color: #ef6c00;"));
        QTimer::singleShot(400, this, [this] {
            if (!m_cancelled)
                startNext();
        });
        return;
    }

    probe.status->setText(detail);
    probe.status->setStyleSheet(QStringLiteral("color: #c62828;"));
    // 一条失败不影响后面的：否则剩下的行会一直停在"等待中"，看起来像整个功能挂了
    ++m_index;
    startNext();
}

void DownloadSourceTestDialog::finishAll()
{
    if (m_retry != nullptr)
        m_retry->setEnabled(true);

    int passed = 0;
    QStringList failed;
    for (const auto& probe : m_probes) {
        if (probe.passed)
            ++passed;
        else
            failed.append(probe.label);
    }

    const int total = m_probes.size();
    if (passed == total) {
        m_summary->setText(m_mirror ? tr("%1/%2 全部通过：这台机器到镜像的链路是通的。").arg(passed).arg(total)
                                    : tr("%1/%2 全部通过：这台机器到官方服务器的链路是通的。").arg(passed).arg(total));
        m_summary->setStyleSheet(QStringLiteral("color: #2e7d32;"));
        return;
    }

    m_summary->setStyleSheet(QStringLiteral("color: #c62828;"));
    const QString who = failed.join(QStringLiteral("、"));
    m_summary->setText(
        m_mirror
            ? tr("%1/%2 通过；失败的：%3。镜像的节点是随机的，个别节点不可达很常见 —— 点「重新测试」多半能过；"
                 "如果一直失败，把源切回官方试试。")
                  .arg(passed)
                  .arg(total)
                  .arg(who)
            : tr("%1/%2 通过；失败的：%3。这说明是这条线路的问题，不是启动器设置错了 —— 可以换到镜像源。")
                  .arg(passed)
                  .arg(total)
                  .arg(who));
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
