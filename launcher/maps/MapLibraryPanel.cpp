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
#include "MapLibraryPanel.h"

#include "MapTheme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace maps {

namespace {

constexpr const char* kMimeType = "application/x-atlas-map-library-item";

QString humanBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

/// 只读表格：拖出去时带的是**文件名**，不是显示文本
class LibraryTable : public QTableWidget {
   public:
    using QTableWidget::QTableWidget;

   protected:
    void startDrag(Qt::DropActions) override
    {
        auto* item = currentItem();
        if (item == nullptr)
            return;
        const QString file = item->data(Qt::UserRole).toString();
        if (file.isEmpty())
            return;

        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(kMimeType), file.toUtf8());
        mime->setText(file);

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
    }
};

/// 分类列表：接住从右边拖过来的行，回调交给面板
class CategoryList : public QListWidget {
   public:
    using QListWidget::QListWidget;

    std::function<void(const QString&, const QString&)> onDropFile;

   protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (accepts(event->mimeData()))
            event->acceptProposedAction();
        else
            event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (accepts(event->mimeData()) && itemAt(event->position().toPoint()) != nullptr)
            event->acceptProposedAction();
        else
            event->ignore();
    }

    void dropEvent(QDropEvent* event) override
    {
        const QString file = QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kMimeType)));
        auto* target = itemAt(event->position().toPoint());
        if (file.isEmpty() || target == nullptr || !onDropFile) {
            event->ignore();
            return;
        }
        const QString catId = target->data(Qt::UserRole).toString();
        if (catId.isEmpty()) {                     // 「全部」不是分类，不接受投放
            event->ignore();
            return;
        }
        onDropFile(file, catId);
        event->acceptProposedAction();
    }

   private:
    static bool accepts(const QMimeData* mime)
    {
        return mime != nullptr && mime->hasFormat(QString::fromLatin1(kMimeType));
    }
};

}  // namespace

MapLibraryPanel::MapLibraryPanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    auto* title = new QLabel(tr("本地地图库"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);

    m_summary = new QLabel(this);
    m_summary->setEnabled(false);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setMinimumWidth(0);
    header->addWidget(m_summary, 1);

    auto* openButton = new QPushButton(tr("目录"), this);
    openButton->setToolTip(tr("打开地图库目录（重装用的备份就放在这里）"));
    header->addWidget(openButton);

    auto* refreshButton = new QPushButton(tr("刷新"), this);
    header->addWidget(refreshButton);

    m_importButton = new QPushButton(tr("导入选中"), this);
    m_importButton->setEnabled(false);
    header->addWidget(m_importButton);

    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(8);

    // 左侧分类栏容器：固定宽度 132px，防止挤占右侧表格（解决 8.1）
    auto* catContainer = new QWidget(this);
    catContainer->setFixedWidth(132);
    auto* catLayout = new QVBoxLayout(catContainer);
    catLayout->setContentsMargins(0, 0, 0, 0);
    catLayout->setSpacing(4);

    m_categories = new CategoryList(this);
    m_categories->setFixedWidth(132);
    m_categories->setAcceptDrops(true);
    m_categories->setDragDropMode(QAbstractItemView::DropOnly);
    m_categories->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categories->setContextMenuPolicy(Qt::CustomContextMenu);
    m_categories->setToolTip(tr("把右边的地图拖到分类上即可归类；右键可新建、重命名或删除分类"));
    static_cast<CategoryList*>(m_categories)->onDropFile = [this](const QString& file, const QString& categoryId) {
        applyCategory(file, categoryId);
    };
    catLayout->addWidget(m_categories, 1);

    auto* catBtnLayout = new QHBoxLayout;
    catBtnLayout->setContentsMargins(0, 0, 0, 0);
    catBtnLayout->setSpacing(4);

    m_addCategoryBtn = new QPushButton(QStringLiteral("+"), this);
    m_addCategoryBtn->setToolTip(tr("新建分类"));
    m_addCategoryBtn->setFixedWidth(64);

    m_delCategoryBtn = new QPushButton(QStringLiteral("-"), this);
    m_delCategoryBtn->setToolTip(tr("删除选中分类（其下地图将移至 Other）"));
    m_delCategoryBtn->setFixedWidth(64);
    m_delCategoryBtn->setEnabled(false);

    catBtnLayout->addWidget(m_addCategoryBtn);
    catBtnLayout->addWidget(m_delCategoryBtn);
    catLayout->addLayout(catBtnLayout);

    body->addWidget(catContainer);

    m_table = new LibraryTable(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("地图"), tr("版本"), tr("分类"), tr("状态") });
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setDragEnabled(true);
    m_table->setDragDropMode(QAbstractItemView::DragOnly);
    m_table->setMinimumHeight(190);
    m_table->setMinimumWidth(0);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setToolTip(tr("拖动行到左侧分类即可归类；双击直接导入"));
    body->addWidget(m_table, 1);

    root->addLayout(body, 1);

    connect(refreshButton, &QPushButton::clicked, this, &MapLibraryPanel::refresh);
    connect(openButton, &QPushButton::clicked, this, [this] {
        const QString dir = MapLibrary::libraryDir();
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(this, tr("打开库目录"), tr("无法创建地图库目录：%1").arg(dir));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(m_importButton, &QPushButton::clicked, this, [this] {
        const auto selected = m_table->selectionModel()->selectedRows();
        if (selected.isEmpty())
            return;
        const QString file = m_table->item(selected.first().row(), 0)->data(Qt::UserRole).toString();
        const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();
        if (QFileInfo::exists(path))
            emit importRequested(path);
    });

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        m_importButton->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString file = m_table->item(row, 0)->data(Qt::UserRole).toString();
        const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();
        if (QFileInfo::exists(path))
            emit importRequested(path);
    });

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_categories->count()) {
            m_delCategoryBtn->setEnabled(false);
            return;
        }
        const QString catId = m_categories->item(row)->data(Qt::UserRole).toString();
        m_showAll = catId.isEmpty();
        m_filterCategoryId = catId;
        m_delCategoryBtn->setEnabled(!m_showAll && catId != QLatin1String("other"));
        rebuildTable();
    });

    connect(m_addCategoryBtn, &QPushButton::clicked, this, &MapLibraryPanel::onAddCategoryClicked);
    connect(m_delCategoryBtn, &QPushButton::clicked, this, &MapLibraryPanel::onDeleteCategoryClicked);
    connect(m_categories, &QWidget::customContextMenuRequested, this, &MapLibraryPanel::showCategoryContextMenu);

    connect(&m_library, &MapLibrary::changed, this, [this] {
        rebuildCategories();
        rebuildTable();
    }, Qt::QueuedConnection);

    refresh();
}

void MapLibraryPanel::refresh()
{
    m_library.reload();
    rebuildCategories();
    rebuildTable();
}

void MapLibraryPanel::onAddCategoryClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("新建分类"), tr("分类名称："), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString id = m_library.addCategory(name.trimmed());
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("新建分类失败"), tr("分类名称无效或已存在同名分类。"));
        return;
    }
}

void MapLibraryPanel::onRenameCategoryClicked()
{
    auto* item = m_categories->currentItem();
    if (item == nullptr)
        return;
    const QString catId = item->data(Qt::UserRole).toString();
    if (catId.isEmpty())
        return;

    const QString oldName = m_library.categoryName(catId);
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("重命名分类"), tr("新分类名称："), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
        return;

    if (!m_library.renameCategory(catId, newName.trimmed())) {
        QMessageBox::warning(this, tr("重命名失败"), tr("无法更新分类名称。"));
    }
}

void MapLibraryPanel::onDeleteCategoryClicked()
{
    auto* item = m_categories->currentItem();
    if (item == nullptr)
        return;
    const QString catId = item->data(Qt::UserRole).toString();
    if (catId.isEmpty() || catId == QLatin1String("other"))
        return;

    const QString catName = m_library.categoryName(catId);
    const int count = m_library.count(catId);
    const auto res = QMessageBox::question(
        this, tr("删除分类"),
        tr("确定要删除分类「%1」吗？\n该分类下的 %2 张地图将被移至「Other」分类。").arg(catName).arg(count),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (res != QMessageBox::Yes)
        return;

    if (!m_library.deleteCategory(catId)) {
        QMessageBox::warning(this, tr("删除分类失败"), tr("无法删除该分类。"));
    }
}

void MapLibraryPanel::showCategoryContextMenu(const QPoint& pos)
{
    auto* item = m_categories->itemAt(pos);
    QMenu menu(this);

    auto* addAction = menu.addAction(tr("新建分类..."));
    connect(addAction, &QAction::triggered, this, &MapLibraryPanel::onAddCategoryClicked);

    if (item != nullptr) {
        const QString catId = item->data(Qt::UserRole).toString();
        if (!catId.isEmpty()) {
            auto* renameAction = menu.addAction(tr("重命名分类..."));
            connect(renameAction, &QAction::triggered, this, &MapLibraryPanel::onRenameCategoryClicked);

            if (catId != QLatin1String("other")) {
                auto* delAction = menu.addAction(tr("删除分类"));
                connect(delAction, &QAction::triggered, this, &MapLibraryPanel::onDeleteCategoryClicked);
            }
        }
    }

    menu.exec(m_categories->mapToGlobal(pos));
}

void MapLibraryPanel::rebuildCategories()
{
    const QString previousCat = (m_categories->currentItem() != nullptr)
                                    ? m_categories->currentItem()->data(Qt::UserRole).toString()
                                    : (m_showAll ? QString() : m_filterCategoryId);
    QSignalBlocker blocker(m_categories);
    m_categories->clear();

    auto* all = new QListWidgetItem(tr("全部（%1）").arg(m_library.items().size()), m_categories);
    all->setData(Qt::UserRole, QString());

    int selectedRow = 0;
    int rowIdx = 1;
    for (const auto& cat : m_library.categories()) {
        auto* item = new QListWidgetItem(QStringLiteral("%1（%2）").arg(cat.name).arg(m_library.count(cat.id)),
                                         m_categories);
        item->setData(Qt::UserRole, cat.id);
        if (!m_showAll && cat.id == previousCat)
            selectedRow = rowIdx;
        rowIdx++;
    }

    if (selectedRow >= m_categories->count())
        selectedRow = 0;

    m_categories->setCurrentRow(selectedRow);
    const QString currentCatId = m_categories->currentItem() ? m_categories->currentItem()->data(Qt::UserRole).toString() : QString();
    m_showAll = currentCatId.isEmpty();
    m_filterCategoryId = currentCatId;
    m_delCategoryBtn->setEnabled(!currentCatId.isEmpty() && currentCatId != QLatin1String("other"));
}

void MapLibraryPanel::rebuildTable()
{
    const auto items = m_showAll ? m_library.items() : m_library.items(m_filterCategoryId);

    QString previousFile;
    if (auto* current = m_table->currentItem())
        previousFile = current->data(Qt::UserRole).toString();

    m_table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const auto& item = items.at(row);

        auto* name = new QTableWidgetItem(item.displayName());
        name->setData(Qt::UserRole, item.file);
        QString tooltip = item.path;
        if (!item.note.isEmpty())
            tooltip += QLatin1String("\n") + item.note;
        if (item.installed)
            tooltip += QLatin1String("\n") + tr("已安装：%1").arg(item.installedInstances.join(QStringLiteral("、")));
        name->setToolTip(tooltip);
        m_table->setItem(row, 0, name);

        QString version = item.mcVersion;
        if (!item.mapVersion.isEmpty())
            version += version.isEmpty() ? item.mapVersion : QStringLiteral(" · ") + item.mapVersion;

        auto* versionItem = new QTableWidgetItem(version.isEmpty() ? QStringLiteral("—") : version);
        versionItem->setData(Qt::UserRole, item.file);
        m_table->setItem(row, 1, versionItem);

        auto* categoryItem = new QTableWidgetItem(m_library.categoryName(item.category));
        categoryItem->setData(Qt::UserRole, item.file);
        m_table->setItem(row, 2, categoryItem);

        auto* stateItem = new QTableWidgetItem(item.installed ? tr("已安装") : QStringLiteral("—"));
        stateItem->setData(Qt::UserRole, item.file);
        if (item.installed) {
            stateItem->setForeground(ui::confidenceColor(Confidence::High));
            stateItem->setToolTip(tr("已安装为实例：%1").arg(item.installedInstances.join(QStringLiteral("、"))));
        }
        m_table->setItem(row, 3, stateItem);
    }

    if (!previousFile.isEmpty()) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* first = m_table->item(row, 0);
            if (first != nullptr && first->data(Qt::UserRole).toString() == previousFile) {
                m_table->selectRow(row);
                m_table->setCurrentCell(row, 0);
                break;
            }
        }
    }

    qint64 total = 0;
    int installed = 0;
    for (const auto& item : items) {
        total += item.bytes;
        if (item.installed)
            ++installed;
    }

    m_summary->setText(tr("%1 张 · %2 · 已安装 %3 张")
                           .arg(items.size())
                           .arg(humanBytes(total))
                           .arg(installed));
    m_summary->setToolTip(tr("%1 张地图，共 %2，已安装 %3 张\n库目录：%4")
                              .arg(items.size())
                              .arg(humanBytes(total))
                              .arg(installed)
                              .arg(QDir::toNativeSeparators(MapLibrary::libraryDir())));

    m_importButton->setEnabled(false);
}

void MapLibraryPanel::applyCategory(const QString& file, const QString& categoryId)
{
    const bool ok = m_library.setCategory(file, categoryId);
    if (!ok) {
        QMessageBox::warning(this, tr("改分类失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
    }
    QTimer::singleShot(0, this, [this] {
        rebuildCategories();
        rebuildTable();
    });
}

}  // namespace maps
