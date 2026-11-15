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
#include "MapPackAttach.h"
#include "MapMeta.h"

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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

namespace maps {

namespace {

constexpr const char* kMimeType = "application/x-atlas-map-library-item";

/// 组标题行在 `Qt::UserRole + 1` 里存分组键（`Qt::UserRole` 留给真实条目的文件名）
constexpr int kGroupKeyRole = Qt::UserRole + 1;
/// 组内版本行（子行）标记：右键菜单据此走"只能改自己"的那一套
constexpr int kChildRowRole = Qt::UserRole + 2;

constexpr int kMarkerSize = 12;
/// 组内版本行的高度：**约半行**。子行只写"哪个语言版本"，不需要正常行的高度
constexpr int kChildRowHeight = 16;
constexpr int kDefaultRowHeight = 30;

/**
 * 「└」折号。
 *
 * 子行原来用「└ 」这个字符前缀，宽度随字体变，还会把文字推得比组标题更靠右；
 * 改成和折叠箭头同一个 12px 图标位，画一条竖线接一条横线 ——
 * 三行的文字起点逐像素对齐，缩进只由图形表达。
 */
QIcon elbowIcon(const QColor& color)
{
    QPixmap pixmap(kMarkerSize, kMarkerSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    // 竖线从上边缘落到中线，再向右折出去（视觉上就是「└」）
    painter.drawLine(QPointF(5.0, 0.5), QPointF(5.0, 8.0));
    painter.drawLine(QPointF(5.0, 8.0), QPointF(10.0, 8.0));
    return QIcon(pixmap);
}

/**
 * 折叠箭头。
 *
 * 自己画而不是用 `▾` / `▸` 字符：字符宽度随字体变，一旦标题用箭头、普通行用空格补齐，
 * 两种行的文字起点就差一两个像素；用固定 12px 的图标给**每一行**留出同一个位置才不会歪。
 */
QIcon arrowIcon(bool expanded, const QColor& color)
{
    QPixmap pixmap(kMarkerSize, kMarkerSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPolygonF triangle;
    if (expanded) {
        triangle << QPointF(2.5, 4.0) << QPointF(9.5, 4.0) << QPointF(6.0, 9.0);
    } else {
        triangle << QPointF(4.0, 2.5) << QPointF(9.0, 6.0) << QPointF(4.0, 9.5);
    }
    painter.drawPolygon(triangle);
    return QIcon(pixmap);
}

/// 普通行占位：透明但**占同样的宽度**，于是所有行的文字起点一致
QIcon blankIcon()
{
    static const QIcon icon = [] {
        QPixmap pixmap(kMarkerSize, kMarkerSize);
        pixmap.fill(Qt::transparent);
        return QIcon(pixmap);
    }();
    return icon;
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

/// 语言短标签：组内行只写「EN / 简中 / 繁中 / 官中」，一个词就说清是哪个版本
QString languageTag(const variant::Info& info)
{
    if (info.source == variant::Source::Author)
        return QStringLiteral("官中");
    switch (info.language) {
        case variant::Language::SimplifiedChinese:
            return QStringLiteral("简中");
        case variant::Language::TraditionalChinese:
            return QStringLiteral("繁中");
        case variant::Language::English:
            return QStringLiteral("EN");
        case variant::Language::Other:
            return QStringLiteral("其他");
        case variant::Language::Unknown:
            break;
    }
    // 没标语言的原版：写「原版」而不是猜 EN（盘灵古域这种中文原创图就是"没标、但是中文"）
    return info.kind == variant::Kind::Localized ? QStringLiteral("汉化") : QStringLiteral("原版");
}

/// 组内行的名字：「v1.0 EN」「v1.0 简中」。没有地图版本号就按 1.0 显示（不写盘）
QString versionLabel(const LibraryItem& item)
{
    QString version = item.mapVersion.trimmed();
    if (version.isEmpty())
        version = QStringLiteral("1.0");
    if (!version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.prepend(QLatin1Char('v'));

    QString label = QStringLiteral("%1 %2").arg(version, languageTag(item.variant));
    // 资源包不是一张图、只能挂到实例上，这个差别必须一眼可见
    if (item.variant.delivery == variant::Delivery::ResourcePack)
        label += QStringLiteral(" · 资源包");
    return label;
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
    catContainer->setFixedWidth(92);
    auto* catLayout = new QVBoxLayout(catContainer);
    catLayout->setContentsMargins(0, 0, 0, 0);
    catLayout->setSpacing(4);

    m_categories = new CategoryList(this);
    m_categories->setFixedWidth(92);
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
    m_table->setHorizontalHeaderLabels({ tr("地图"), tr("版本"), tr("语言 / 来源"), tr("分类"), tr("状态") });
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
    m_table->setToolTip(tr("拖动行到左侧分类即可归类；双击直接导入；点组标题展开/收起同图的不同语言版本；"
                           "右键组标题可整组操作，右键语言版本只能改它自己"));
    m_table->setIconSize(QSize(kMarkerSize, kMarkerSize));
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
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* first = m_table->item(row, 0);
        const QString key = first != nullptr ? first->data(kGroupKeyRole).toString() : QString();
        if (key.isEmpty())
            return;
        toggleGroup(key);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* first = m_table->item(row, 0);
        if (first != nullptr && !first->data(kGroupKeyRole).toString().isEmpty()) {
            // 双击 = 两次 cellClicked（已经切换了两次，等于没动）+ 一次这里：
            // 再切一次，双击就和单击一样只切换一个来回
            toggleGroup(first->data(kGroupKeyRole).toString());
            return;
        }
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

void MapLibraryPanel::toggleGroup(const QString& key)
{
    if (key.isEmpty())
        return;

    if (m_expandedGroups.contains(key)) {
        m_expandedGroups.remove(key);
        // rebuildTable 会"为了保住选中行"自动展开它所在的组，收起时先把选中清掉
        m_table->clearSelection();
        m_table->setCurrentCell(-1, -1);
    } else {
        m_expandedGroups.insert(key);
    }
    rebuildTable();
}

void MapLibraryPanel::rebuildTable()
{
    const auto items = m_showAll ? m_library.items() : m_library.items(m_filterCategoryId);

    QString previousFile;
    if (auto* current = m_table->currentItem())
        previousFile = current->data(Qt::UserRole).toString();

    // 改语言/改分类都会 refresh；收起状态下如果不自动展开，刚才选中的那一行会"消失"
    QString previousGroup;
    for (const auto& item : items) {
        if (!previousFile.isEmpty() && item.file == previousFile) {
            previousGroup = item.groupKey();
            break;
        }
    }

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

    const auto isExpanded = [this, &previousGroup](const QString& key) {
        return m_expandedGroups.contains(key) || key == previousGroup;
    };

    int rowCount = 0;
    for (const auto& key : order) {
        const auto& list = groups.value(key);
        if (list.size() <= 1) {
            ++rowCount;
        } else {
            ++rowCount;  // 组标题
            if (isExpanded(key))
                rowCount += list.size();
        }
    }

    // 先清空再重建：子行现在不写 1~3 列，若沿用旧行，上一轮 standalone 行的
    // 版本/语言/分类文本会留在原地，看起来像"子行也有版本号"
    m_table->setRowCount(0);
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
        const bool expanded = grouped && isExpanded(key);
        if (grouped) {
            ++multiVersionGroups;
            const auto& head = list.first();
            const int installedInGroup = groupInstalled(list);

            // 组标题优先用站上的标准名（`Bitstream` 而不是 `Bitstream - by Gibbs & Asometric`）；
            // 没关联大厅才退回索引里的名字，并把 ` - by 作者` 这类尾巴去掉。
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
            if (groupTitle.isEmpty())
                groupTitle = head.displayName();

            // 标题只写图名，版本数不再摆在标题里 —— 折叠时组下面根本看不到行，
            // "（2 个版本）"只是在解释一个看不见的东西。数量放 tooltip。
            auto* title = new QTableWidgetItem(groupTitle);
            title->setIcon(arrowIcon(expanded, ui::bodyTextColor()));
            // 组标题与"没有组的图"都用粗体，组内版本行不加粗 —— 层级靠字重与缩进，
            // 不靠颜色：一排灰字里只要有两条粗体，眼睛就知道那是"几张图"而不是"几个包"
            title->setFont(ui::cardTitleFont(title->font()));
            title->setFlags(Qt::ItemIsEnabled);  // 不可选中：拖拽/双击/导入按钮都不认它
            title->setData(kGroupKeyRole, key);
            qint64 groupBytes = 0;
            for (const auto& item : list)
                groupBytes += item.bytes;
            title->setToolTip(tr("%1\n%2 个语言版本 · 共 %3 · 已装 %4\n点一下%5；右键可整组操作")
                                  .arg(head.displayName())
                                  .arg(list.size())
                                  .arg(humanBytes(groupBytes))
                                  .arg(installedInGroup)
                                  .arg(expanded ? tr("收起") : tr("展开")));
            m_table->setItem(row, 0, title);
            m_table->setRowHeight(row, kDefaultRowHeight);

            // 组标题只写 **MC 版本**：地图版本一组可能有好几个，写哪一个都是偏心；
            // 但"这张图跑在什么版本上"是玩家扫列表时最想知道的一件事，所以留在这一行。
            QStringList versions;
            for (const auto& item : list) {
                if (!item.mcVersion.isEmpty() && !versions.contains(item.mcVersion))
                    versions << item.mcVersion;
            }
            std::sort(versions.begin(), versions.end(), [](const QString& left, const QString& right) {
                return DataVersionTable::instance().dataVersionFor(left) <
                       DataVersionTable::instance().dataVersionFor(right);
            });
            QString versionText = QStringLiteral("—");
            if (!versions.isEmpty()) {
                constexpr int kMaxInline = 2;
                versionText = versions.mid(0, kMaxInline).join(QStringLiteral(" / "));
                if (versions.size() > kMaxInline)
                    versionText += QStringLiteral(" …");
            }
            auto* groupVersion = new QTableWidgetItem(versionText);
            groupVersion->setFlags(Qt::ItemIsEnabled);
            groupVersion->setForeground(ui::mutedTextColor());
            groupVersion->setToolTip(versions.isEmpty()
                                         ? tr("组内没有解析出版本")
                                         : tr("组内各版本的 MC 版本：%1").arg(versions.join(QStringLiteral("、"))));
            m_table->setItem(row, 1, groupVersion);

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

        if (grouped && !expanded)
            continue;

        // 组内两行的标签可能撞车（两个都是「v1.0 EN」）——只有撞车时才补 MC 版本区分，
        // 不撞就一个字都不多写
        QHash<QString, int> labelCounts;
        if (grouped) {
            for (const auto& item : list)
                ++labelCounts[versionLabel(item)];
        }

        for (const auto& item : list) {
            auto* name = new QTableWidgetItem;
            if (grouped) {
                // 组标题已经写了图名，子行只写"这是哪个语言版本"：`v1.0 EN` / `v1.0 简中`
                QString label = versionLabel(item);
                if (labelCounts.value(label) > 1 && !item.mcVersion.isEmpty())
                    label += QStringLiteral(" · MC %1").arg(item.mcVersion);
                name->setText(label);
                name->setIcon(elbowIcon(ui::mutedTextColor()));
                name->setFont(ui::metaFont(name->font()));
                name->setForeground(ui::mutedTextColor());
                name->setData(kChildRowRole, true);
            } else {
                // 没有组的图 = 这张图只有它自己：与组标题同一层级，同样加粗
                name->setText(item.displayName());
                name->setIcon(blankIcon());
                name->setFont(ui::cardTitleFont(name->font()));
            }
            name->setData(Qt::UserRole, item.file);

            QString tooltip = item.path;
            if (!item.mapVersion.isEmpty())
                tooltip += QLatin1String("\n") + tr("地图版本：%1").arg(item.mapVersion);
            else
                tooltip += QLatin1String("\n") + tr("地图版本未标注，按 v1.0 显示");
            if (!item.variant.label.isEmpty())
                tooltip += QLatin1String("\n") + tr("文件名标记：%1").arg(item.variant.label);
            tooltip += QLatin1String("\n") + tr("%1 · %2 · MC %3")
                                              .arg(variant::languageLabel(item.variant.language),
                                                   variant::deliveryLabel(item.variant.delivery),
                                                   item.mcVersion.isEmpty() ? QStringLiteral("—") : item.mcVersion);
            if (!item.hallSlug.isEmpty()) {
                tooltip += QLatin1String("\n") + tr("关联大厅：%1%2")
                                                  .arg(item.hallSlug, item.hallVerified ? QString() : tr("（未确认）"));
            }
            if (!item.note.isEmpty())
                tooltip += QLatin1String("\n") + item.note;
            if (item.installed)
                tooltip += QLatin1String("\n") + tr("已安装：%1").arg(item.installedInstances.join(QStringLiteral("、")));
            name->setToolTip(tooltip);
            m_table->setItem(row, 0, name);

            if (!grouped) {
                // 没有组的行才需要这些列：子行的 MC 版本 / 分类 / 来源都在组标题与右键菜单里，
                // 一排四列重复信息只会让人以为"每行都是一张图"
                QString version = item.mcVersion;
                if (!item.mapVersion.isEmpty())
                    version += version.isEmpty() ? item.mapVersion : QStringLiteral(" · ") + item.mapVersion;

                auto* versionItem = new QTableWidgetItem(version.isEmpty() ? QStringLiteral("—") : version);
                versionItem->setData(Qt::UserRole, item.file);
                m_table->setItem(row, 1, versionItem);

                auto* languageItem = new QTableWidgetItem(item.variantSummary());
                languageItem->setData(Qt::UserRole, item.file);
                languageItem->setToolTip(tr("%1 · %2").arg(variant::sourceLabel(item.variant.source),
                                                           variant::deliveryLabel(item.variant.delivery)));
                m_table->setItem(row, 2, languageItem);

                auto* categoryItem = new QTableWidgetItem(m_library.categoryName(item.category));
                categoryItem->setData(Qt::UserRole, item.file);
                m_table->setItem(row, 3, categoryItem);
            } else {
                // 子行只写「版本 + 语言」，其余列留空。但**空列也要有 item**：
                // QTableWidget 只给有 item 的格子画选中底色，缺列时选中行会断成两截。
                for (int column = 1; column <= 3; ++column) {
                    auto* filler = new QTableWidgetItem;
                    filler->setData(Qt::UserRole, item.file);
                    filler->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    filler->setToolTip(tooltip);
                    m_table->setItem(row, column, filler);
                }
            }

            auto* stateItem = new QTableWidgetItem(item.installed ? tr("已安装") : QStringLiteral("—"));
            stateItem->setData(Qt::UserRole, item.file);
            if (item.installed) {
                stateItem->setForeground(ui::confidenceColor(Confidence::High));
                stateItem->setToolTip(tr("已安装为实例：%1").arg(item.installedInstances.join(QStringLiteral("、"))));
            }
            if (grouped)
                stateItem->setFont(ui::metaFont(stateItem->font()));
            m_table->setItem(row, 4, stateItem);

            // 组内版本行只占半行高：它承载的信息只有"哪个语言版本 + 装没装"
            m_table->setRowHeight(row, grouped ? kChildRowHeight : kDefaultRowHeight);
            ++row;
        }
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

    const QString groupKey = first->data(kGroupKeyRole).toString();
    if (!groupKey.isEmpty()) {
        showGroupContextMenu(groupKey, globalPos);
        return;
    }

    const QString file = first->data(Qt::UserRole).toString();
    if (file.isEmpty())
        return;

    if (first->data(kChildRowRole).toBool())
        showVersionContextMenu(file, globalPos);
    else
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

    const bool expanded = m_expandedGroups.contains(groupKey);
    QAction* toggle = menu.addAction(expanded ? tr("收起") : tr("展开"));
    connect(toggle, &QAction::triggered, this, [this, groupKey] { toggleGroup(groupKey); });

    // 不给"导入默认版本"：一组里哪个算默认是程序猜的，玩家要的是**自己挑一个**；
    // 两项并存时"默认"还会被当成推荐，点错就多建一个实例。
    QMenu* versions = menu.addMenu(tr("导入特定版本"));
    for (const auto& item : list) {
        const QString label = tr("%1 · MC %2%3").arg(versionLabel(item),
                                                      item.mcVersion.isEmpty() ? QStringLiteral("—") : item.mcVersion,
                                                      item.installed ? tr(" · 已安装") : QString());
        QAction* action = versions->addAction(label);
        const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), item.file).absoluteFilePath();
        connect(action, &QAction::triggered, this, [this, path] { emit importRequested(path); });
    }

    menu.addSeparator();

    // 整组改分类：组标题上只有一份分类，子行改分类没有意义（会立刻变「混合」）
    QMenu* categoryMenu = menu.addMenu(tr("整组归到类别"));
    for (const auto& category : m_library.categories()) {
        QAction* action = categoryMenu->addAction(category.name);
        action->setCheckable(true);
        action->setChecked(defaultItem->category == category.id);
        connect(action, &QAction::triggered, this,
                [this, groupKey, id = category.id] { applyCategoryToGroup(groupKey, id); });
    }

    if (defaultItem->hallSlug.isEmpty()) {
        QAction* link = menu.addAction(tr("关联到大厅地图…"));
        connect(link, &QAction::triggered, this, [this, files] { pickHallMap(files); });
    } else {
        QAction* unlink = menu.addAction(tr("解除大厅关联（整组）"));
        connect(unlink, &QAction::triggered, this, [this, files] {
            for (const auto& file : files)
                m_library.setHallLink(file, QString(), false);
        });
    }

    QAction* openAction = menu.addAction(tr("打开所在目录"));
    connect(openAction, &QAction::triggered, this, [this, defaultItem] {
        const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), defaultItem->file).absoluteFilePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });

    menu.exec(globalPos);
}

void MapLibraryPanel::showVersionContextMenu(const QString& file, const QPoint& globalPos)
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
    // 组内版本行只能改"它自己是什么"，不能改"它属于哪张图、归哪一类"——
    // 那两件事写在组标题上，子行改会让整组显示成「混合」
    auto* info = menu.addAction(tr("%1 · MC %2").arg(versionLabel(item),
                                                      item.mcVersion.isEmpty() ? QStringLiteral("—") : item.mcVersion));
    info->setEnabled(false);
    menu.addSeparator();

    QAction* importAction = menu.addAction(tr("导入这个版本"));
    QAction* versionAction = menu.addAction(tr("地图版本…"));
    QAction* variantAction = menu.addAction(tr("语言标签…"));
    QAction* openAction = menu.addAction(tr("打开所在目录"));

    QAction* attachAction = nullptr;
    if (item.variant.delivery == variant::Delivery::ResourcePack) {
        const auto probe = MapPackAttach::inspect(path);
        menu.addSeparator();
        attachAction = menu.addAction(tr("作为资源包挂到已装实例…"));
        attachAction->setEnabled(probe.valid);
        attachAction->setToolTip(probe.valid ? tr("放进实例的 resourcepacks/ 并启用；不建实例、不动存档") : probe.reason);
    }

    QAction* chosen = menu.exec(globalPos);
    if (chosen == importAction)
        emit importRequested(path);
    else if (chosen == openAction)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    else if (chosen == variantAction)
        editVariant(file);
    else if (chosen == versionAction)
        editMapVersion(file);
    else if (attachAction != nullptr && chosen == attachAction)
        attachResourcePack(file, path);
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
    const QString path = QFileInfo(QDir(MapLibrary::libraryDir()), file).absoluteFilePath();

    QMenu menu(this);
    QAction* importAction = menu.addAction(tr("导入这个版本"));
    QAction* openAction = menu.addAction(tr("打开所在目录"));
    menu.addSeparator();

    QAction* variantAction = menu.addAction(tr("语言标签…"));
    QAction* versionAction = menu.addAction(tr("地图版本…"));

    // 类别：直接在这里改，不用把行拖到左边
    QMenu* categoryMenu = menu.addMenu(tr("类别"));
    const QString currentCategory = [this, &file] {
        for (const auto& item : m_library.items()) {
            if (item.file == file)
                return item.category;
        }
        return QString();
    }();
    for (const auto& category : m_library.categories()) {
        QAction* action = categoryMenu->addAction(category.name);
        action->setCheckable(true);
        action->setChecked(category.id == currentCategory);
        connect(action, &QAction::triggered, this, [this, file, id = category.id] { applyCategory(file, id); });
    }

    // 所属主：这张包算到哪张图下面（空 = 独立成图）
    QMenu* groupMenu = menu.addMenu(tr("所属主"));
    QAction* standalone = groupMenu->addAction(tr("独立成图（不关联）"));
    standalone->setCheckable(true);
    standalone->setChecked([this, &file] {
        for (const auto& item : m_library.items())
            if (item.file == file)
                return item.hallSlug.isEmpty();
        return true;
    }());
    connect(standalone, &QAction::triggered, this, [this, file] { m_library.setHallLink(file, QString(), false); });

    groupMenu->addSeparator();
    // 现有图组：有大厅关联的按 slug 挂，没有的按英文名（本地分组）合
    QHash<QString, QString> slugNames;     // slug -> 显示名
    QHash<QString, QString> localNames;    // englishName -> 显示名
    for (const auto& item : m_library.items()) {
        if (!item.hallSlug.isEmpty()) {
            if (!slugNames.contains(item.hallSlug))
                slugNames.insert(item.hallSlug, item.displayName());
        } else if (!item.englishName.isEmpty()) {
            if (!localNames.contains(item.englishName))
                localNames.insert(item.englishName, item.displayName());
        }
    }

    QList<QString> slugs = slugNames.keys();
    std::sort(slugs.begin(), slugs.end(), [&slugNames](const QString& left, const QString& right) {
        return slugNames.value(left).localeAwareCompare(slugNames.value(right)) < 0;
    });
    for (const auto& slug : slugs) {
        QAction* action = groupMenu->addAction(tr("%1（%2）").arg(slugNames.value(slug), slug));
        action->setCheckable(true);
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
    groupMenu->addSeparator();
    QAction* searchHall = groupMenu->addAction(tr("从大厅搜索…"));
    connect(searchHall, &QAction::triggered, this, [this, file] { pickHallMap({ file }); });

    menu.addSeparator();

    // 资源包没有世界，不能走导入 —— 只能挂到已装实例上
    const auto probe = MapPackAttach::inspect(path);
    QAction* attachAction = menu.addAction(tr("作为资源包挂到已装实例…"));
    attachAction->setEnabled(probe.valid);
    attachAction->setToolTip(probe.valid ? tr("放进实例的 resourcepacks/ 并启用；不建实例、不动存档")
                                        : probe.reason);

    QAction* chosen = menu.exec(globalPos);
    if (chosen == importAction) {
        emit importRequested(path);
    } else if (chosen == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } else if (chosen == attachAction) {
        attachResourcePack(file, path);
    } else if (chosen == variantAction) {
        editVariant(file);
    } else if (chosen == versionAction) {
        editMapVersion(file);
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
                                 tr("先打开「添加实例 → CTM 地图」，点一次「更新数据」。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("关联到 CTM 地图"));
    dialog.resize(560, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(tr("按名字过滤（缓存里有 %1 张）").arg(snapshot.listRows.size()));
    auto* list = new QListWidget(&dialog);
    layout->addWidget(filter);
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    const auto rebuild = [list](const QJsonArray& rows, const QString& text) {
        list->clear();
        int shown = 0;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const QString name = row.value(QStringLiteral("name")).toString();
            const QString slug = row.value(QStringLiteral("slug")).toString();
            if (name.isEmpty() || slug.isEmpty())
                continue;
            if (!text.isEmpty() && !name.contains(text, Qt::CaseInsensitive) &&
                !slug.contains(text, Qt::CaseInsensitive))
                continue;

            auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2  ·  MC %3")
                                                 .arg(name, slug, row.value(QStringLiteral("version")).toString()));
            item->setData(Qt::UserRole, slug);
            list->addItem(item);
            if (++shown >= 200)  // 过滤为空时也别把 2287 行全塞进列表
                break;
        }
    };
    rebuild(snapshot.listRows, QString());

    connect(filter, &QLineEdit::textChanged, &dialog,
            [rebuild, rows = snapshot.listRows](const QString& text) { rebuild(rows, text); });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;
    auto* current = list->currentItem();
    if (current == nullptr)
        return;
    // 组标题上选的关联对整个组生效：同一张图的几个语言版本必须锚在同一个 slug 上，
    // 否则分组又会散开
    const QString slug = current->data(Qt::UserRole).toString();
    for (const auto& file : files)
        m_library.setHallLink(file, slug, false);
}

}  // namespace maps
