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
#include "DataVersionTable.h"
#include "FileSystem.h"
#include "HallCache.h"
#include "HallMatcher.h"
#include "MapPackAttach.h"
#include "SeriesNames.h"
#include "MapMeta.h"

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QProcess>
#include <QRegularExpression>
#include <QLineEdit>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPolygonF>
#include <QPixmap>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace maps {

namespace {

constexpr const char* kMimeType = "application/x-atlas-map-library-item";

/// 组标题行在 `Qt::UserRole + 1` 里存分组键（`Qt::UserRole` 留给真实条目的文件名）
constexpr int kGroupKeyRole = Qt::UserRole + 1;
/// 系列行：`Qt::UserRole + 3` 存系列名
constexpr int kSeriesRole = Qt::UserRole + 3;

/// 站上的系列名 → 组标题（`Mapwreck` → `Mapwreck Series`；本身已带 Series 的不重复加）
QString seriesDisplayName(const QString& series)
{
    if (series.endsWith(QLatin1String("series"), Qt::CaseInsensitive))
        return series;
    return series + QStringLiteral(" Series");
}

/// 系列缩写 + 期号：`Mapwreck` + 7 → `MW7`；`Untold Stories` + 0 → `US1`（缺号记作 1）
QString seriesAbbrev(const QString& series, int number)
{
    return maps::series::abbrevFor(series, number);
}

struct SeriesInfo {
    QString name;
    int number = 0;
};

/**
 * slug → 系列（站上的 `series` / `series_num`）。
 *
 * 全部来自大厅缓存：库里只存 slug，系列属于"站上的说法"，不往本地索引里抄。
 * 按缓存文件的 mtime 做进程内缓存 —— 这个表每建一次列表都要，别每次啃 3.6 MB JSON。
 */
const QHash<QString, SeriesInfo>& hallSeriesIndex()
{
    static QHash<QString, SeriesInfo> cached;
    static qint64 cachedStamp = -1;

    const QFileInfo file(HallCache::path());
    const qint64 stamp = file.exists() ? file.lastModified().toMSecsSinceEpoch() : 0;
    if (stamp == cachedStamp)
        return cached;

    cachedStamp = stamp;
    cached.clear();
    if (stamp == 0)
        return cached;

    const auto snapshot = HallCache::load();
    if (!snapshot.isValid())
        return cached;

    for (const auto& value : snapshot.listRows) {
        const auto row = value.toObject();
        const QString slug = row.value(QStringLiteral("slug")).toString();
        const QString series = row.value(QStringLiteral("series")).toString().trimmed();
        if (slug.isEmpty() || series.isEmpty())
            continue;
        SeriesInfo info;
        info.name = series;
        info.number = row.value(QStringLiteral("series_num")).toInt();
        cached.insert(slug, info);
    }
    return cached;
}

/// 一组里的图属于哪个系列（空 = 没有系列）
QString seriesNameFor(const QList<LibraryItem>& list, const QHash<QString, SeriesInfo>& index)
{
    for (const auto& item : list) {
        if (item.hallSlug.isEmpty())
            continue;
        const auto it = index.constFind(item.hallSlug);
        if (it != index.constEnd() && !it->name.isEmpty())
            return it->name;
    }
    return {};
}

/// 这一组里有没有中文版本（汉化或官中）—— 语言列只回答"中文还是英文"，不列具体来源
bool groupHasChinese(const QList<LibraryItem>& list)
{
    for (const auto& item : list) {
        if (item.variant.language == variant::Language::SimplifiedChinese ||
            item.variant.language == variant::Language::TraditionalChinese)
            return true;
    }
    return false;
}

/**
 * 行首图标位：**宽 12、高 = 行高**。
 *
 * 宽决定文字起点（系列行箭头、地图行折号、普通行占位都是这个宽度）；
 * 高必须等于行高，组内相邻两行的竖线才接得上 —— 12×12 的小图标会在行间留 9px 空隙，
 * 一组图看起来仍是几个断开的叶子。
 */
constexpr int kMarkerWidth = 12;
constexpr int kMarkerHeight = 30;
/// 组内版本行的高度：**约半行**。子行只写"哪个语言版本"，不需要正常行的高度
constexpr int kChildRowHeight = 16;
constexpr int kDefaultRowHeight = kMarkerHeight;
/// 左侧分类栏宽度：等宽字体下「MiniGame (  2)」正好放得下，再宽就白占表格的位置
constexpr int kCategoryBarWidth = 92;

/// 竖线在图标里的横坐标；折点取图标（= 行）的垂直中线，与文字基线对齐
constexpr qreal kMarkerStemX = 5.0;
constexpr qreal kMarkerArmEndX = 10.0;
constexpr qreal kMarkerMidY = kMarkerHeight / 2.0;

/**
 * 「└」折号：一组里的**最后一张**图。
 *
 * 竖线从本行顶边画到折点，表示"这一支到此为止"；不画到行底，下面的行不会连上来。
 * 与折叠箭头同一个宽度、同一条竖线位置 —— 系列行的箭头、地图行的折号落在同一列，
 * 文字起点才会逐像素对齐；不用「└ 」字符是因为宽度随字体变，对齐会歪。
 */
QPixmap elbowPixmap(const QColor& color)
{
    QPixmap pixmap(kMarkerWidth, kMarkerHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPointF(kMarkerStemX, 0.5), QPointF(kMarkerStemX, kMarkerMidY));
    painter.drawLine(QPointF(kMarkerStemX, kMarkerMidY), QPointF(kMarkerArmEndX, kMarkerMidY));
    return pixmap;
}

QIcon elbowIcon(const QColor& color)
{
    return QIcon(elbowPixmap(color));
}

/**
 * 「├」折号：同一个系列里**不是最后一张**的图。
 *
 * 与 `elbowPixmap` 只差竖线：这里一直画到本行底边，下一行的竖线接上来，一组图才连成
 * 一棵树；最后一张仍用「└」，表示这一支到此为止。
 */
QPixmap teePixmap(const QColor& color)
{
    QPixmap pixmap(kMarkerWidth, kMarkerHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPointF(kMarkerStemX, 0.0), QPointF(kMarkerStemX, kMarkerHeight));
    painter.drawLine(QPointF(kMarkerStemX, kMarkerMidY), QPointF(kMarkerArmEndX, kMarkerMidY));
    return pixmap;
}

/**
 * 折叠箭头。
 *
 * 自己画而不是用 `▾` / `▸` 字符：字符宽度随字体变，一旦标题用箭头、普通行用空格补齐，
 * 两种行的文字起点就差一两个像素；用固定 12px 的图标给**每一行**留出同一个位置才不会歪。
 * 画布与折号同高，三角形垂直居中 —— 三种图标必须同尺寸，否则 Qt 会缩放其中一种。
 */
QPixmap arrowPixmap(bool expanded, const QColor& color)
{
    QPixmap pixmap(kMarkerWidth, kMarkerHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal top = kMarkerMidY - 5.0;  // 12px 高的三角形在行内居中
    QPolygonF triangle;
    if (expanded) {
        triangle << QPointF(2.5, top + 2.0) << QPointF(9.5, top + 2.0) << QPointF(6.0, top + 7.0);
    } else {
        triangle << QPointF(4.0, top + 1.0) << QPointF(9.0, top + 4.5) << QPointF(4.0, top + 8.0);
    }
    painter.drawPolygon(triangle);
    return pixmap;
}

QIcon arrowIcon(bool expanded, const QColor& color)
{
    return QIcon(arrowPixmap(expanded, color));
}

/// 普通行占位：透明但**占同样的宽度**，于是所有行的文字起点一致
QPixmap blankPixmap()
{
    static const QPixmap pixmap = [] {
        QPixmap backing(kMarkerWidth, kMarkerHeight);
        backing.fill(Qt::transparent);
        return backing;
    }();
    return pixmap;
}

QIcon blankIcon()
{
    return QIcon(blankPixmap());
}

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

/// 等宽字体下的「显示格数」：汉字占两格、ASCII 占一格。
/// `leftJustified` 按**字符数**补空格，中英混排反而对不齐（「全部」2 个字符占 4 格），所以自己算。
int displayCells(const QString& text)
{
    int cells = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        const bool wide = (u >= 0x1100 && u <= 0x115F) || (u >= 0x2E80 && u <= 0xA4CF) || (u >= 0xAC00 && u <= 0xD7A3) ||
                          (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFE30 && u <= 0xFE4F) || (u >= 0xFF00 && u <= 0xFF60) ||
                          (u >= 0xFFE0 && u <= 0xFFE6);
        cells += wide ? 2 : 1;
    }
    return cells;
}

QString padToCells(QString text, int cells)
{
    const int need = cells - displayCells(text);
    if (need > 0)
        text += QString(need, QLatin1Char(' '));
    return text;
}

/**
 * 分类栏的等宽字体。
 *
 * 系统 `FixedFont` 在 Windows 上常常落到 Courier New —— 字形带衬线、8pt 下笔画发虚，
 * 一列中文夹英文尤其糊。这里按"正字面等宽"优先挑，并且**用整数号**：
 * DirectWrite 下分数号（8.5pt）要缩放采样，正是"看着糊"的来源。
 */
QFont categoryFont(const QFont& base)
{
    static const QStringList preferred{
        QStringLiteral("Cascadia Mono"),  QStringLiteral("Consolas"),        QStringLiteral("JetBrains Mono"),
        QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono"),
    };

    QFont font = base;
    const auto families = QFontDatabase::families();
    bool found = false;
    for (const auto& name : preferred) {
        if (families.contains(name, Qt::CaseInsensitive)) {
            font.setFamily(name);
            found = true;
            break;
        }
    }
    if (!found)
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    font.setPointSize(qMax(8, int(qRound(base.pointSizeF()))));
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
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

    // 左侧分类栏容器：固定宽度（8.1）。分类名都在 3~8 个字符内，132px 纯属浪费表格的地方，
    // 收到 92px：等宽字体下「MiniGame (  2)」正好放得下，右边表格多出 40px 给地图名。
    auto* catContainer = new QWidget(this);
    catContainer->setFixedWidth(kCategoryBarWidth);
    auto* catLayout = new QVBoxLayout(catContainer);
    catLayout->setContentsMargins(0, 0, 0, 0);
    catLayout->setSpacing(4);

    m_categories = new CategoryList(this);
    m_categories->setFixedWidth(kCategoryBarWidth);
    // 与右侧表格一样去掉原生边框：两者并排时，一个有线一个没线会显得歪
    m_categories->setFrameShape(QFrame::NoFrame);
    // 等宽字体 + 整数号：分类名长短不一，比例字体下每一行的计数永远对不齐（「CTM (9)」vs「MiniGame (2)」）
    m_categories->setFont(categoryFont(font()));
    m_categories->setAcceptDrops(true);
    m_categories->setDragDropMode(QAbstractItemView::DropOnly);
    m_categories->setSelectionMode(QAbstractItemView::SingleSelection);
    // 窄栏里放不下就省略，别让一条横向滚动条吃掉分类栏的高度
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setTextElideMode(Qt::ElideRight);
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
    m_addCategoryBtn->setFixedWidth(44);

    m_delCategoryBtn = new QPushButton(QStringLiteral("-"), this);
    m_delCategoryBtn->setToolTip(tr("删除选中分类（其下地图将移至 Other）"));
    m_delCategoryBtn->setFixedWidth(44);
    m_delCategoryBtn->setEnabled(false);

    catBtnLayout->addWidget(m_addCategoryBtn);
    catBtnLayout->addWidget(m_delCategoryBtn);
    catLayout->addLayout(catBtnLayout);

    body->addWidget(catContainer);

    m_table = new LibraryTable(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("地图"), tr("版本"), tr("语言"), tr("分类"), tr("状态") });
    m_table->verticalHeader()->setVisible(false);
    // 五列都可拖：玩家按自己的屏幕拖宽度，别替他固定死（Stretch / ResizeToContents 都拖不动）
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(56);
    // 列宽本身可拖，拖动结果记在设置里（不然每次重开对话框都回到默认值）
    restoreColumnState();
    m_columnSaveTimer = new QTimer(this);
    m_columnSaveTimer->setSingleShot(true);
    m_columnSaveTimer->setInterval(400);
    connect(m_columnSaveTimer, &QTimer::timeout, this, &MapLibraryPanel::saveColumnState);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, [this] {
        if (m_columnSaveTimer != nullptr)
            m_columnSaveTimer->start();
    });

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
    m_table->setToolTip(tr("拖动行到左侧分类即可归类；双击导入；点系列行展开/收起；右键改这张图"));
    // 图标位与行同高：折号的竖线要跨行接上（见 kMarkerHeight）
    m_table->setIconSize(QSize(kMarkerWidth, kMarkerHeight));
    ui::tuneTable(m_table, kDefaultRowHeight);
    // 子行要压到 16px：垂直表头的最小高度默认按字体算（约 20），不放开就会被静默钳回 20
    m_table->verticalHeader()->setMinimumSectionSize(kChildRowHeight - 2);
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
        if (first == nullptr)
            return;
        // 系列行没有分组键；地图行一律按"这张图"导入（多版本先让玩家挑）
        const QString groupKey = first->data(kGroupKeyRole).toString();
        if (!groupKey.isEmpty())
            importGroup(groupKey);
    });

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        bool actionable = false;
        for (const auto& index : m_table->selectionModel()->selectedRows()) {
            auto* first = m_table->item(index.row(), 0);
            if (first != nullptr && !first->data(kGroupKeyRole).toString().isEmpty()) {
                actionable = true;
                break;
            }
        }
        m_importButton->setEnabled(actionable);
    });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* first = m_table->item(row, 0);
        const QString series = first != nullptr ? first->data(kSeriesRole).toString() : QString();
        if (!series.isEmpty())
            toggleSeries(series);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* first = m_table->item(row, 0);
        if (first == nullptr)
            return;
        const QString series = first->data(kSeriesRole).toString();
        if (!series.isEmpty()) {
            // 双击 = 两次 cellClicked（已经切了两个来回）+ 一次这里：再切一次等于"只切一次"
            toggleSeries(series);
            return;
        }
        const QString groupKey = first->data(kGroupKeyRole).toString();
        if (!groupKey.isEmpty())
            importGroup(groupKey);
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

MapLibraryPanel::~MapLibraryPanel()
{
    // 关窗口时定时器可能还在等那 400ms：这里同步落一次，否则"拖完列宽马上关掉"会丢
    saveColumnState();
}

void MapLibraryPanel::refresh()
{
    m_library.reload();
    rebuildCategories();
    rebuildTable();
}

int MapLibraryPanel::autoLinkUnassigned()
{
    const int linked = m_library.autoLinkUnassigned();
    if (linked > 0)
        qInfo() << "Map library: 自动关联了" << linked << "个包到 CTM 条目（未确认）";
    return linked;
}

int MapLibraryPanel::normalizeNames(QString* error)
{
    const int changed = m_library.normalizeFileNames(error);
    if (changed > 0)
        refresh();
    return changed;
}

void MapLibraryPanel::restoreColumnState()
{
    auto* header = m_table->horizontalHeader();

    QString saved;
    if (APPLICATION != nullptr && APPLICATION->settings() != nullptr)
        saved = APPLICATION->settings()->get("MapLibraryColumnState").toString();

    if (!saved.isEmpty()) {
        const QByteArray state = QByteArray::fromBase64(saved.toLatin1());
        // restoreState 在列数/版本对不上时返回 false：那就退回默认值，别留下半套宽度
        if (!state.isEmpty() && header->restoreState(state))
            return;
    }

    // 默认宽度按导入对话框的常见宽度配（约 640px 内放得下）：五列加起来 616，
    // 再宽就会一进来就有横向滚动条 —— 玩家拖过一次之后按他的来（存在设置里）
    m_table->setColumnWidth(0, 260);  // 地图（名字最长，给宽一点）
    m_table->setColumnWidth(1, 110);  // 版本
    m_table->setColumnWidth(2, 110);  // 语言 / 来源
    m_table->setColumnWidth(3, 72);   // 分类
    m_table->setColumnWidth(4, 64);   // 状态
}

void MapLibraryPanel::saveColumnState()
{
    if (APPLICATION == nullptr || APPLICATION->settings() == nullptr)
        return;
    const QByteArray state = m_table->horizontalHeader()->saveState().toBase64();
    if (!state.isEmpty())
        APPLICATION->settings()->set("MapLibraryColumnState", QString::fromLatin1(state));
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

    // 等宽字体下按「格」对齐：名字左对齐到同一格，计数右对齐到三位 —— 比例字体里做不到，
    // 而这一栏本来就是"扫一眼看每类有几个"，对不齐就只能一行行读。
    // 「全部」写 `ALL`：它是这一列的"第 0 项"，用两个汉字的宽度会永远比下面的英文名宽一截。
    const QString allName = QStringLiteral("ALL");
    int nameCells = displayCells(allName);
    for (const auto& cat : m_library.categories())
        nameCells = qMax(nameCells, displayCells(cat.name));

    const auto makeLabel = [nameCells](const QString& name, int count) {
        return QStringLiteral("%1 (%2)").arg(padToCells(name, nameCells), QString::number(count).rightJustified(3));
    };

    auto* all = new QListWidgetItem(makeLabel(allName, m_library.items().size()), m_categories);
    all->setData(Qt::UserRole, QString());

    int selectedRow = 0;
    int rowIdx = 1;
    for (const auto& cat : m_library.categories()) {
        auto* item = new QListWidgetItem(makeLabel(cat.name, m_library.count(cat.id)), m_categories);
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

void MapLibraryPanel::toggleSeries(const QString& series)
{
    if (series.isEmpty())
        return;
    if (m_expandedSeries.contains(series))
        m_expandedSeries.remove(series);
    else
        m_expandedSeries.insert(series);

    // 收起时把选中清掉：被选中的地图行马上要消失，不清会留下一个空选中
    m_table->clearSelection();
    m_table->setCurrentCell(-1, -1);
    rebuildTable();
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

    /**
     * 行只有两层：**系列**（可折叠）→ **地图**。
     *
     * 不再铺开「原版 / 简中 / 繁中 / v1.0 / v1.2」这些版本行：那是**导入时**该做的选择
     * （`MapImportDialog` 本来就会问世界、关联与语言），摆在库里只会把一张图撑成四行，
     * 真正要找"我有哪几张图"时反而看不清。所以每张图一行，版本列只写 MC 版本，
     * 语言列只写"中文"或"英文"（二选一：图里只要有一版是中文就写中文，来源进 tooltip）。
     */
    const auto& seriesIndex = hallSeriesIndex();

    QStringList seriesOrder;
    QHash<QString, QStringList> seriesGroups;
    QStringList plainGroups;
    for (const auto& key : order) {
        const QString series = seriesNameFor(groups.value(key), seriesIndex);
        if (series.isEmpty()) {
            plainGroups << key;
            continue;
        }
        if (!seriesGroups.contains(series))
            seriesOrder << series;
        seriesGroups[series] << key;
    }

    const auto seriesInstalled = [&groups, &groupInstalled](const QStringList& members) {
        for (const auto& key : members) {
            if (groupInstalled(groups.value(key)) > 0)
                return true;
        }
        return false;
    };

    std::sort(seriesOrder.begin(), seriesOrder.end(), [&](const QString& left, const QString& right) {
        const bool leftInstalled = seriesInstalled(seriesGroups.value(left));
        const bool rightInstalled = seriesInstalled(seriesGroups.value(right));
        if (leftInstalled != rightInstalled)
            return leftInstalled;
        return left.localeAwareCompare(right) < 0;
    });

    int rowCount = plainGroups.size();
    for (const auto& series : seriesOrder) {
        ++rowCount;  // 系列标题
        if (m_expandedSeries.contains(series))
            rowCount += seriesGroups.value(series).size();
    }

    // 先清空再重建：上一轮的文本留在原地会看起来像"这一行也有那些列"
    m_table->setRowCount(0);
    m_table->clearSpans();
    m_table->setRowCount(rowCount);

    int row = 0;

    /// 一张图一行。`UserRole` 记主条目的文件名（兼容"选中行恢复"），`kGroupKeyRole` 记分组键
    const auto addMapRow = [&](int row, const QString& groupKey, const QList<LibraryItem>& rawList, const QString& series,
                               bool lastInSeries) {
        auto list = rawList;
        std::sort(list.begin(), list.end(), [](const LibraryItem& left, const LibraryItem& right) {
            if (left.variant.isDefault != right.variant.isDefault)
                return left.variant.isDefault;
            const bool leftOriginal = left.variant.kind == variant::Kind::Original;
            const bool rightOriginal = right.variant.kind == variant::Kind::Original;
            if (leftOriginal != rightOriginal)
                return leftOriginal;
            return left.displayName().localeAwareCompare(right.displayName()) < 0;
        });
        const auto& head = list.first();

        QString title = groupTitleFor(list);
        if (!series.isEmpty()) {
            const auto it = seriesIndex.constFind(head.hallSlug);
            const int number = it != seriesIndex.constEnd() ? it->number : 0;
            const QString abbrev = seriesAbbrev(series, number);
            if (!abbrev.isEmpty())
                title = QStringLiteral("[%1] %2").arg(abbrev, title);
        }

        qint64 bytes = 0;
        QStringList installedInstances;
        for (const auto& one : list) {
            bytes += one.bytes;
            installedInstances << one.installedInstances;
        }
        const int installed = groupInstalled(list);

        auto* name = new QTableWidgetItem(title);
        // 与系列行同一个图标位：同组中间几张画「├」，最后一张画「└」，一组图在视觉上
        // 连成一棵树；没有系列的图用空白图标占位，保证各行的"地图名"起点始终对齐
        QPixmap marker = blankPixmap();
        if (!series.isEmpty())
            marker = lastInSeries ? elbowPixmap(ui::mutedTextColor()) : teePixmap(ui::mutedTextColor());
        name->setData(Qt::DecorationRole, marker);
        name->setFont(ui::cardTitleFont(name->font()));
        name->setData(kGroupKeyRole, groupKey);
        name->setData(Qt::UserRole, head.file);
        QString tip = head.path;
        tip += QLatin1String("\n") + tr("%1 个语言版本 · 共 %2").arg(list.size()).arg(humanBytes(bytes));
        if (installed > 0)
            tip += QLatin1String("\n") + tr("已安装 %1 个").arg(installed);
        if (!series.isEmpty())
            tip += QLatin1String("\n") + tr("系列：%1").arg(seriesDisplayName(series));
        name->setToolTip(tip);
        m_table->setItem(row, 0, name);

        // 版本列只写 MC 版本：地图版本属于"导入时选哪个包"，不是列表要回答的问题
        QStringList versions;
        for (const auto& one : list) {
            if (!one.mcVersion.isEmpty() && !versions.contains(one.mcVersion))
                versions << one.mcVersion;
        }
        std::sort(versions.begin(), versions.end(), [](const QString& left, const QString& right) {
            return DataVersionTable::instance().dataVersionFor(left) < DataVersionTable::instance().dataVersionFor(right);
        });
        QString versionText = versions.isEmpty() ? QStringLiteral("—") : versions.mid(0, 2).join(QStringLiteral(" / "));
        if (versions.size() > 2)
            versionText += QStringLiteral(" …");
        auto* versionItem = new QTableWidgetItem(versionText);
        versionItem->setToolTip(versions.isEmpty() ? tr("没有解析出 MC 版本") : tr("MC 版本：%1").arg(versions.join(QStringLiteral("、"))));
        m_table->setItem(row, 1, versionItem);

        // 语言列只回答"有没有中文"：汉化、官中都算中文；来源与包形态进 tooltip
        auto* languageItem = new QTableWidgetItem(groupHasChinese(list) ? tr("中文") : tr("英文"));
        QStringList languageDetail;
        for (const auto& one : list)
            languageDetail << one.variantSummary();
        languageItem->setToolTip(languageDetail.join(QLatin1String("\n")));
        m_table->setItem(row, 2, languageItem);

        QSet<QString> categories;
        for (const auto& one : list)
            categories.insert(one.category);
        auto* categoryItem =
            new QTableWidgetItem(categories.size() == 1 ? m_library.categoryName(head.category) : tr("混合"));
        m_table->setItem(row, 3, categoryItem);

        auto* stateItem = new QTableWidgetItem(installed > 1 ? tr("已装 %1").arg(installed)
                                                             : (installed == 1 ? tr("已安装") : QStringLiteral("—")));
        if (installed > 0) {
            stateItem->setForeground(ui::confidenceColor(Confidence::High));
            stateItem->setToolTip(tr("已安装为实例：%1").arg(installedInstances.join(QStringLiteral("、"))));
        }
        m_table->setItem(row, 4, stateItem);
        m_table->setRowHeight(row, kDefaultRowHeight);
    };

    // ── 系列标题 + 系列内地图 ──
    for (const auto& series : seriesOrder) {
        const auto& members = seriesGroups.value(series);
        const bool expanded = m_expandedSeries.contains(series);

        int installedInSeries = 0;
        for (const auto& key : members)
            installedInSeries += groupInstalled(groups.value(key));

        const QString tip = tr("%1\n%2 张图 · 已装 %3\n点一下%4；右键可整组操作")
                                .arg(seriesDisplayName(series))
                                .arg(members.size())
                                .arg(installedInSeries)
                                .arg(expanded ? tr("收起") : tr("展开"));

        auto* title = new QTableWidgetItem(seriesDisplayName(series));
        title->setData(Qt::DecorationRole, arrowPixmap(expanded, ui::bodyTextColor()));
        title->setFont(ui::cardTitleFont(title->font()));
        title->setFlags(Qt::ItemIsEnabled);
        title->setData(kSeriesRole, series);
        title->setToolTip(tip);
        m_table->setItem(row, 0, title);
        m_table->setRowHeight(row, kDefaultRowHeight);

        /**
         * 系列行是**聚合行**，四列都要填满（只写"1 张"会让整张表看起来缺了半行），
         * 但版本与语言不能写具体值：一个系列常横跨好几个 MC 版本、也常有中英两版，
         * 写其中一两个会让人以为整个系列就是这个版本 / 只有这一种语言。
         * 所以两列都只写「~」，具体值进 tooltip —— 想看单张图的版本，展开就是。
         */
        QStringList seriesVersions;
        QSet<QString> seriesCategories;
        bool seriesHasZh = false;
        bool seriesHasEn = false;
        qint64 seriesBytes = 0;
        for (const auto& key : members) {
            for (const auto& item : groups.value(key)) {
                if (!item.mcVersion.isEmpty() && !seriesVersions.contains(item.mcVersion))
                    seriesVersions << item.mcVersion;
                seriesCategories.insert(item.category);
                if (item.variant.language == variant::Language::SimplifiedChinese ||
                    item.variant.language == variant::Language::TraditionalChinese)
                    seriesHasZh = true;
                else
                    seriesHasEn = true;
                seriesBytes += item.bytes;
            }
        }
        std::sort(seriesVersions.begin(), seriesVersions.end(), [](const QString& left, const QString& right) {
            return DataVersionTable::instance().dataVersionFor(left) < DataVersionTable::instance().dataVersionFor(right);
        });

        auto* seriesVersion = new QTableWidgetItem(seriesVersions.isEmpty() ? QStringLiteral("—") : QStringLiteral("~"));
        seriesVersion->setFlags(Qt::ItemIsEnabled);
        seriesVersion->setForeground(ui::mutedTextColor());
        seriesVersion->setToolTip(seriesVersions.isEmpty() ? tr("这个系列没有解析出 MC 版本")
                                                          : tr("系列内各图用到的 MC 版本：%1")
                                                                .arg(seriesVersions.join(QStringLiteral("、"))));
        m_table->setItem(row, 1, seriesVersion);

        auto* seriesLangItem = new QTableWidgetItem(seriesHasZh || seriesHasEn ? QStringLiteral("~") : QStringLiteral("—"));
        seriesLangItem->setFlags(Qt::ItemIsEnabled);
        seriesLangItem->setForeground(ui::mutedTextColor());
        QStringList languageKinds;
        if (seriesHasZh)
            languageKinds << tr("中文版本");
        if (seriesHasEn)
            languageKinds << tr("英文版本");
        seriesLangItem->setToolTip(languageKinds.isEmpty() ? tr("这个系列没有可判定的语言")
                                                           : tr("系列内含：%1").arg(languageKinds.join(QStringLiteral("、"))));
        m_table->setItem(row, 2, seriesLangItem);

        auto* seriesCategory = new QTableWidgetItem(seriesCategories.size() == 1
                                                        ? m_library.categoryName(*seriesCategories.constBegin())
                                                        : tr("混合"));
        seriesCategory->setFlags(Qt::ItemIsEnabled);
        seriesCategory->setForeground(ui::mutedTextColor());
        m_table->setItem(row, 3, seriesCategory);

        auto* seriesState = new QTableWidgetItem(installedInSeries > 0
                                                     ? tr("已装 %1 · %2 张").arg(installedInSeries).arg(members.size())
                                                     : tr("%1 张").arg(members.size()));
        seriesState->setFlags(Qt::ItemIsEnabled);
        seriesState->setForeground(installedInSeries > 0 ? ui::confidenceColor(Confidence::High) : ui::mutedTextColor());
        seriesState->setToolTip(tr("系列共 %1 张图 · %2 · 已装 %3")
                                    .arg(members.size())
                                    .arg(humanBytes(seriesBytes))
                                    .arg(installedInSeries));
        m_table->setItem(row, 4, seriesState);
        ++row;

        if (!expanded)
            continue;

        for (int index = 0; index < members.size(); ++index) {
            addMapRow(row, members.at(index), groups.value(members.at(index)), series, index + 1 == members.size());
            ++row;
        }
    }

    // ── 不属于任何系列的图，照旧一行一张 ──
    for (const auto& key : plainGroups) {
        addMapRow(row, key, groups.value(key), QString(), true);
        ++row;
    }

    if (!previousFile.isEmpty()) {
        for (int candidate = 0; candidate < m_table->rowCount(); ++candidate) {
            auto* first = m_table->item(candidate, 0);
            if (first != nullptr && first->data(Qt::UserRole).toString() == previousFile) {
                m_table->selectRow(candidate);
                m_table->setCurrentCell(candidate, 0);
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
    m_summary->setToolTip(tr("%1 个压缩包，归为 %2 张图、%3 个系列，共 %4，已安装 %5 个版本\n库目录：%6")
                              .arg(items.size())
                              .arg(order.size())
                              .arg(seriesOrder.size())
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

void MapLibraryPanel::applyCategoryToGroup(const QString& groupKey, const QString& categoryId)
{
    QStringList files;
    for (const auto& item : m_library.groupItems(groupKey))
        files << item.file;
    if (files.isEmpty())
        return;

    if (!m_library.setCategoryForItems(files, categoryId)) {
        QMessageBox::warning(this, tr("改分类失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
        return;
    }
    QTimer::singleShot(0, this, [this] {
        rebuildCategories();
        rebuildTable();
    });
}

QString MapLibraryPanel::groupTitleFor(const QList<LibraryItem>& list) const
{
    if (list.isEmpty())
        return {};

    // 改名是「整组写」的，所以组里任意一条带 title 就是这张图现在的名字；
    // 全都空着才回落到自动推断。这一条必须排在站上标准名之前 ——
    // 否则关联了大厅的组怎么改都改不动（站上的名字会一直盖住玩家的选择）。
    for (const auto& item : list) {
        if (!item.title.isEmpty())
            return item.title;
    }

    int headIndex = 0;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).variant.isDefault) {
            headIndex = i;
            break;
        }
    }
    const auto& head = list.at(headIndex);

    QString groupTitle = head.displayName();
    if (!head.hallSlug.isEmpty()) {
        const auto hallRow = HallCache::findListRow(head.hallSlug);
        const QString hallName = hallRow.value(QStringLiteral("name")).toString();
        if (!hallName.isEmpty())
            groupTitle = hallName;
    }
    static const QRegularExpression bySuffix(QStringLiteral("\\s*[-–—]\\s*by\\s+.*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    groupTitle.remove(bySuffix);
    return groupTitle.isEmpty() ? head.displayName() : groupTitle;
}

void MapLibraryPanel::renameGroup(const QString& groupKey, const QString& currentTitle)
{
    const auto list = m_library.groupItems(groupKey);
    if (list.isEmpty())
        return;

    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("重命名这张图"),
                                               tr("它在地图库里叫什么（留空 = 恢复自动推断）。\n"
                                                  "只改显示名：不动文件名、不动分组，同组的几个语言版本也不会被拆开。"),
                                               QLineEdit::Normal, currentTitle, &ok);
    if (!ok)
        return;

    QStringList files;
    for (const auto& item : list)
        files << item.file;
    if (!m_library.setTitleForItems(files, text)) {
        QMessageBox::warning(this, tr("重命名失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
    }
}

void MapLibraryPanel::renameItem(const QString& file)
{
    LibraryItem item;
    bool found = false;
    for (const auto& candidate : m_library.items()) {
        if (candidate.file == file) {
            item = candidate;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("重命名这张图"),
                                               tr("它在地图库里叫什么（留空 = 恢复自动推断）。\n"
                                                  "只改显示名：不动文件名，也不影响识别出来的版本与语言。"),
                                               QLineEdit::Normal, item.displayName(), &ok);
    if (!ok)
        return;
    if (!m_library.setTitle(file, text)) {
        QMessageBox::warning(this, tr("重命名失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
    }
}

void MapLibraryPanel::showItemContextMenu(const QPoint& pos)
{
    // `customContextMenuRequested` 给的坐标**已经是视口坐标**（和 itemAt / indexAt 同一套）。
    // 之前又按"表格坐标"映射了一次，于是整体上移一个表头（32px）：右键 A 行会弹出 A 上一行
    // 的菜单 —— 组标题与子行的菜单不同，表现出来就是"点了这个、管的是那个"。
    const QPoint viewportPos = pos;
    const QPoint globalPos = m_table->viewport()->mapToGlobal(viewportPos);

    // 用 indexAt 而不是 itemAt：组标题只有 0/3/4 三列有 item，右键点在「版本」列
    // 这种空格子上时 itemAt 会是 nullptr，菜单就"点了没反应"
    const QModelIndex index = m_table->indexAt(viewportPos);
    if (!index.isValid())
        return;
    auto* first = m_table->item(index.row(), 0);
    if (first == nullptr)
        return;

    // 系列行：只有展开/收起与打开目录，别把地图那一套操作套到系列上
    const QString series = first->data(kSeriesRole).toString();
    if (!series.isEmpty()) {
        showSeriesContextMenu(series, globalPos);
        return;
    }

    const QString groupKey = first->data(kGroupKeyRole).toString();
    if (!groupKey.isEmpty()) {
        showGroupContextMenu(groupKey, globalPos);
        return;
    }

    const QString file = first->data(Qt::UserRole).toString();
    if (file.isEmpty())
        return;

    showStandaloneMenu(file, globalPos);
}

void MapLibraryPanel::showGroupContextMenu(const QString& groupKey, const QPoint& globalPos)
{
    const auto list = m_library.groupItems(groupKey);
    if (list.isEmpty())
        return;

    // 默认版本：玩家标过 default 的就是它，否则按组内顺序（原版在前）
    const LibraryItem* defaultItem = &list.first();
    for (const auto& item : list) {
        if (item.variant.isDefault) {
            defaultItem = &item;
            break;
        }
    }

    int installed = 0;
    qint64 bytes = 0;
    QStringList files;
    for (const auto& item : list) {
        files << item.file;
        bytes += item.bytes;
        if (item.installed)
            ++installed;
    }

    QMenu menu(this);
    auto* info = menu.addAction(tr("%1 · %2 个版本 · %3 · 已装 %4")
                                    .arg(defaultItem->displayName())
                                    .arg(list.size())
                                    .arg(humanBytes(bytes))
                                    .arg(installed));
    info->setEnabled(false);
    menu.addSeparator();

    QAction* importAction = menu.addAction(tr("导入"));
    connect(importAction, &QAction::triggered, this, [this, groupKey] { importGroup(groupKey); });

    QAction* renameAction = menu.addAction(tr("重命名…"));
    connect(renameAction, &QAction::triggered, this,
            [this, groupKey, current = groupTitleFor(list)] { renameGroup(groupKey, current); });

    /**
     * 语言标签与地图版本是**每个包**的属性。一张图一行之后，组里的几个语言版本没有
     * 自己的行了，"独立图"那套菜单能改、组里这两项却够不着（11-22 改版漏的，[06 §113]）。
     * 组里只有一个包就直连（多数情况），两个以上先选**哪个包** —— 子菜单里写字**文件名**，
     * 不写 `v1.0 简中`：那个标签是"版本 + 语言"，同组的两个汉化包会完全相同（用户看到
     * 两个一模一样的条目，以为在选语言/版本，其实是在选包，[06 §115]）。
     */
    const auto addPerFileEditor = [&](const QString& label, void (MapLibraryPanel::*editor)(const QString&)) {
        if (list.size() == 1) {
            QAction* action = menu.addAction(label);
            connect(action, &QAction::triggered, this, [this, file = list.first().file, editor] { (this->*editor)(file); });
            return;
        }
        QMenu* perFile = menu.addMenu(label);
        for (const auto& item : list) {
            QAction* action = perFile->addAction(item.file);
            connect(action, &QAction::triggered, this, [this, file = item.file, editor] { (this->*editor)(file); });
        }
    };
    addPerFileEditor(tr("语言标签…"), &MapLibraryPanel::editVariant);
    addPerFileEditor(tr("地图版本…"), &MapLibraryPanel::editMapVersion);

    // 整组改分类：组标题上只有一份分类，子行改分类没有意义（会立刻变「混合」）
    QMenu* categoryMenu = menu.addMenu(tr("类别"));
    for (const auto& category : m_library.categories()) {
        QAction* action = categoryMenu->addAction(category.name);
        action->setCheckable(true);
        action->setChecked(defaultItem->category == category.id);
        connect(action, &QAction::triggered, this,
                [this, groupKey, id = category.id] { applyCategoryToGroup(groupKey, id); });
    }

    if (defaultItem->hallSlug.isEmpty()) {
        QAction* link = menu.addAction(tr("关联到 CTM 地图条目…"));
        connect(link, &QAction::triggered, this, [this, files] { pickHallMap(files); });
    } else {
        QAction* relink = menu.addAction(tr("更换 CTM 地图关联…"));
        connect(relink, &QAction::triggered, this, [this, files] { pickHallMap(files); });
        QAction* unlink = menu.addAction(tr("解除 CTM 地图关联"));
        connect(unlink, &QAction::triggered, this, [this, files] {
            for (const auto& file : files)
                m_library.setHallLink(file, QString(), false);
        });
    }

    menu.addSeparator();

    QAction* openAction = menu.addAction(tr("打开所在目录"));
    connect(openAction, &QAction::triggered, this, [this, defaultItem] {
        revealInExplorer(QFileInfo(QDir(MapLibrary::libraryDir()), defaultItem->file).absoluteFilePath());
    });

    QAction* deleteAction = menu.addAction(tr("删除全部版本…"));
    menu.insertSeparator(deleteAction);
    connect(deleteAction, &QAction::triggered, this, [this, files] { deleteMaps(files); });

    menu.exec(globalPos);
}

void MapLibraryPanel::editMapVersion(const QString& file)
{
    QString current;
    for (const auto& item : m_library.items()) {
        if (item.file == file) {
            current = item.mapVersion;
            break;
        }
    }

    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("地图版本"),
                                               tr("这张图自己的版本号（例如 1.0、1.2.3）。\n"
                                                  "留空 = 按 1.0 显示。"),
                                               QLineEdit::Normal,
                                               current.isEmpty() ? QStringLiteral("1.0") : current, &ok);
    if (!ok)
        return;
    if (!m_library.setMapVersion(file, text)) {
        QMessageBox::warning(this, tr("改版本失败"),
                             tr("无法写入地图库索引：%1").arg(QDir::toNativeSeparators(MapLibrary::indexPath())));
    }
}

void MapLibraryPanel::showStandaloneMenu(const QString& file, const QPoint& globalPos)
{
    LibraryItem item;
    bool found = false;
    for (const auto& candidate : m_library.items()) {
        if (candidate.file == file) {
            item = candidate;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();

    QMenu menu(this);
    QAction* importAction = menu.addAction(tr("导入"));
    menu.addSeparator();

    QAction* renameAction = menu.addAction(tr("重命名…"));
    QAction* variantAction = menu.addAction(tr("语言标签…"));
    QAction* versionAction = menu.addAction(tr("地图版本…"));

    // 类别：直接在这里改，不用把行拖到左边
    QMenu* categoryMenu = menu.addMenu(tr("类别"));
    for (const auto& category : m_library.categories()) {
        QAction* action = categoryMenu->addAction(category.name);
        action->setCheckable(true);
        action->setChecked(category.id == item.category);
        connect(action, &QAction::triggered, this, [this, file, id = category.id] { applyCategory(file, id); });
    }

    // 所属图：这张包算到哪张图下面（空 = 独立成图）
    QMenu* groupMenu = menu.addMenu(tr("所属图"));
    QAction* standalone = groupMenu->addAction(tr("独立成图（不关联）"));
    standalone->setCheckable(true);
    standalone->setChecked(item.hallSlug.isEmpty());
    connect(standalone, &QAction::triggered, this, [this, file] { m_library.setHallLink(file, QString(), false); });

    groupMenu->addSeparator();
    // 现有图组：有大厅关联的按 slug 挂，没有的按英文名（本地分组）合
    QHash<QString, QString> slugNames;     // slug -> 显示名
    QHash<QString, QString> localNames;    // englishName -> 显示名
    for (const auto& candidate : m_library.items()) {
        if (!candidate.hallSlug.isEmpty()) {
            if (!slugNames.contains(candidate.hallSlug))
                slugNames.insert(candidate.hallSlug, candidate.displayName());
        } else if (!candidate.englishName.isEmpty()) {
            if (!localNames.contains(candidate.englishName))
                localNames.insert(candidate.englishName, candidate.displayName());
        }
    }

    QList<QString> slugs = slugNames.keys();
    std::sort(slugs.begin(), slugs.end(), [&slugNames](const QString& left, const QString& right) {
        return slugNames.value(left).localeAwareCompare(slugNames.value(right)) < 0;
    });
    for (const auto& slug : slugs) {
        QAction* action = groupMenu->addAction(tr("%1（%2）").arg(slugNames.value(slug), slug));
        action->setCheckable(true);
        action->setChecked(slug == item.hallSlug);
        connect(action, &QAction::triggered, this, [this, file, slug] { m_library.setHallLink(file, slug, false); });
    }

    QList<QString> locals = localNames.keys();
    std::sort(locals.begin(), locals.end(), [&localNames](const QString& left, const QString& right) {
        return localNames.value(left).localeAwareCompare(localNames.value(right)) < 0;
    });
    for (const auto& name : locals) {
        QAction* action = groupMenu->addAction(tr("%1（本地分组）").arg(localNames.value(name)));
        connect(action, &QAction::triggered, this, [this, file, name] {
            // 本地分组的键是英文名：先解除大厅关联，再把英文名对齐，两条命令都走原子写
            m_library.setHallLink(file, QString(), false);
            m_library.setEnglishName(file, name);
        });
    }

    /**
     * CTM 地图站关联放在**顶层**，不再藏进「所属图」。
     *
     * 对 CTM 这类图来说，"它对应站上的哪一条"是最常做的一件事；
     * 关联之后子菜单里那几条「现有分组」依然可用（那是本地合并，不是站上关联）。
     */
    menu.addSeparator();
    QAction* hallLinkAction = menu.addAction(item.hallSlug.isEmpty() ? tr("关联到 CTM 地图条目…")
                                                                    : tr("更换 CTM 地图关联…"));
    connect(hallLinkAction, &QAction::triggered, this, [this, file] { pickHallMap({ file }); });
    if (!item.hallSlug.isEmpty()) {
        QAction* unlinkAction = menu.addAction(tr("解除 CTM 地图关联"));
        connect(unlinkAction, &QAction::triggered, this, [this, file] { m_library.setHallLink(file, QString(), false); });
    }

    menu.addSeparator();

    QAction* openAction = menu.addAction(tr("打开目录"));

    // 资源包没有世界，不能走导入 —— 只能挂到已装实例上
    const auto probe = MapPackAttach::inspect(path);
    QAction* attachAction = menu.addAction(tr("作为资源包挂到实例…"));
    attachAction->setEnabled(probe.valid);
    attachAction->setToolTip(probe.valid ? tr("放进实例的 resourcepacks/ 并启用；不建实例、不动存档")
                                        : probe.reason);

    QAction* deleteAction = menu.addAction(tr("删除…"));
    menu.insertSeparator(deleteAction);

    QAction* chosen = menu.exec(globalPos);
    if (chosen == importAction) {
        emit importRequested(path);
    } else if (chosen == openAction) {
        revealInExplorer(path);
    } else if (chosen == attachAction) {
        attachResourcePack(file, path);
    } else if (chosen == renameAction) {
        renameItem(file);
    } else if (chosen == variantAction) {
        editVariant(file);
    } else if (chosen == versionAction) {
        editMapVersion(file);
    } else if (chosen == deleteAction) {
        deleteMaps({ file });
    }
}

void MapLibraryPanel::deleteMaps(const QStringList& files)
{
    if (files.isEmpty())
        return;

    QStringList names;
    qint64 bytes = 0;
    bool anyInstalled = false;
    for (const auto& item : m_library.items()) {
        if (!files.contains(item.file))
            continue;
        names << item.file;
        bytes += item.bytes;
        anyInstalled = anyInstalled || item.installed;
    }
    if (names.isEmpty())
        return;

    // 清单超过 6 条就折叠，别让确认框变成一屏文件列表
    const QString listed = names.size() <= 6
                               ? names.join(QStringLiteral("\n"))
                               : names.mid(0, 6).join(QStringLiteral("\n")) + tr("\n… 另有 %1 个").arg(names.size() - 6);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("删除地图"));
    box.setText(tr("从库里删除 %1 个包（%2）？文件会一起删掉，不能撤销。").arg(names.size()).arg(humanBytes(bytes)));
    box.setInformativeText(listed);
    if (anyInstalled)
        box.setInformativeText(listed + tr("\n\n注意：其中有包已被实例使用，删除会被拒绝。"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    // 按钮用中文，别依赖 Qt 的默认翻译（中文 Windows 上没装 qt_zh_CN 就是英文）
    box.button(QMessageBox::Yes)->setText(tr("删除"));
    box.button(QMessageBox::Cancel)->setText(tr("取消"));
    if (box.exec() != QMessageBox::Yes)
        return;

    QString error;
    if (!m_library.removeItems(names, &error)) {
        QMessageBox::warning(this, tr("删除失败"), error);
        return;
    }
}

#ifdef Q_OS_WIN
namespace {

/**
 * 注册表某个键的**默认值**（空值名）。读不到返回空串。
 *
 * 用 `RegGetValueW` 而不是 QSettings：`Folder\shell` 下的默认值是**没有名字**的那个值，
 * QSettings 那套"用点号/Default 取默认值"的写法在注册表后端上不保证能取到。
 */
QString readRegDefault(HKEY root, const QString& subKey)
{
    wchar_t buffer[2048];
    DWORD bytes = sizeof(buffer);
    const LONG result = RegGetValueW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), nullptr, RRF_RT_REG_SZ, nullptr,
                                     buffer, &bytes);
    if (result != ERROR_SUCCESS)
        return {};
    const int length = static_cast<int>(bytes / sizeof(wchar_t)) - 1;  // bytes 含结尾的 \0
    return QString::fromWCharArray(buffer, qMax(0, length)).trimmed();
}

/// 这台机器"打开文件夹"用的是哪个动词：`HKCU\Software\Classes` 覆盖优先，其次 `HKCR`。
/// 装了 Directory Opus 的 Explorer Replacement 时是 `openindopus`，原装系统是 `open`。
QString folderOpenVerb()
{
    QString verb = readRegDefault(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\Folder\\shell"));
    if (verb.isEmpty())
        verb = readRegDefault(HKEY_CLASSES_ROOT, QStringLiteral("Folder\\shell"));
    if (verb.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return {};
    return verb;
}

/// 动词对应的命令行，例如 `"D:\…\dopusrt.exe" /nodde /idlist,%I,%L`
QString commandForVerb(const QString& verb)
{
    const QString key = QStringLiteral("Folder\\shell\\%1\\command").arg(verb);
    QString command = readRegDefault(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\") + key);
    if (command.isEmpty())
        command = readRegDefault(HKEY_CLASSES_ROOT, key);
    return command;
}

/// 命令行里的可执行文件（第一段，可能带引号）
QString executableFromCommand(const QString& command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const int end = trimmed.indexOf(QLatin1Char('"'), 1);
        if (end > 1)
            return trimmed.mid(1, end - 1);
    }
    return trimmed.section(QLatin1Char(' '), 0, 0);
}

/// 交给系统外壳：打开的文件夹并选中文件（Uses 资源管理器；不做任何命令行拼串）
bool revealWithShell(const QString& path)
{
    const PIDLIST_ABSOLUTE idl = ILCreateFromPathW(reinterpret_cast<PCWSTR>(path.utf16()));
    if (idl == nullptr)
        return false;
    const HRESULT hr = SHOpenFolderAndSelectItems(idl, 0, nullptr, 0);
    ILFree(idl);
    return SUCCEEDED(hr);
}

}  // namespace
#endif

void MapLibraryPanel::revealInExplorer(const QString& path)
{
#ifdef Q_OS_WIN
    const QFileInfo info(path);
    if (info.exists()) {
        /**
         * 「打开所在目录」要按**这台机器**的文件管理器走，不能硬编码 explorer.exe：
         * 装了 Directory Opus（Explorer Replacement）之后，`Folder\shell` 的默认动词是
         * `openindopus`，硬调资源管理器等于绕开用户的选择。
         *
         * 三种情况（[06 §114](06-变更记录.md) / [§115](06-变更记录.md)）：
         *   · 默认动词是 DOpus → `dopusrt.exe /cmd Go OPENCONTAINER PATH "<文件>"`；
         *   · 资源管理器 / 没有自定义动词 → `SHOpenFolderAndSelectItems`；
         *     旧的 `explorer.exe /select,<路径>` 在带空格的路径上会被 Qt 整体加引号，
         *     explorer 解析失败后退回「文档」—— 用户实际撞到的就是这一条；
         *   · 别的管理器（Total Commander 等）→ 交给外壳打开目录（不做选中，也不猜参数）。
         *
         * DOpus 这里**不能**写 `Go "<文件>"`：DOpus 把压缩包也当文件夹，直接 Go 一个 zip 会
         * 进到包里面（2026-11-23 用户实际撞到）。`OPENCONTAINER` 是 DOpus 官方的
         * "打开所在容器并选中该项"，文件 / 文件夹 / 压缩包一视同仁。
         */
        const QString verb = folderOpenVerb();
        const QString program = verb.isEmpty() ? QString() : executableFromCommand(commandForVerb(verb));

        if (program.endsWith(QLatin1String("dopusrt.exe"), Qt::CaseInsensitive)) {
            const QStringList args = { QStringLiteral("/cmd"),   QStringLiteral("Go"), QStringLiteral("OPENCONTAINER"),
                                       QStringLiteral("PATH"), QDir::toNativeSeparators(info.absoluteFilePath()) };
            if (QProcess::startDetached(program, args))
                return;
        } else if (program.isEmpty() || program.endsWith(QLatin1String("explorer.exe"), Qt::CaseInsensitive)) {
            if (revealWithShell(info.absoluteFilePath()))
                return;
        }
    }
#endif
    // 文件不在了、或上面几条都没成：退回"打开所在目录"，至少让人看到位置
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void MapLibraryPanel::importGroup(const QString& groupKey)
{
    const auto list = m_library.groupItems(groupKey);
    if (list.isEmpty())
        return;

    const auto pathOf = [](const LibraryItem& item) {
        return QFileInfo(QDir(MapLibrary::libraryDir()), item.file).absoluteFilePath();
    };

    // 只有一个包：直接走导入（版本/语言在导入对话框里还能改）
    if (list.size() == 1) {
        emit importRequested(pathOf(list.first()));
        return;
    }

    /**
     * 多个包：**在这里挑**，因为列表里已经不展示版本行了。
     * 挑完仍旧走 `MapImportDialog` —— 世界、加载器、关联都在那里确认。
     */
    QMenu menu(this);
    QAction* header = menu.addAction(tr("这张图有 %1 个版本").arg(list.size()));
    header->setEnabled(false);
    menu.addSeparator();
    for (const auto& item : list) {
        QString label = item.variantSummary();
        if (label.isEmpty())
            label = tr("版本");
        if (!item.mapVersion.isEmpty())
            label += QStringLiteral(" · ") + item.mapVersion;
        if (!item.mcVersion.isEmpty())
            label += QStringLiteral(" · MC ") + item.mcVersion;
        if (item.installed)
            label += tr(" · 已安装");
        QAction* action = menu.addAction(label);
        connect(action, &QAction::triggered, this, [this, path = pathOf(item)] { emit importRequested(path); });
    }
    menu.exec(QCursor::pos());
}

void MapLibraryPanel::showSeriesContextMenu(const QString& series, const QPoint& globalPos)
{
    QMenu menu(this);
    auto* info = menu.addAction(seriesDisplayName(series));
    info->setEnabled(false);
    menu.addSeparator();

    const bool expanded = m_expandedSeries.contains(series);
    QAction* toggle = menu.addAction(expanded ? tr("收起") : tr("展开"));
    connect(toggle, &QAction::triggered, this, [this, series] { toggleSeries(series); });

    QAction* open = menu.addAction(tr("打开地图库目录"));
    connect(open, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(MapLibrary::libraryDir()));
    });

    menu.exec(globalPos);
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

void MapLibraryPanel::editVariant(const QString& file)
{
    variant::Info info;
    bool found = false;
    for (const auto& item : m_library.items()) {
        if (item.file == file) {
            info = item.variant;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("语言标签"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto* language = new QComboBox(&dialog);
    language->addItem(tr("原版（未标注语言）"));
    language->addItem(tr("原版（英文）"));
    language->addItem(tr("简中汉化"));
    language->addItem(tr("繁中汉化"));
    language->addItem(tr("官中（作者发布）"));
    language->addItem(tr("其他语言"));

    auto* source = new QComboBox(&dialog);
    source->addItem(tr("来源未知"));
    source->addItem(tr("作者发布"));
    source->addItem(tr("社区汉化"));
    source->addItem(tr("自译"));

    // 回填当前值
    if (info.kind == variant::Kind::Localized) {
        if (info.language == variant::Language::TraditionalChinese)
            language->setCurrentIndex(3);
        else if (info.source == variant::Source::Author)
            language->setCurrentIndex(4);
        else if (info.language == variant::Language::Other)
            language->setCurrentIndex(5);
        else
            language->setCurrentIndex(2);
        source->setCurrentIndex(int(info.source));
    } else {
        language->setCurrentIndex(info.language == variant::Language::English ? 1 : 0);
    }
    source->setEnabled(info.kind == variant::Kind::Localized);

    form->addRow(tr("语言"), language);
    form->addRow(tr("来源"), source);

    auto* hint = new QLabel(tr("载体：%1\n所属主：%2")
                                .arg(variant::deliveryLabel(info.delivery),
                                     info.basedOn.isEmpty() ? tr("按分组键算") : info.basedOn),
                            &dialog);
    hint->setWordWrap(true);
    hint->setFont(ui::metaFont(hint->font()));
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    form->addRow(hint);

    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    // 官中 = 语言简中 + 来源作者，别再让玩家把来源改成"自译"
    connect(language, &QComboBox::currentIndexChanged, &dialog, [language, source](int index) {
        source->setEnabled(index >= 2 && index != 4);
        if (index == 4)
            source->setCurrentIndex(int(variant::Source::Author));
    });

    if (dialog.exec() != QDialog::Accepted)
        return;

    variant::Info updated;
    switch (language->currentIndex()) {
        case 0:
            updated.kind = variant::Kind::Original;
            updated.language = variant::Language::Unknown;
            break;
        case 1:
            updated.kind = variant::Kind::Original;
            updated.language = variant::Language::English;
            break;
        case 2:
            updated.kind = variant::Kind::Localized;
            updated.language = variant::Language::SimplifiedChinese;
            break;
        case 3:
            updated.kind = variant::Kind::Localized;
            updated.language = variant::Language::TraditionalChinese;
            break;
        case 4:
            updated.kind = variant::Kind::Localized;
            updated.language = variant::Language::SimplifiedChinese;
            updated.source = variant::Source::Author;
            break;
        default:
            updated.kind = variant::Kind::Localized;
            updated.language = variant::Language::Other;
            break;
    }
    if (updated.kind == variant::Kind::Localized && language->currentIndex() != 4)
        updated.source = variant::Source(source->currentIndex());

    // 只改语言与来源，载体/依附/默认这些别动
    updated.delivery = info.delivery;
    updated.basedOn = info.basedOn;
    updated.isDefault = info.isDefault;
    if (updated.kind == variant::Kind::Localized) {
        updated.label = info.label.isEmpty() ? variant::languageLabel(updated.language) : info.label;
        // 原来写的是别的语言，就别继续挂旧标签（「简中汉化v1.0」不能贴在繁中包上）
        if (info.kind == variant::Kind::Localized && info.language != updated.language)
            updated.label = variant::languageLabel(updated.language);
    }

    m_library.setVariant(file, updated);
}

void MapLibraryPanel::pickHallMap(const QStringList& files)
{
    if (files.isEmpty())
        return;

    const auto snapshot = HallCache::load();
    if (!snapshot.isValid()) {
        QMessageBox::information(this, tr("还没有大厅数据"),
                                 tr("先打开「添加实例 → CTM 地图」，点一次「更新数据」。\n"
                                    "关联要对着站上的真实条目挑，本地得先有那份清单。"));
        return;
    }

    // 本地这张图叫什么、判出来什么版本 —— 用来自动挑候选（与导入对话框同一套匹配器）
    LibraryItem primary;
    for (const auto& item : m_library.items()) {
        if (item.file == files.first()) {
            primary = item;
            break;
        }
    }

    HallMatchQuery query;
    query.englishName = primary.englishName;
    query.chineseName = primary.chineseName.isEmpty() ? primary.title : primary.chineseName;
    query.fileName = primary.file;
    query.mcVersion = primary.mcVersion;
    const auto suggestions = HallMatcher::suggest(query, 5);
    const QString currentSlug = primary.hallSlug;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("关联到 CTM 地图站条目"));
    dialog.resize(620, 520);
    auto* layout = new QVBoxLayout(&dialog);

    auto* hint = new QLabel(files.size() > 1 ? tr("整组 %1 个语言版本会一起锚到同一条目上。").arg(files.size())
                                             : tr("「%1」对应站上的哪一张？").arg(primary.displayName()),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(tr("按名字或 slug 过滤（缓存里有 %1 张）").arg(snapshot.listRows.size()));
    auto* list = new QListWidget(&dialog);
    layout->addWidget(filter);
    layout->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    /**
     * 列表 = 「推荐」+「全部条目」。
     *
     * 只给全部条目的话，玩家得在 2000+ 行里自己认名字（站上的名字常带 ` - by 作者`，
     * 而本地包名可能是中文）；只给推荐又会在匹配器失手时无路可走。两段都留着，
     * 推荐在上、并写清"为什么像"。
     */
    const auto rebuild = [list, suggestions](const QJsonArray& rows, const QString& text) {
        list->clear();
        QSet<QString> seen;
        const auto matches = [&text](const QString& name, const QString& slug) {
            return text.isEmpty() || name.contains(text, Qt::CaseInsensitive) || slug.contains(text, Qt::CaseInsensitive);
        };

        int recommended = 0;
        for (const auto& candidate : suggestions) {
            if (!matches(candidate.name, candidate.slug))
                continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("★ %1  ·  %2  ·  MC %3").arg(candidate.name, candidate.reason, candidate.version));
            item->setData(Qt::UserRole, candidate.slug);
            item->setToolTip(tr("站上名称：%1\nMC 版本：%2\nslug：%3\n为什么像：%4（%5）")
                                 .arg(candidate.name, candidate.version, candidate.slug, candidate.reason)
                                 .arg(candidate.score, 0, 'f', 2));
            list->addItem(item);
            seen.insert(candidate.slug);
            ++recommended;
        }
        if (recommended > 0) {
            auto* separator = new QListWidgetItem(tr("── 全部条目 ──"));
            separator->setFlags(Qt::NoItemFlags);
            list->addItem(separator);
        }

        int shown = 0;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const QString name = row.value(QStringLiteral("name")).toString();
            const QString slug = row.value(QStringLiteral("slug")).toString();
            if (name.isEmpty() || slug.isEmpty() || seen.contains(slug))
                continue;
            if (!matches(name, slug))
                continue;

            auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2  ·  MC %3")
                                                 .arg(name, slug, row.value(QStringLiteral("version")).toString()));
            item->setData(Qt::UserRole, slug);
            list->addItem(item);
            if (++shown >= 200)  // 过滤为空时也别把 2287 行全塞进列表
                break;
        }
    };

    // 初值就是把关键词填进过滤框的效果：先让人看到"像的"，但不清空全部
    rebuild(snapshot.listRows, QString());

    connect(filter, &QLineEdit::textChanged, &dialog,
            [rebuild, rows = snapshot.listRows](const QString& text) { rebuild(rows, text); });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto* okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false);
    connect(list, &QListWidget::currentItemChanged, &dialog, [okButton](QListWidgetItem* current, QListWidgetItem*) {
        okButton->setEnabled(current != nullptr && (current->flags() & Qt::ItemIsEnabled));
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 已关联的预选那一条。没关联时**只在推荐足够确定时**预选：
    // 第一名分数够高、且明显领先第二名 —— 和导入对话框同一条规则，避免手快点错就写进索引。
    for (int i = 0; i < list->count(); ++i) {
        if (!currentSlug.isEmpty() && list->item(i)->data(Qt::UserRole).toString() == currentSlug) {
            list->setCurrentRow(i);
            break;
        }
    }
    if (list->currentItem() == nullptr && !suggestions.isEmpty() && suggestions.first().score >= 0.9 &&
        (suggestions.size() == 1 || suggestions.first().score - suggestions.at(1).score >= 0.1) &&
        list->count() > 0) {
        list->setCurrentRow(0);
    }

    if (dialog.exec() != QDialog::Accepted)
        return;
    auto* current = list->currentItem();
    if (current == nullptr)
        return;

    // 组标题上选的关联对整个组生效：同一张图的几个语言版本必须锚在同一个 slug 上，
    // 否则分组又会散开。界面上点过就算人工确认（verified），自动匹配永远不写 true。
    const QString slug = current->data(Qt::UserRole).toString();
    if (slug.isEmpty())
        return;
    for (const auto& file : files)
        m_library.setHallLink(file, slug, true);
}

}  // namespace maps
