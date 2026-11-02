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
#include "MapResourcePage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "ResourcePaths.h"
#include "ResourceStore.h"
#include "settings/SettingsObject.h"

namespace maps {
namespace {

QString describeState(const QString& name)
{
    const QString dir = resource::dirFor(name);
    const QString where = QDir::toNativeSeparators(dir);
    if (!QFileInfo::exists(dir))
        return QObject::tr("未创建（%1）").arg(where);
    return QObject::tr("%1（%2）").arg(QFileInfo(dir).isDir() ? QObject::tr("已就绪") : QObject::tr("不是目录"), where);
}

/// 「本机还有哪些目录可以复用」：勾选一个就填进路径框
class CandidateDialog : public QDialog {
   public:
    explicit CandidateDialog(const QList<ResourceStore::Candidate>& candidates, QWidget* parent) : QDialog(parent)
    {
        setWindowTitle(tr("检测到的资源目录"));
        setMinimumWidth(620);

        auto* layout = new QVBoxLayout(this);
        auto* hint = new QLabel(tr("下面这些目录里已经有 Minecraft 的资源，可以直接指过去复用（不会复制、不会改动原文件）。"), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        m_tree = new QTreeWidget(this);
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->setHeaderLabels({ tr("目录"), tr("assets"), tr("libraries"), tr("java") });
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < 4; ++i)
            m_tree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

        for (const auto& candidate : candidates) {
            auto* item = new QTreeWidgetItem(m_tree);
            item->setText(0, QDir::toNativeSeparators(candidate.path));
            item->setText(1, candidate.assets ? tr("有") : QStringLiteral("—"));
            item->setText(2, candidate.libraries ? tr("有") : QStringLiteral("—"));
            item->setText(3, candidate.java ? tr("有") : QStringLiteral("—"));
        }
        if (m_tree->topLevelItemCount() > 0)
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        layout->addWidget(m_tree, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("使用这个目录"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    }

    QString selected() const
    {
        auto* item = m_tree->currentItem();
        return item != nullptr ? item->text(0) : QString();
    }

   private:
    QTreeWidget* m_tree = nullptr;
};

}  // namespace

MapResourcePage::MapResourcePage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    auto* box = new QGroupBox(tr("资源库位置"), this);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(8);

    auto* intro = new QLabel(
        tr("Minecraft 的三样公共资源 —— 游戏资源（assets）、运行库（libraries）、"
           "Mojang 官方 Java 运行时（java，首次进图时那 400 多个 jvm.dll 就是它）—— 默认都放在启动器数据目录里。\n"
           "把这里指到别的盘（或指向本机已有的 .minecraft / 别的启动器目录），点「应用资源库」就会把这些文件"
           "真的搬过去，之后启动器直接读新位置：换启动器不用重新下载，也不占系统盘。"),
        box);
    intro->setWordWrap(true);
    boxLayout->addWidget(intro);

    auto* dirRow = new QHBoxLayout();
    m_dir = new QLineEdit(box);
    m_dir->setPlaceholderText(tr("留空 = 启动器数据目录（%1）").arg(QDir::toNativeSeparators(APPLICATION->dataRoot())));
    m_dir->setText(APPLICATION->settings()->get("SharedResourceDir").toString());
    auto* browseButton = new QPushButton(tr("浏览…"), box);
    auto* detectButton = new QPushButton(tr("检测本机…"), box);
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browseButton);
    dirRow->addWidget(detectButton);
    boxLayout->addLayout(dirRow);

    m_migrate = new QCheckBox(tr("把数据目录里已有的资源搬到目标目录"), box);
    m_migrate->setChecked(true);
    m_migrate->setToolTip(tr("勾选 = 直接迁移（同盘瞬间完成，跨盘会复制后删源）；\n"
                             "不勾 = 只切换路径，旧数据留在数据目录里自己处理。"));
    boxLayout->addWidget(m_migrate);

    auto* actionRow = new QHBoxLayout();
    m_apply = new QPushButton(tr("应用资源库"), box);
    m_apply->setToolTip(tr("迁移并切换路径。改完需要重启启动器才生效（资源路径在启动时就固定了）。"));
    actionRow->addWidget(m_apply);
    actionRow->addStretch(1);
    boxLayout->addLayout(actionRow);

    m_status = new QLabel(box);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    boxLayout->addWidget(m_status);

    layout->addWidget(box);

    m_current = new QLabel(this);
    m_current->setWordWrap(true);
    m_current->setEnabled(false);
    layout->addWidget(m_current);
    layout->addStretch(1);

    connect(browseButton, &QPushButton::clicked, this, &MapResourcePage::browse);
    connect(detectButton, &QPushButton::clicked, this, &MapResourcePage::detectExisting);
    connect(m_apply, &QPushButton::clicked, this, &MapResourcePage::applyNow);

    refreshStatus();
}

void MapResourcePage::refreshStatus()
{
    QStringList lines;
    for (const QString& name : ResourceStore::managedNames())
        lines << QStringLiteral("%1：%2").arg(name, describeState(name));
    m_current->setText(tr("当前：%1").arg(lines.join(QStringLiteral("　·　"))));
}

void MapResourcePage::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择资源库目录"), m_dir->text());
    if (!dir.isEmpty())
        m_dir->setText(QDir::toNativeSeparators(dir));
}

void MapResourcePage::detectExisting()
{
    const auto candidates = ResourceStore::scanCandidates();
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("没有找到可复用的目录"),
                                 tr("常见位置（当前数据目录、%%APPDATA%%\\.minecraft、其他 Prism/MultiMC 目录）里都没找到 assets / libraries / java。\n"
                                    "可以手动「浏览…」到任意目录。"));
        return;
    }

    CandidateDialog dialog(candidates, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selected().isEmpty())
        m_dir->setText(dialog.selected());
}

void MapResourcePage::applyNow()
{
    const QString root = m_dir->text().trimmed();
    APPLICATION->settings()->set("SharedResourceDir", root);

    if (root.isEmpty()) {
        m_status->setText(tr("已改回数据目录（重启启动器后生效）。之前迁移出去的文件没有搬回来，需要的话手动移回。"));
        refreshStatus();
        return;
    }

    QProgressDialog progress(tr("正在迁移资源…"), QString(), 0, 100, this);
    progress.setWindowTitle(tr("资源库"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);  // 复制到一半取消会留下半套文件，干脆不给取消
    progress.setMinimumDuration(0);
    progress.setValue(0);

    const QStringList errors = ResourceStore::apply(root, m_migrate->isChecked(),
                                                    [&progress](int done, int total, const QString& file) {
                                                        progress.setMaximum(qMax(1, total));
                                                        progress.setValue(done);
                                                        progress.setLabelText(tr("正在迁移 %1（%2 / %3）").arg(file).arg(done).arg(total));
                                                        QCoreApplication::processEvents();
                                                    });
    progress.close();

    if (errors.isEmpty()) {
        m_status->setStyleSheet(QString());
        m_status->setText(tr("✓ 已迁移到 %1。**重启启动器**后生效（资源路径在启动时固定）。").arg(QDir::toNativeSeparators(root)));
    } else {
        m_status->setStyleSheet(QStringLiteral("color: #d08a2c;"));
        m_status->setText(tr("有 %1 项没完成：\n· %2").arg(errors.size()).arg(errors.join(QStringLiteral("\n· "))));
    }
    refreshStatus();
}

bool MapResourcePage::apply()
{
    // 只记设置：真正的搬迁由「应用资源库」按钮执行（可能要搬几个 GB，不该发生在点「确定」时）
    APPLICATION->settings()->set("SharedResourceDir", m_dir->text().trimmed());
    return true;
}

}  // namespace maps
