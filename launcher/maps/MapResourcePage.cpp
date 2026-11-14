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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "MapInfoCard.h"
#include "MapStyle.h"
#include "ResourcePaths.h"
#include "ResourceStore.h"
#include "settings/SettingsObject.h"

namespace maps {

/// 一行「根目录」/「路径」用的标签：宽度不够时从中间省略。
/// 直接 setText 整条路径会把布局撑到几千像素，窗口一窄就换行、把页面顶得很高。
class ElidedLabel : public QLabel {
   public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    void setPath(const QString& path)
    {
        m_path = QDir::toNativeSeparators(path);
        setToolTip(m_path);
        updateText();
    }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

   private:
    void updateText()
    {
        const int available = width() - contentsMargins().left() - contentsMargins().right();
        const QString elided =
            available > 0 ? fontMetrics().elidedText(m_path, Qt::ElideMiddle, available) : m_path;
        // 只在结果变化时 setText：resize 里无条件写文本会触发一次多余的布局重算
        if (elided != text())
            QLabel::setText(elided);
    }

    QString m_path;
};

namespace {

/// 三个资源目录各自的状态。**只回答"在不在、能不能用"**，
/// 路径由页面上方的「根目录」统一交代，不再每行重复四五十个字符。
struct ResourceState {
    QString name;
    QString state;
    QString path;
    bool ready = false;
    bool missing = false;
};

ResourceState describeState(const QString& name)
{
    ResourceState out;
    out.name = name;
    const QString dir = resource::dirFor(name);
    out.path = QDir::cleanPath(QFileInfo(dir).absolutePath());

    const QFileInfo info(dir);
    if (!info.exists()) {
        out.state = QObject::tr("未创建");
        out.missing = true;
    } else if (!info.isDir()) {
        out.state = QObject::tr("不是目录");
    } else {
        out.state = QObject::tr("已就绪");
        out.ready = true;
    }
    return out;
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
    layout->setContentsMargins(ui::pageMargins());
    layout->setSpacing(ui::kSpaceM);

    // ── 位置 ──
    // 说明只留「是什么 + 有什么用」，细节（三个目录各装什么）收进 tooltip：
    // 设置页的第一眼应该看见控件，而不是先读一段四行的散文。
    auto* box = new QGroupBox(tr("资源库位置"), this);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(ui::kSpaceS);

    auto* intro = new QLabel(tr("Minecraft 的三份公共资源默认放在数据目录里。指到别的盘、或本机已有的 "
                                ".minecraft 目录，就能复用资源、不占系统盘。"),
                             box);
    intro->setWordWrap(true);
    intro->setToolTip(tr("assets：音效、语言、材质索引\n"
                         "libraries：Java 类库，以及每个版本的 client jar\n"
                         "java：Mojang 官方 Java 运行时（首次进图时那 400 多个 jvm.dll 就是它）"));
    boxLayout->addWidget(intro);

    auto* dirRow = new QHBoxLayout();
    dirRow->setSpacing(ui::kSpaceS);
    m_dir = new QLineEdit(box);
    m_dir->setPlaceholderText(tr("留空 = 使用数据目录"));
    m_dir->setToolTip(tr("当前数据目录：%1").arg(QDir::toNativeSeparators(APPLICATION->dataRoot())));
    m_dir->setText(APPLICATION->settings()->get("SharedResourceDir").toString());
    auto* browseButton = new QPushButton(tr("浏览…"), box);
    auto* detectButton = new QPushButton(tr("检测本机…"), box);
    ui::applySecondaryButton(browseButton);
    ui::applySecondaryButton(detectButton);
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browseButton);
    dirRow->addWidget(detectButton);
    boxLayout->addLayout(dirRow);

    m_migrate = new QCheckBox(tr("把现有资源一并搬过去"), box);
    m_migrate->setChecked(true);
    m_migrate->setToolTip(tr("勾选 = 迁移（同盘瞬间完成，跨盘复制后删源）；\n"
                             "不勾 = 只切换路径，旧数据留在原处自己处理。"));
    boxLayout->addWidget(m_migrate);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(ui::kSpaceM);
    m_apply = ui::makePrimaryButton(tr("应用资源库"), box);
    m_apply->setToolTip(tr("迁移并切换路径。改完立即生效：资源路径与下载账本会一起改过去，不用重启。"));
    actionRow->addWidget(m_apply);
    auto* applyHint = new QLabel(tr("只点「确定」不会搬文件；应用后立即生效"), box);
    applyHint->setFont(ui::metaFont(applyHint->font()));
    applyHint->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    actionRow->addWidget(applyHint, 1);
    boxLayout->addLayout(actionRow);

    m_status = new QLabel(box);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setVisible(false);
    boxLayout->addWidget(m_status);

    layout->addWidget(box);

    // ── 当前状态 ──
    // 根目录只说一次，三行只列「名字 + 状态」；只有某个目录确实不在根目录下时，
    // 才在第三列补上它自己的路径。原来一行里重复三遍同一个长前缀，读起来全是斜杠。
    auto* statusBox = new QGroupBox(tr("当前状态"), this);
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->setSpacing(ui::kSpaceS);

    auto* rootRow = new QHBoxLayout();
    rootRow->setSpacing(ui::kSpaceS);
    auto* rootKey = new QLabel(tr("根目录"), statusBox);
    rootKey->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    rootKey->setFixedWidth(72);
    rootRow->addWidget(rootKey);
    m_rootPath = new ElidedLabel(statusBox);
    rootRow->addWidget(m_rootPath, 1);
    statusLayout->addLayout(rootRow);

    m_statusBody = new QWidget(statusBox);
    m_statusGrid = new QGridLayout(m_statusBody);
    m_statusGrid->setContentsMargins(0, 0, 0, 0);
    m_statusGrid->setHorizontalSpacing(ui::kSpaceM);
    m_statusGrid->setVerticalSpacing(ui::kSpaceXS);
    m_statusGrid->setColumnMinimumWidth(0, 72);
    m_statusGrid->setColumnStretch(2, 1);
    statusLayout->addWidget(m_statusBody);

    layout->addWidget(statusBox);
    layout->addStretch(1);

    connect(browseButton, &QPushButton::clicked, this, &MapResourcePage::browse);
    connect(detectButton, &QPushButton::clicked, this, &MapResourcePage::detectExisting);
    connect(m_apply, &QPushButton::clicked, this, &MapResourcePage::applyNow);

    refreshStatus();
}

void MapResourcePage::refreshStatus()
{
    if (m_statusGrid == nullptr)
        return;

    ui::clearLayout(m_statusGrid);

    QString root = APPLICATION->settings()->get("SharedResourceDir").toString().trimmed();
    if (root.isEmpty())
        root = APPLICATION->dataRoot();
    root = QDir::cleanPath(root);
    if (m_rootPath != nullptr)
        m_rootPath->setPath(root);

    int row = 0;
    for (const QString& name : ResourceStore::managedNames()) {
        const ResourceState state = describeState(name);

        auto* nameLabel = new QLabel(state.name, m_statusBody);
        m_statusGrid->addWidget(nameLabel, row, 0, Qt::AlignVCenter);

        const QColor color = state.ready ? QColor(ui::successColorName())
                                         : state.missing ? ui::mutedTextColor() : QColor(ui::warningColorName());
        auto* stateLabel = new QLabel(QStringLiteral("<span style='color:%1'>●</span> %2")
                                          .arg(color.name(), state.state.toHtmlEscaped()),
                                      m_statusBody);
        m_statusGrid->addWidget(stateLabel, row, 1, Qt::AlignVCenter);

        // 只有不在根目录下才显示独立路径；正常情况下三行加起来也只占一列宽度
        if (state.path != root) {
            auto* pathLabel = new ElidedLabel(m_statusBody);
            pathLabel->setPath(state.path);
            m_statusGrid->addWidget(pathLabel, row, 2, Qt::AlignVCenter);
        }
        ++row;
    }
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
        // 改回数据目录：基址也要跟着退回去，否则本次会话仍按旧根找库文件
        APPLICATION->refreshResourcePaths();
        m_status->setStyleSheet(QString());
        m_status->setVisible(true);
        m_status->setText(tr("已改回数据目录（立即生效）。之前迁移出去的文件没有搬回来，需要的话手动移回。"));
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

    // 文件已经在目标位置，下载账本（metacache 的 libraries / asset_indexes 基址）必须一起挪。
    // 少了这一步：本次会话仍按旧根解析 → 判定"文件不存在" → 条目被除名写盘 → 重启后整份重下；
    // classpath 也会继续指向旧目录，客户端直接 ClassNotFoundException。
    APPLICATION->refreshResourcePaths();

    if (errors.isEmpty()) {
        m_status->setVisible(true);
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg(ui::successColorName()));
        m_status->setText(tr("已迁移到 %1，下载账本已同步（立即生效，不用重启）。"
                             "如果目标位置本来就有别的启动器的资源，已有的那份会按 sha1 直接认下来，不会重下。")
                              .arg(QDir::toNativeSeparators(root)));
    } else {
        m_status->setVisible(true);
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg(ui::warningColorName()));
        m_status->setText(tr("有 %1 项没完成：\n· %2").arg(errors.size()).arg(errors.join(QStringLiteral("\n· "))));
    }
    refreshStatus();
}

bool MapResourcePage::apply()
{
    // 只记设置：真正的搬迁由「应用资源库」按钮执行（可能要搬几个 GB，不该发生在点「确定」时）。
    // 但路径一改就要立刻同步基址 —— 否则「改了路径、没搬文件」这种最常见的手动切换
    // 会让本次会话的下载写到旧目录、classpath 指向旧目录。
    const QString root = m_dir->text().trimmed();
    if (root != APPLICATION->settings()->get("SharedResourceDir").toString()) {
        APPLICATION->settings()->set("SharedResourceDir", root);
        APPLICATION->refreshResourcePaths();
    }
    return true;
}

}  // namespace maps
