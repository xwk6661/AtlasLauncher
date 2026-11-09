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
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
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
    /**
     * 必须自己造 QDrag。
     *
     * QTableWidget 默认的拖拽走 `QAbstractItemModel::mimeData()`（内部格式
     * `application/x-qabstractitemmodeldatalist`），覆写 `mimeData(items)` 根本不会被调用
     * —— 实测拖到分类上没有任何反应（2026-11-09）。
     */
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

    std::function<void(const QString&, MapCategory)> onDropFile;

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
        const int value = target->data(Qt::UserRole).toInt();
        if (value < 0) {                     // 「全部」不是分类，不接受投放
            event->ignore();
            return;
        }
        onDropFile(file, static_cast<MapCategory>(value));
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
    // QLabel 默认按整段文字给最小宽度，会把整个对话框撑宽（实测右侧「状态」列被裁掉）。
    // 让它可以被压缩，完整信息放 tooltip。
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

    m_categories = new CategoryList(this);
    m_categories->setMinimumWidth(170);
    m_categories->setMaximumWidth(220);
    m_categories->setAcceptDrops(true);
    m_categories->setDragDropMode(QAbstractItemView::DropOnly);
    m_categories->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categories->setToolTip(tr("把右边的地图拖到分类上即可归类"));
    static_cast<CategoryList*>(m_categories)->onDropFile = [this](const QString& file, MapCategory category) {
        applyCategory(file, category);
    };
    body->addWidget(m_categories);

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
        if (row < 0)
            return;
        const int value = m_categories->item(row)->data(Qt::UserRole).toInt();
        m_showAll = value < 0;
        if (!m_showAll)
            m_filter = static_cast<MapCategory>(value);
        rebuildTable();
    });

    connect(&m_library, &MapLibrary::changed, this, [this] {
        // 排队执行：changed 可能是从拖放/双击的栈里发出来的，同步重建会把正在拖的 item 删掉
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

void MapLibraryPanel::rebuildCategories()
{
    const int previous = m_categories->currentRow();
    QSignalBlocker blocker(m_categories);
    m_categories->clear();

    auto* all = new QListWidgetItem(tr("全部（%1）").arg(m_library.items().size()), m_categories);
    all->setData(Qt::UserRole, -1);

    for (auto category : allCategories()) {
        auto* item = new QListWidgetItem(tr("%1（%2）").arg(categoryLabel(category)).arg(m_library.count(category)),
                                        m_categories);
        item->setData(Qt::UserRole, static_cast<int>(category));
    }

    // 尽量保持原来的选择
    int row = previous;
    if (row < 0 || row >= m_categories->count())
        row = 0;
    m_categories->setCurrentRow(row);
}

void MapLibraryPanel::rebuildTable()
{
    const auto items = m_showAll ? m_library.items() : m_library.items(m_filter);

    // 重建会清掉选择；把之前选中的文件记住，重建后按文件名选回去
    // （否则拖完一次再想拖第二次，要先用鼠标重新点一下这一行）
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

        auto* categoryItem = new QTableWidgetItem(categoryLabel(categoryFromId(item.category)));
        categoryItem->setData(Qt::UserRole, item.file);
        m_table->setItem(row, 2, categoryItem);

        auto* stateItem = new QTableWidgetItem(item.installed ? tr("已安装") : QStringLiteral("—"));
        stateItem->setData(Qt::UserRole, item.file);
        if (item.installed) {
            // 用主题里的"高置信度绿"，不写死色号（深色主题下 0x2E7D32 只有 1.6:1 对比度）
            stateItem->setForeground(ui::confidenceColor(Confidence::High));
            stateItem->setToolTip(tr("已安装为实例：%1").arg(item.installedInstances.join(QStringLiteral("、"))));
        }
        m_table->setItem(row, 3, stateItem);
    }

    // 把选择还原到同一个文件（重建会把 item 全换掉）
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

void MapLibraryPanel::applyCategory(const QString& file, MapCategory category)
{
    const bool ok = m_library.setCategory(file, category);
    if (!ok) {
        QMessageBox::warning(this, tr("改分类失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
    }
    /**
     * **必须延后到下一轮事件循环**。
     *
     * 这个函数是从分类列表的 dropEvent 里进来的，此刻 QDrag::exec() 还在栈上，
     * 拖拽源就是右边这张表；如果在这里同步 rebuildTable()，会把正在被拖的 item 删掉
     * ——实测直接崩掉整个启动器（2026-11-09）。
     */
    QTimer::singleShot(0, this, [this] {
        rebuildCategories();
        rebuildTable();
    });
}

}  // namespace maps
