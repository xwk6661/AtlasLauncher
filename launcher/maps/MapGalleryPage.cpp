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
#include "MapGalleryPage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

#include "InstanceList.h"
#include "MapTheme.h"
#include "minecraft/MinecraftInstance.h"

namespace maps {
namespace {

constexpr int TILE_WIDTH = 200;
constexpr int COVER_WIDTH = 176;
constexpr int COVER_HEIGHT = 99;  // 16:9
/// 卡片总高：封面 + 标题 + 中文名 + 版本行（照 AIO 的 .item 四行）
constexpr int CARD_HEIGHT = COVER_HEIGHT + 74;

/// 路径 → 已缩放的封面。卡片会频繁重绘（选中/滚动），缩放在这里只做一次；
/// refresh() 开头清空，重装地图换了封面才不会继续显示旧图。
QHash<QString, QPixmap>& coverCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

QIcon coverFor(const MapEntry& entry)
{
    if (entry.coverPath.isEmpty())
        return QIcon::fromTheme(QStringLiteral("worlds"));

    auto& cache = coverCache();
    if (const auto it = cache.constFind(entry.coverPath); it != cache.constEnd())
        return QIcon(*it);

    QPixmap pixmap(entry.coverPath);
    if (pixmap.isNull())
        return QIcon::fromTheme(QStringLiteral("worlds"));

    const QPixmap scaled =
        pixmap.scaled(COVER_WIDTH, COVER_HEIGHT, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    cache.insert(entry.coverPath, scaled);
    return QIcon(scaled);
}

/**
 * 画廊卡片：照 AIO `itemOf()` 的四行版式 ——
 * 封面（右上角带评分角标）+ 标题 + 中文名（次要色）+ 版本·加载器。
 *
 * 为什么不用 QStandardItem 自带的"图标 + 一行文字"：那一行放不下三个信息，
 * 而 AIO 的卡片正是靠"中文名"和"评分"两行才看得出是地图库而不是文件列表。
 */
class MapCardDelegate : public QStyledItemDelegate {
   public:
    explicit MapCardDelegate(const QList<MapEntry>* entries, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_entries(entries)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return { TILE_WIDTH, CARD_HEIGHT };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const MapEntry* entry = find(index.data(Qt::UserRole).toString());
        if (entry == nullptr) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect card = option.rect.adjusted(4, 4, -4, -4);

        // 选中 / 悬停：细边框（AIO 的 .item.is-active）
        if (option.state & QStyle::State_Selected) {
            painter->setPen(QPen(ui::accentColor(), 1));
            painter->drawRoundedRect(card.adjusted(0, 0, -1, -1), 3, 3);
        }

        // 封面：水平居中，16:9
        const QRect coverRect(card.left() + (card.width() - COVER_WIDTH) / 2, card.top(), COVER_WIDTH, COVER_HEIGHT);
        const QPixmap cover = coverFor(*entry).pixmap(COVER_WIDTH, COVER_HEIGHT);
        painter->drawPixmap(coverRect, cover);

        // 评分角标：封面右上角（AIO 的 .item-rate 就在图上）
        if (entry->notes.rating > 0) {
            const QString text = QStringLiteral("%1").arg(entry->notes.rating / 2.0, 0, 'f', 1);
            QFont badgeFont = option.font;
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.9);
            painter->setFont(badgeFont);
            const QFontMetricsF metrics(badgeFont);
            const int width = static_cast<int>(metrics.horizontalAdvance(text)) + 10;
            const QRect badge(coverRect.right() - width - 4, coverRect.top() + 4, width, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 150));
            painter->drawRoundedRect(badge, 3, 3);
            painter->setPen(QColor(0xff, 0xd5, 0x4f));
            painter->drawText(badge, Qt::AlignCenter, text);
        }

        // 文本区
        int y = coverRect.bottom() + 6;
        const int textLeft = card.left() + 4;
        const int textWidth = card.width() - 8;

        QFont titleFont = option.font;
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(option.palette.color(QPalette::WindowText));
        const QFontMetrics titleMetrics(titleFont);
        painter->drawText(QRect(textLeft, y, textWidth, titleMetrics.height()), Qt::AlignHCenter,
                          titleMetrics.elidedText(entry->displayName, Qt::ElideRight, textWidth));
        y += titleMetrics.height() + 1;

        // 中文名：只在设置过时出现（AIO 的 .item-alias）
        if (!entry->notes.alias.isEmpty()) {
            QFont aliasFont = option.font;
            aliasFont.setPointSizeF(aliasFont.pointSizeF() * 0.92);
            painter->setFont(aliasFont);
            painter->setPen(ui::mutedTextColor());
            const QFontMetrics aliasMetrics(aliasFont);
            painter->drawText(QRect(textLeft, y, textWidth, aliasMetrics.height()), Qt::AlignHCenter,
                              aliasMetrics.elidedText(entry->notes.alias, Qt::ElideRight, textWidth));
            y += aliasMetrics.height();
        }

        // 版本 · 加载器
        QFont metaFont = option.font;
        metaFont.setPointSizeF(metaFont.pointSizeF() * 0.9);
        painter->setFont(metaFont);
        painter->setPen(ui::mutedTextColor());
        const QFontMetrics metaMetrics(metaFont);
        painter->drawText(QRect(textLeft, y, textWidth, metaMetrics.height()), Qt::AlignHCenter,
                          metaMetrics.elidedText(entry->versionBadge, Qt::ElideRight, textWidth));

        painter->restore();
    }

   private:
    const MapEntry* find(const QString& id) const
    {
        if (m_entries == nullptr)
            return nullptr;
        for (const auto& entry : *m_entries) {
            if (entry.instanceId == id)
                return &entry;
        }
        return nullptr;
    }

    const QList<MapEntry>* m_entries = nullptr;
};

}  // namespace

MapGalleryPage::MapGalleryPage(InstanceList* instances, QWidget* parent) : QWidget(parent), m_instances(instances)
{
    setAcceptDrops(true);
    buildUi();
    refresh();
}

namespace {

/// 拖进来的文件是否像地图包
bool looksLikeArchive(const QUrl& url)
{
    if (!url.isLocalFile())
        return false;
    static const QStringList suffixes{ QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
                                       QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("mcworld") };
    return suffixes.contains(QFileInfo(url.toLocalFile()).suffix().toLower());
}

}  // namespace

void MapGalleryPage::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const auto& url : event->mimeData()->urls()) {
        if (looksLikeArchive(url)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MapGalleryPage::dragMoveEvent(QDragMoveEvent* event)
{
    // 与 dragEnterEvent 保持一致：拖非地图文件时不该显示「可放置」
    if (!event->mimeData()->hasUrls())
        return;
    for (const auto& url : event->mimeData()->urls()) {
        if (looksLikeArchive(url)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MapGalleryPage::dropEvent(QDropEvent* event)
{
    for (const auto& url : event->mimeData()->urls()) {
        if (!looksLikeArchive(url))
            continue;
        event->acceptProposedAction();
        emit importRequested(url.toLocalFile());
        return;
    }
}

void MapGalleryPage::buildUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_view = new QListView(splitter);
    m_view->setViewMode(QListView::IconMode);
    m_view->setIconSize(QSize(COVER_WIDTH, COVER_HEIGHT));
    m_view->setGridSize(QSize(TILE_WIDTH, CARD_HEIGHT));
    m_view->setItemDelegate(new MapCardDelegate(&m_entries, m_view));
    m_view->setMouseTracking(true);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setWordWrap(true);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setFrameShape(QFrame::NoFrame);
    m_model = new QStandardItemModel(this);
    m_view->setModel(m_model);

    // 详情做成右侧窄栏（与实例模式的实例操作侧栏同位、同形态），
    // 不占中央：中央只留卡片网格
    auto* detail = new QWidget(splitter);
    detail->setFixedWidth(250);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(10, 10, 10, 10);

    m_cover = new QLabel(detail);
    m_cover->setFixedSize(230, 129);  // 16:9
    m_cover->setScaledContents(true);
    m_cover->setAlignment(Qt::AlignCenter);
    detailLayout->addWidget(m_cover);

    m_title = new QLabel(detail);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    detailLayout->addWidget(m_title);

    m_badge = new QLabel(detail);
    m_badge->setWordWrap(true);
    detailLayout->addWidget(m_badge);

    m_confidence = new QLabel(detail);
    m_confidence->setWordWrap(true);
    detailLayout->addWidget(m_confidence);

    m_stats = new QLabel(detail);
    m_stats->setWordWrap(true);
    detailLayout->addWidget(m_stats);

    m_evidence = new QLabel(detail);
    m_evidence->setWordWrap(true);
    m_evidence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_evidence);

    m_worldCombo = new QComboBox(detail);
    m_worldCombo->setVisible(false);
    detailLayout->addWidget(m_worldCombo);

    detailLayout->addStretch(1);

    m_launchButton = new QPushButton(tr("进入地图"), detail);
    m_launchButton->setMinimumHeight(34);
    m_openButton = new QPushButton(tr("打开实例"), detail);
    auto* importButton = new QPushButton(tr("导入地图…"), detail);

    detailLayout->addWidget(m_launchButton);
    detailLayout->addWidget(m_openButton);
    detailLayout->addWidget(importButton);

    splitter->addWidget(m_view);
    splitter->addWidget(detail);
    layout->addWidget(splitter);

    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MapGalleryPage::updateDetails);
    const auto requestLaunch = [this] {
        const QString id = selectedInstanceId();
        if (id.isEmpty())
            return;
        const QString world = m_worldCombo->isVisible() ? m_worldCombo->currentData().toString() : QString();
        emit launchRequested(id, world);
    };
    connect(m_view, &QListView::doubleClicked, this, requestLaunch);
    connect(m_launchButton, &QPushButton::clicked, this, requestLaunch);
    connect(m_openButton, &QPushButton::clicked, this, [this] {
        const QString id = selectedInstanceId();
        if (!id.isEmpty())
            emit instanceOpenRequested(id);
    });
    connect(importButton, &QPushButton::clicked, this, [this] { emit importRequested(QString()); });
}

void MapGalleryPage::refresh()
{
    const QString previous = selectedInstanceId();
    coverCache().clear();  // 重装地图会换封面，旧缩略图不能留着
    m_entries = MapRepository::collect(m_instances);

    m_model->clear();
    for (const auto& entry : m_entries) {
        auto* item = new QStandardItem(coverFor(entry), entry.displayName);
        item->setEditable(false);
        item->setToolTip(QStringLiteral("%1\n%2").arg(entry.displayName, entry.versionBadge));
        item->setData(entry.instanceId, Qt::UserRole);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_model->appendRow(item);
    }

    if (m_model->rowCount() == 0) {
        m_title->setText(tr("还没有地图"));
        m_badge->clear();
        m_confidence->clear();
        m_stats->setText(tr("把地图压缩包拖进来，或点下面的「导入地图…」。"));
        m_evidence->clear();
        m_cover->clear();
        m_worldCombo->clear();
        m_worldCombo->setVisible(false);
        m_launchButton->setEnabled(false);
        m_openButton->setEnabled(false);
        return;
    }

    int row = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).instanceId == previous) {
            row = i;
            break;
        }
    }
    m_view->setCurrentIndex(m_model->index(row, 0));
    updateDetails();
}

QString MapGalleryPage::selectedInstanceId() const
{
    const auto index = m_view->currentIndex();
    if (!index.isValid())
        return {};
    return index.data(Qt::UserRole).toString();
}

void MapGalleryPage::updateDetails()
{
    const QString id = selectedInstanceId();
    if (id.isEmpty()) {
        m_launchButton->setEnabled(false);
        m_openButton->setEnabled(false);
        return;
    }
    m_launchButton->setEnabled(true);
    m_openButton->setEnabled(true);

    for (const auto& entry : m_entries) {
        if (entry.instanceId != id)
            continue;

        m_title->setText(entry.displayName);
        m_badge->setText(entry.versionBadge);

        const auto& version = entry.meta.version;
        m_confidence->setText(QStringLiteral("<span style='color:%1;'>%2</span>")
                                  .arg(ui::confidenceColor(version.confidence).name(),
                                       tr("版本置信度：%1").arg(confidenceToString(version.confidence))));

        const qint64 minutes = entry.meta.stats.playSeconds / 60;
        const QString exit = entry.meta.stats.lastExit;
        const QString exitText = exit == QLatin1String("safe")       ? tr("正常落盘")
                                 : exit == QLatin1String("crash")    ? tr("异常（未确认落盘）")
                                                                     : tr("未判定");
        m_stats->setText(tr("游玩 %1 分钟 · 上次退出：%2").arg(minutes).arg(exitText));

        m_evidence->setText(version.evidence.isEmpty() ? tr("（无判定证据）")
                                                       : version.evidence.join(QStringLiteral("\n")));

        // 一包多世界：让玩家选进哪个（PvP 地图包常见）
        m_worldCombo->clear();
        for (const auto& world : entry.meta.worlds)
            m_worldCombo->addItem(world.folder, world.folder);
        m_worldCombo->setVisible(entry.meta.worlds.size() > 1);

        if (!entry.coverPath.isEmpty())
            m_cover->setPixmap(QPixmap(entry.coverPath));
        else
            m_cover->clear();
        break;
    }
}

}  // namespace maps
