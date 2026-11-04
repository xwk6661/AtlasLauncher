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
#include "ModSearchDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace maps {

ModSearchDialog::ModSearchDialog(const QString& gameVersion, const QString& loader, QWidget* parent)
    : QDialog(parent), m_gameVersion(gameVersion), m_loader(loader)
{
    setWindowTitle(tr("搜索并添加模组"));
    resize(720, 520);
    buildUi();
    startSearch();
}

void ModSearchDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    auto* searchRow = new QHBoxLayout();
    m_provider = new QComboBox(this);
    m_provider->addItem(tr("Modrinth"), QString::fromLatin1(modprovider::MODRINTH));
    m_provider->addItem(tr("CurseForge"), QString::fromLatin1(modprovider::CURSEFORGE));
    searchRow->addWidget(m_provider);

    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(tr("按名称搜索（留空看热门）"));
    m_query->setClearButtonEnabled(true);
    searchRow->addWidget(m_query, 1);

    auto* search = new QPushButton(tr("搜索"), this);
    search->setDefault(true);
    searchRow->addWidget(search);
    layout->addLayout(searchRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("模组"), tr("作者"), tr("简介") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::MultiSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    auto* close = new QPushButton(tr("关闭"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    actions->addWidget(close);

    m_add = new QPushButton(tr("添加选中的"), this);
    m_add->setEnabled(false);
    connect(m_add, &QPushButton::clicked, this, &ModSearchDialog::accept);
    actions->addWidget(m_add);
    layout->addLayout(actions);

    connect(search, &QPushButton::clicked, this, &ModSearchDialog::startSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &ModSearchDialog::startSearch);
    connect(m_provider, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { startSearch(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const int count = m_table->selectionModel() != nullptr ? m_table->selectionModel()->selectedRows().size() : 0;
        m_add->setEnabled(count > 0);
        m_add->setText(count > 0 ? tr("添加选中的 %1 个").arg(count) : tr("添加选中的"));
    });
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) { accept(); });
}

void ModSearchDialog::startSearch()
{
    ++m_generation;
    if (m_task != nullptr) {
        m_task->disconnect(this);
        m_task->abort();
        m_task->deleteLater();
        m_task = nullptr;
    }

    m_hits.clear();
    m_table->setRowCount(0);
    m_add->setEnabled(false);
    m_status->setText(tr("搜索中…"));

    const int generation = m_generation;
    m_task = new ModSearchTask(m_provider->currentData().toString(), m_query->text().trimmed(), m_gameVersion, m_loader,
                               this);
    connect(m_task, &Task::succeeded, this, [this, generation] {
        if (generation != m_generation)
            return;
        m_hits = m_task->hits();
        fillTable();
    });
    connect(m_task, &Task::failed, this, [this, generation](const QString& reason) {
        if (generation != m_generation)
            return;
        m_status->setText(tr("搜索失败：%1").arg(reason));
    });
    m_task->start();
}

void ModSearchDialog::fillTable()
{
    m_table->setRowCount(m_hits.size());
    for (int row = 0; row < m_hits.size(); ++row) {
        const auto& hit = m_hits.at(row);
        auto* name = new QTableWidgetItem(hit.name.isEmpty() ? hit.project : hit.name);
        name->setToolTip(hit.project);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(hit.author));
        m_table->setItem(row, 2, new QTableWidgetItem(hit.description));
    }
    m_status->setText(m_hits.isEmpty() ? tr("没有结果。换个关键词，或者确认这个加载器下真的有人发过。")
                                       : tr("共 %1 条（Ctrl/Shift 可多选，双击直接添加）").arg(m_hits.size()));
}

void ModSearchDialog::accept()
{
    m_selected.clear();
    const auto rows = m_table->selectionModel() != nullptr ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    for (const auto& index : rows) {
        if (index.row() < 0 || index.row() >= m_hits.size())
            continue;
        const auto& hit = m_hits.at(index.row());

        ModPresetEntry entry;
        entry.provider = hit.provider;
        entry.project = hit.project;
        entry.name = hit.name;
        entry.note = hit.description;
        entry.enabled = true;
        m_selected.append(entry);
    }

    if (m_selected.isEmpty()) {
        QDialog::reject();
        return;
    }
    QDialog::accept();
}

}  // namespace maps
