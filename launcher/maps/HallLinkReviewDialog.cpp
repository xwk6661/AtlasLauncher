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
#include "HallLinkReviewDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "HallCache.h"
#include "HallMatcher.h"

namespace maps {

namespace {

/// 下拉里「保持当前关联」的哨兵值（空串已被「解除关联」占用）
constexpr const char* KEEP_CURRENT = "__keep__";

constexpr int COL_CHECK = 0;
constexpr int COL_MAP = 1;
constexpr int COL_CURRENT = 2;
constexpr int COL_EVIDENCE = 3;
constexpr int COL_TARGET = 4;

QString candidateLine(const HallMatchCandidate& candidate)
{
    return HallLinkReviewDialog::tr("%1（%2 · %3）").arg(candidate.name, candidate.reason).arg(candidate.score, 0, 'f', 2);
}

}  // namespace

HallLinkReviewDialog::HallLinkReviewDialog(MapLibrary* library, QWidget* parent) : QDialog(parent), m_library(library)
{
    setWindowTitle(tr("待确认的大厅关联"));
    resize(960, 560);

    auto* layout = new QVBoxLayout(this);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setText(tr("这些条目是自动匹配的（索引里记作未确认），程序只给建议、不替你确认。\n"
                       "勾选 = 认可当前关联；想换成别的条目，在右侧下拉里改。「改归属」会把这张图并到新条目所在的那一组，"
                       "只确认则分组不变。"));
    layout->addWidget(m_hint);

    m_snapshot = HallCache::load();
    for (const auto& value : m_snapshot.listRows) {
        const auto row = value.toObject();
        const QString slug = row.value(QStringLiteral("slug")).toString();
        if (!slug.isEmpty())
            m_snapshotRows.insert(slug, row);
    }

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("确认"), tr("本地这张图"), tr("当前关联"), tr("匹配证据"), tr("归属") });
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_CHECK, QHeaderView::Fixed);
    m_table->setColumnWidth(COL_CHECK, 48);
    m_table->horizontalHeader()->setSectionResizeMode(COL_MAP, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_CURRENT, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_EVIDENCE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TARGET, QHeaderView::Fixed);
    m_table->setColumnWidth(COL_TARGET, 300);
    layout->addWidget(m_table, 1);

    buildRows();

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    auto* buttons = new QHBoxLayout();
    auto* selectAll = new QPushButton(tr("全选"), this);
    auto* selectNone = new QPushButton(tr("全不选"), this);
    buttons->addWidget(selectAll);
    buttons->addWidget(selectNone);
    buttons->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_confirmButton = new QPushButton(tr("确认选中的关联"), this);
    box->addButton(m_confirmButton, QDialogButtonBox::AcceptRole);
    box->button(QDialogButtonBox::Close)->setText(tr("取消"));
    buttons->addWidget(box);
    layout->addLayout(buttons);

    connect(selectAll, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < m_table->rowCount(); ++row)
            m_table->item(row, COL_CHECK)->setCheckState(Qt::Checked);
    });
    connect(selectNone, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < m_table->rowCount(); ++row)
            m_table->item(row, COL_CHECK)->setCheckState(Qt::Unchecked);
    });
    connect(m_confirmButton, &QPushButton::clicked, this, &HallLinkReviewDialog::applySelection);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { updateSummary(); });

    updateSummary();
}

QString HallLinkReviewDialog::hallTitle(const QString& slug, bool* cacheValid) const
{
    if (cacheValid != nullptr)
        *cacheValid = m_snapshot.isValid();
    const auto row = m_snapshotRows.value(slug);
    return row.value(QStringLiteral("name")).toString();
}

void HallLinkReviewDialog::buildRows()
{
    m_rows.clear();
    if (m_library != nullptr) {
        for (const auto& item : m_library->items()) {
            if (item.hallSlug.isEmpty() || item.hallVerified)
                continue;
            m_rows.append(item);
        }
    }

    std::sort(m_rows.begin(), m_rows.end(),
              [](const LibraryItem& a, const LibraryItem& b) { return a.displayName().localeAwareCompare(b.displayName()) < 0; });

    m_table->setRowCount(m_rows.size());
    const bool cacheValid = m_snapshot.isValid();

    for (int row = 0; row < m_rows.size(); ++row) {
        const auto& item = m_rows.at(row);

        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Unchecked);
        m_table->setItem(row, COL_CHECK, check);

        auto* map = new QTableWidgetItem(item.displayName());
        map->setToolTip(tr("%1\n文件：%2\nMC 版本：%3").arg(item.displayName(), item.file, item.mcVersion));
        m_table->setItem(row, COL_MAP, map);

        bool hasCache = false;
        const QString currentTitle = hallTitle(item.hallSlug, &hasCache);
        const QString currentText = hasCache
                                        ? (currentTitle.isEmpty() ? tr("%1（缓存里找不到这条）").arg(item.hallSlug) : currentTitle)
                                        : tr("%1（大厅缓存还没同步）").arg(item.hallSlug);
        auto* current = new QTableWidgetItem(currentText);
        current->setToolTip(tr("slug：%1\n未确认：这只是自动匹配的结论，站上条目可能不是这张图。").arg(item.hallSlug));
        m_table->setItem(row, COL_CURRENT, current);

        HallMatchQuery query;
        query.englishName = item.englishName;
        query.chineseName = item.chineseName.isEmpty() ? item.title : item.chineseName;
        query.fileName = item.file;
        query.mcVersion = item.mcVersion;
        const auto candidates = cacheValid ? HallMatcher::rank(m_snapshot.listRows, query, 4)
                                           : QList<HallMatchCandidate>{};

        QString evidenceText;
        if (!cacheValid) {
            evidenceText = tr("大厅缓存未同步，无法复核");
        } else if (candidates.isEmpty()) {
            evidenceText = tr("没有匹配到候选");
        } else if (candidates.first().slug == item.hallSlug) {
            evidenceText = tr("★ 与当前关联一致　%1").arg(candidateLine(candidates.first()));
        } else {
            evidenceText = tr("★ 建议改为　%1").arg(candidateLine(candidates.first()));
        }
        auto* evidence = new QTableWidgetItem(evidenceText);
        QStringList detail;
        for (const auto& candidate : candidates)
            detail << QStringLiteral("%1  ·  %2  ·  MC %3  ·  %4").arg(candidate.name, candidate.slug, candidate.version, candidate.reason);
        evidence->setToolTip(detail.isEmpty() ? tr("缓存里没有相近的条目。") : detail.join(QLatin1Char('\n')));
        m_table->setItem(row, COL_EVIDENCE, evidence);

        auto* target = new QComboBox(m_table);
        target->addItem(tr("保持当前关联（只确认）"), QString::fromLatin1(KEEP_CURRENT));
        target->addItem(tr("解除关联"), QString());
        for (const auto& candidate : candidates) {
            if (candidate.slug == item.hallSlug)
                continue;
            target->addItem(tr("改为：%1").arg(candidateLine(candidate)), candidate.slug);
        }
        m_table->setCellWidget(row, COL_TARGET, target);
        connect(target, &QComboBox::currentIndexChanged, this, [this] { updateSummary(); });

        // 只有「证据支持当前关联」的行才默认勾上：批量确认最容易犯的错是闭眼全选
        const bool supported = cacheValid && !candidates.isEmpty() && candidates.first().slug == item.hallSlug &&
                               candidates.first().score >= 0.75;
        check->setCheckState(supported ? Qt::Checked : Qt::Unchecked);
        if (!supported)
            current->setToolTip(current->toolTip() + tr("\n\n匹配器没有给出同等强度的证据，请自己看一眼再勾。"));
    }
}

void HallLinkReviewDialog::updateSummary()
{
    int checked = 0;
    int changing = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto* check = m_table->item(row, COL_CHECK);
        if (check == nullptr || check->checkState() != Qt::Checked)
            continue;
        ++checked;
        if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_TARGET))) {
            const QString slug = combo->currentData().toString();
            if (slug != QLatin1String(KEEP_CURRENT))
                ++changing;
        }
    }

    if (m_table->rowCount() == 0) {
        m_summary->setText(tr("没有待确认的关联 —— 所有条目都人工确认过了。"));
    } else {
        m_summary->setText(tr("共 %1 条待确认 · 已勾选 %2 条 · 其中会改归属或解除的 %3 条")
                               .arg(m_table->rowCount())
                               .arg(checked)
                               .arg(changing));
    }
    if (m_confirmButton != nullptr)
        m_confirmButton->setEnabled(checked > 0);
}

void HallLinkReviewDialog::applySelection()
{
    if (m_library == nullptr)
        return;

    QList<MapLibrary::HallLinkEdit> edits;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto* check = m_table->item(row, COL_CHECK);
        if (check == nullptr || check->checkState() != Qt::Checked)
            continue;
        auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_TARGET));
        QString slug = combo != nullptr ? combo->currentData().toString() : QString();
        if (slug == QLatin1String(KEEP_CURRENT))
            slug = m_rows.at(row).hallSlug;
        edits.append({ m_rows.at(row).file, slug, !slug.isEmpty() });
    }

    if (edits.isEmpty()) {
        QMessageBox::information(this, tr("待确认的大厅关联"), tr("没有勾选任何条目。"));
        return;
    }

    QString error;
    const int changed = m_library->applyHallLinkEdits(edits, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("待确认的大厅关联"), error);
        return;
    }

    m_changed = changed;
    if (changed == 0) {
        QMessageBox::information(this, tr("待确认的大厅关联"), tr("这些条目本来就是已确认状态，没有需要写入的改动。"));
    } else {
        QMessageBox::information(this, tr("待确认的大厅关联"), tr("已更新 %1 条关联。").arg(changed));
    }
    accept();
}

int HallLinkReviewDialog::review(MapLibrary* library, QWidget* parent)
{
    if (library == nullptr)
        return 0;

    if (library->unverifiedHallLinkCount() == 0) {
        QMessageBox::information(parent, tr("待确认的大厅关联"),
                                 tr("没有待确认的关联 —— 所有条目都人工确认过了。"));
        return 0;
    }

    HallLinkReviewDialog dialog(library, parent);
    if (dialog.exec() != QDialog::Accepted)
        return 0;
    return dialog.changedCount();
}

}  // namespace maps
