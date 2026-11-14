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
#include "MapStyle.h"
#include "FileSystem.h"
#include "MapPackAttach.h"
#include "MapMeta.h"

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
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
    root->setSpacing(ui::kSpaceS);

    auto* header = new QHBoxLayout;
    header->setSpacing(ui::kSpaceS);
    auto* title = new QLabel(tr("本地地图库"), this);
    title->setFont(ui::cardTitleFont(title->font()));
    header->addWidget(title);

    m_summary = new QLabel(this);
    m_summary->setEnabled(false);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setMinimumWidth(0);
    header->addWidget(m_summary, 1);

    auto* openButton = new QPushButton(tr("目录"), this);
    openButton->setToolTip(tr("打开地图库目录（重装用的备份就放在这里）"));
    ui::applySecondaryButton(openButton);
    header->addWidget(openButton);

    auto* refreshButton = new QPushButton(tr("刷新"), this);
    ui::applySecondaryButton(refreshButton);
    header->addWidget(refreshButton);

    m_importButton = new QPushButton(tr("导入选中"), this);
    m_importButton->setEnabled(false);
    ui::applySecondaryButton(m_importButton);
    header->addWidget(m_importButton);

    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(ui::kSpaceM);

    // 左侧分类栏容器：固定宽度 132px，防止挤占右侧表格（解决 8.1）
    auto* catContainer = new QWidget(this);
    catContainer->setFixedWidth(132);
    auto* catLayout = new QVBoxLayout(catContainer);
    catLayout->setContentsMargins(0, 0, 0, 0);
    catLayout->setSpacing(4);

    m_categories = new CategoryList(this);
    m_categories->setFixedWidth(132);
    // 与右侧表格一样去掉原生边框：两者并排时，一个有线一个没线会显得歪
    m_categories->setFrameShape(QFrame::NoFrame);
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
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("地图"), tr("版本"), tr("语言"), tr("分类"), tr("状态") });
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setDragEnabled(true);
    m_table->setDragDropMode(QAbstractItemView::DragOnly);
    m_table->setMinimumHeight(220);
    m_table->setMinimumWidth(0);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setToolTip(tr("拖动行到左侧分类即可归类；双击直接导入"));
    ui::tuneTable(m_table);
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
        auto* first = m_table->item(selected.first().row(), 0);
        const QString file = first != nullptr ? first->data(Qt::UserRole).toString() : QString();
        if (file.isEmpty())  // 组标题行没有文件名，不参与导入
            return;
        const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();
        if (QFileInfo::exists(path))
            emit importRequested(path);
    });

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        bool actionable = false;
        for (const auto& index : m_table->selectionModel()->selectedRows()) {
            auto* first = m_table->item(index.row(), 0);
            if (first != nullptr && !first->data(Qt::UserRole).toString().isEmpty()) {
                actionable = true;
                break;
            }
        }
        m_importButton->setEnabled(actionable);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* first = m_table->item(row, 0);
        const QString file = first != nullptr ? first->data(Qt::UserRole).toString() : QString();
        if (file.isEmpty())
            return;
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

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &MapLibraryPanel::showItemContextMenu);

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

    // ── 按「图」归组：同一张图的原版/简中/繁中排在一起 ──
    // 分组键是 MapLibrary::groupKey()：关联了大厅就是 slug，否则是归一化图名。
    // 所以改名不会把同一张图拆成两组；没关联的仍靠名字兜底。
    QStringList order;
    QHash<QString, QList<LibraryItem>> groups;
    for (const auto& item : items) {
        const QString key = item.groupKey();
        if (!groups.contains(key))
            order << key;
        groups[key].append(item);
    }

    const auto groupInstalled = [](const QList<LibraryItem>& list) {
        int count = 0;
        for (const auto& item : list) {
            if (item.installed)
                ++count;
        }
        return count;
    };

    std::sort(order.begin(), order.end(), [&groups, &groupInstalled](const QString& left, const QString& right) {
        const bool leftInstalled = groupInstalled(groups.value(left)) > 0;
        const bool rightInstalled = groupInstalled(groups.value(right)) > 0;
        if (leftInstalled != rightInstalled)
            return leftInstalled;
        const auto& leftItems = groups.value(left);
        const auto& rightItems = groups.value(right);
        const QString leftName = leftItems.isEmpty() ? left : leftItems.first().displayName();
        const QString rightName = rightItems.isEmpty() ? right : rightItems.first().displayName();
        return leftName.localeAwareCompare(rightName) < 0;
    });

    int rowCount = 0;
    for (const auto& key : order) {
        const auto& list = groups.value(key);
        rowCount += list.size() > 1 ? list.size() + 1 : 1;
    }

    m_table->clearSpans();
    m_table->setRowCount(rowCount);

    int row = 0;
    int multiVersionGroups = 0;
    for (const auto& key : order) {
        auto list = groups.value(key);
        // 组内顺序：默认变体 → 原版 → 其它
        std::sort(list.begin(), list.end(), [](const LibraryItem& left, const LibraryItem& right) {
            if (left.variant.isDefault != right.variant.isDefault)
                return left.variant.isDefault;
            const bool leftOriginal = left.variant.kind == variant::Kind::Original;
            const bool rightOriginal = right.variant.kind == variant::Kind::Original;
            if (leftOriginal != rightOriginal)
                return leftOriginal;
            return left.displayName().localeAwareCompare(right.displayName()) < 0;
        });

        const bool grouped = list.size() > 1;
        if (grouped) {
            ++multiVersionGroups;
            const auto& head = list.first();
            const int installedInGroup = groupInstalled(list);

            auto* title = new QTableWidgetItem(head.displayName());
            QFont bold = title->font();
            bold.setBold(true);
            title->setFont(bold);
            title->setFlags(Qt::ItemIsEnabled);  // 不可选中：拖拽/双击/导入按钮都不认它
            title->setToolTip(tr("%1 的 %2 个语言版本").arg(head.displayName()).arg(list.size()));
            m_table->setItem(row, 0, title);

            auto* count = new QTableWidgetItem(tr("%1 个语言版本").arg(list.size()));
            count->setFlags(Qt::ItemIsEnabled);
            count->setForeground(ui::mutedTextColor());
            m_table->setItem(row, 2, count);

            // 分类一致才显示，否则写「混合」——别替玩家挑一个
            QSet<QString> categories;
            for (const auto& item : list)
                categories.insert(item.category);
            auto* categoryItem = new QTableWidgetItem(categories.size() == 1
                                                          ? m_library.categoryName(head.category)
                                                          : tr("混合"));
            categoryItem->setFlags(Qt::ItemIsEnabled);
            categoryItem->setForeground(ui::mutedTextColor());
            m_table->setItem(row, 3, categoryItem);

            auto* stateItem = new QTableWidgetItem(installedInGroup > 0 ? tr("已装 %1").arg(installedInGroup)
                                                                        : QStringLiteral("—"));
            stateItem->setFlags(Qt::ItemIsEnabled);
            if (installedInGroup > 0) {
                stateItem->setForeground(ui::confidenceColor(Confidence::High));
                stateItem->setToolTip(tr("这一组里已安装 %1 个版本").arg(installedInGroup));
            }
            m_table->setItem(row, 4, stateItem);
            ++row;
        }

        for (const auto& item : list) {
            const QString prefix = grouped ? QStringLiteral("└ ") : QString();
            auto* name = new QTableWidgetItem(prefix + item.displayName());
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

            auto* languageItem = new QTableWidgetItem(item.variantSummary());
            languageItem->setData(Qt::UserRole, item.file);
            QString languageTooltip = tr("%1 · %2").arg(variant::sourceLabel(item.variant.source),
                                                        variant::deliveryLabel(item.variant.delivery));
            if (!item.hallSlug.isEmpty()) {
                languageTooltip += QLatin1String("\n") + tr("关联大厅：%1%2")
                                                               .arg(item.hallSlug,
                                                                    item.hallVerified ? QString() : tr("（未确认）"));
            }
            languageItem->setToolTip(languageTooltip);
            m_table->setItem(row, 2, languageItem);

            auto* categoryItem = new QTableWidgetItem(m_library.categoryName(item.category));
            categoryItem->setData(Qt::UserRole, item.file);
            m_table->setItem(row, 3, categoryItem);

            auto* stateItem = new QTableWidgetItem(item.installed ? tr("已安装") : QStringLiteral("—"));
            stateItem->setData(Qt::UserRole, item.file);
            if (item.installed) {
                stateItem->setForeground(ui::confidenceColor(Confidence::High));
                stateItem->setToolTip(tr("已安装为实例：%1").arg(item.installedInstances.join(QStringLiteral("、"))));
            }
            m_table->setItem(row, 4, stateItem);
            ++row;
        }
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

    m_summary->setText(tr("%1 个包 · %2 张图 · %3 · 已安装 %4")
                           .arg(items.size())
                           .arg(order.size())
                           .arg(humanBytes(total))
                           .arg(installed));
    m_summary->setToolTip(tr("%1 个压缩包，归为 %2 张图（其中 %3 张有多个语言版本），共 %4，已安装 %5 个版本\n库目录：%6")
                              .arg(items.size())
                              .arg(order.size())
                              .arg(multiVersionGroups)
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

void MapLibraryPanel::showItemContextMenu(const QPoint& pos)
{
    auto* cell = m_table->itemAt(pos);
    if (cell == nullptr)
        return;
    auto* first = m_table->item(cell->row(), 0);
    const QString file = first != nullptr ? first->data(Qt::UserRole).toString() : QString();
    if (file.isEmpty())  // 组标题行
        return;

    const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();

    QMenu menu(this);
    QAction* importAction = menu.addAction(tr("导入这个版本"));
    QAction* openAction = menu.addAction(tr("打开所在目录"));
    menu.addSeparator();

    // 资源包没有世界，不能走导入 —— 只能挂到已装实例上
    const auto probe = MapPackAttach::inspect(path);
    QAction* attachAction = menu.addAction(tr("作为资源包挂到已装实例…"));
    attachAction->setEnabled(probe.valid);
    attachAction->setToolTip(probe.valid ? tr("放进实例的 resourcepacks/ 并启用；不建实例、不动存档")
                                        : probe.reason);

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == importAction) {
        emit importRequested(path);
    } else if (chosen == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } else if (chosen == attachAction) {
        attachResourcePack(file, path);
    }
}

void MapLibraryPanel::attachResourcePack(const QString& file, const QString& path)
{
    QStringList labels;
    QStringList roots;
    auto* instances = APPLICATION != nullptr ? APPLICATION->instances() : nullptr;
    if (instances != nullptr) {
        for (int i = 0; i < instances->count(); ++i) {
            auto* instance = instances->at(i);
            if (instance == nullptr)
                continue;
            const QString root = instance->instanceRoot();
            // 没启动过的实例没有 options.txt，挂上去也会在第一次启动时被整份重写
            if (!QFileInfo::exists(FS::PathCombine(root, QStringLiteral(".minecraft/options.txt"))))
                continue;
            labels << instance->name();
            roots << root;
        }
    }
    if (labels.isEmpty()) {
        QMessageBox::information(this, tr("没有可挂的实例"),
                                 tr("至少要有一个启动过一次的实例（它才会有 options.txt）。\n"
                                    "先把汉化整包当普通地图导入并进一次游戏，再回来挂资源包。"));
        return;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("挂到哪个实例"),
        tr("资源包会放进该实例的 resourcepacks/ 并自动启用。不建实例、不动存档。可以先挂到原版实例上对照。"),
        labels, 0, false, &ok);
    if (!ok)
        return;

    const QString instanceRoot = roots.at(labels.indexOf(chosen));
    const auto attached = MapPackAttach::attach(path, instanceRoot);
    if (!attached) {
        QMessageBox::warning(this, tr("挂载失败"), attached.error());
        return;
    }

    // 索引里标清楚"这是资源包、依附在哪个整包上"，分组才不会把它当成独立一张图
    for (const auto& item : m_library.items()) {
        if (item.file != file)
            continue;
        auto info = item.variant;
        info.delivery = variant::Delivery::ResourcePack;
        const auto meta = MapMeta::load(instanceRoot);
        const QString basedOn = QFileInfo(meta.archive.libraryCopy).fileName();
        info.basedOn = basedOn;
        m_library.setVariant(file, info);

        // 依附的原版若已关联大厅，就继承同一个 slug（同图不同语言）
        for (const auto& other : m_library.items()) {
            if (other.file == basedOn && !other.hallSlug.isEmpty()) {
                m_library.setHallLink(file, other.hallSlug, false);
                break;
            }
        }
        break;
    }

    QMessageBox::information(this, tr("已挂上"),
                             tr("已写入 %1 并启用。\n\n撤销：删掉该实例 resourcepacks 下的这个文件，\n"
                                "再把 options.txt 的 resourcePacks 里对应那一项去掉。")
                                 .arg(*attached));
    refresh();
}

}  // namespace maps
