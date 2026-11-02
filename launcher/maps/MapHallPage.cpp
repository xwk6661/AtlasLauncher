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
#include "MapHallPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QUrl>
#include <QPainter>
#include <QLinearGradient>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "HallAPI.h"
#include "MapCoverLoader.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "MapArchiveScanner.h"
#include "MapImportDialog.h"
#include "MapHallLink.h"
#include "MapImportTask.h"
#include "MapMeta.h"
#include "MapNotes.h"
#include "minecraft/MinecraftInstance.h"
#include "MapSettingsDialog.h"
#include "MapTheme.h"
#include "MapReviewCard.h"
#include "MapTextUtils.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"

namespace maps {
namespace {

constexpr int ROW_COVER_W = 132;  // AIO .hall-row-cover
constexpr int ROW_COVER_H = 74;
constexpr int DETAIL_COVER_W = 96;  // 头部缩略图，不再是 260×150 的大图
constexpr int DETAIL_COVER_H = 54;
/// 评论一页几条：够填满一屏，又不至于一次拉一堆
constexpr int REVIEWS_PER_PAGE = 6;

/**
 * 占位封面：封面图挂在被阻断的主机上（`*.supabase.co`）或者开了防盗链
 * （`ctmrepository.com` 连浏览器以外的客户端一律 403），Qt 取不到字节。
 * 与其留一块空白，不如按标题画一个稳定的色块 + 首字母 —— 至少列表不"爛"。
 */
QPixmap makePlaceholderCover(const QString& title, int width, int height)
{
    return MapCoverLoader::placeholderCover(title, width, height);
}


/**
 * 时长：≥1 小时就换成小时（实现挪到 MapTextUtils，评论卡片也要用同一套）。
 *
 * 站上很多图是「365.2 分钟」这种数字 —— 玩家得自己除 60。60 分钟以下仍用分钟，
 * 因为「1.3 小时」反而不如「78 分钟」直观。
 */
QString formatLength(int minutes)
{
    return text::formatPlaytime(minutes);
}

/**
 * 难度 / 长度在站上是英文枚举（`medium` / `very hard` / `very long`），
 * 直接塞进下拉框不友好；显示用中文，`userData` 仍保留原值给筛选用。
 */
QString difficultyLabel(const QString& value)
{
    if (value == QLatin1String("very easy"))
        return QObject::tr("非常简单");
    if (value == QLatin1String("easy"))
        return QObject::tr("简单");
    if (value == QLatin1String("medium"))
        return QObject::tr("中等");
    if (value == QLatin1String("hard"))
        return QObject::tr("困难");
    if (value == QLatin1String("very hard"))
        return QObject::tr("极难");
    return value;
}

QString lengthLabel(const QString& value)
{
    if (value == QLatin1String("short"))
        return QObject::tr("短");
    if (value == QLatin1String("medium"))
        return QObject::tr("中");
    if (value == QLatin1String("long"))
        return QObject::tr("长");
    if (value == QLatin1String("very long"))
        return QObject::tr("超长");
    return value;
}

/// 长度按「短 → 中 → 长 → 超长」排，别按字母序（字母序是 long/short/very long，读着别扭）
int lengthRank(const QString& value)
{
    static const QStringList order = { QStringLiteral("short"), QStringLiteral("medium"), QStringLiteral("long"),
                                       QStringLiteral("very long") };
    const int index = order.indexOf(value);
    return index < 0 ? order.size() : index;
}


/// 列表里的行：照 AIO `.hall-row` 的四栏（封面 / 标题+chip+一句话 / 数据 / 动作）
class HallRow : public QWidget {
   public:
    explicit HallRow(const HallItem& item, QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(9, 8, 9, 8);
        layout->setSpacing(12);

        m_cover = new QLabel(this);
        m_cover->setFixedSize(ROW_COVER_W, ROW_COVER_H);
        m_cover->setAlignment(Qt::AlignCenter);
        // 先摆占位图：封面多半取不到，空着比"难看"更糟
        m_cover->setPixmap(makePlaceholderCover(item.title, ROW_COVER_W, ROW_COVER_H));
        layout->addWidget(m_cover);

        auto* text = new QVBoxLayout();
        text->setSpacing(3);

        // 标题：AIO 用 fs-lg + 强字重；这里同样加粗、不换行（过长的省略）
        auto* title = new QLabel(item.title, this);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.05);
        title->setFont(titleFont);
        title->setWordWrap(false);
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        text->addWidget(title);

        // chip 行：系列 / 版本 / 难度 / 时长（AIO 的 .hall-chip：细边框、不填充）
        QStringList chips;
        if (!item.series.isEmpty())
            chips.append(item.series);
        if (!item.version.isEmpty())
            chips.append(item.version);
        if (!item.difficulty.isEmpty())
            chips.append(item.difficulty);
        if (!item.length.isEmpty())
            chips.append(item.length);

        if (!chips.isEmpty()) {
            auto* chipRow = new QHBoxLayout();
            chipRow->setContentsMargins(0, 0, 0, 0);
            chipRow->setSpacing(4);
            for (const auto& chipText : chips) {
                auto* chip = new QLabel(chipText, this);
                chip->setStyleSheet(QStringLiteral(
                    "border: 1px solid rgba(127,127,127,70); border-radius: 2px; padding: 0px 4px; font-size: 11px;"));
                chip->setEnabled(false);
                chipRow->addWidget(chip);
            }
            chipRow->addStretch(1);
            text->addLayout(chipRow);
        }

        // 数据行：★评分 · 平均时长 · 游玩人数（AIO 的 .hall-row-stats）
        QStringList stats;
        if (item.rating >= 0)
            stats.append(QStringLiteral("★ %1").arg(item.rating, 0, 'f', 1));
        if (item.minutes > 0)
            stats.append(formatLength(item.minutes));
        if (item.plays > 0)
            stats.append(QStringLiteral("%1 人玩过").arg(item.plays));
        if (!stats.isEmpty()) {
            auto* statsLabel = new QLabel(stats.join(QStringLiteral("  ·  ")), this);
            statsLabel->setEnabled(false);
            text->addWidget(statsLabel);
        }

        if (!item.subtitle.isEmpty()) {
            auto* blurb = new QLabel(item.subtitle, this);
            blurb->setWordWrap(false);
            blurb->setEnabled(false);
            text->addWidget(blurb);
        }
        text->addStretch(1);
        layout->addLayout(text, 1);

        // 点击要落到 QListWidget 上，行内控件不抢选中
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setCover(const QPixmap& source)
    {
        if (source.isNull())
            return;
        const QPixmap scaled =
            source.scaled(ROW_COVER_W, ROW_COVER_H, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        m_cover->setPixmap(scaled.copy(0, 0, ROW_COVER_W, ROW_COVER_H));
    }

   private:
    QLabel* m_cover = nullptr;
};

QPixmap cropTo(const QPixmap& source, int width, int height)
{
    if (source.isNull())
        return {};
    const QPixmap scaled = source.scaled(width, height, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy(0, 0, width, height);
}

}  // namespace

MapHallPage::MapHallPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    buildUi();

    m_api = new HallAPI(this);
    // 设置项里可以换成等价反代（见 docs/map-launcher 02 §3.6）
    m_api->setEndpoint(APPLICATION->settings()->get("MapHallEndpoint").toString());

    connect(m_api, &HallAPI::searchDone, this, &MapHallPage::onSearchDone);
    connect(m_api, &HallAPI::detailDone, this, &MapHallPage::onDetail);
    connect(m_api, &HallAPI::failed, this, &MapHallPage::onFailed);
    connect(m_api, &HallAPI::facetsDone, this, &MapHallPage::onFacets);
    connect(m_api, &HallAPI::reviewsDone, this, &MapHallPage::onReviews);
    connect(m_api, &HallAPI::userNamesDone, this, &MapHallPage::onUserNames);
    connect(m_api, &HallAPI::syncProgress, this, [this](int step, int total, const QString& text) {
        m_status->setText(tr("%1（%2/%3）").arg(text).arg(step).arg(total));
    });
    connect(m_api, &HallAPI::syncDone, this, [this](bool ok, const QString& message) {
        m_syncing = false;
        m_syncButton->setEnabled(true);
        m_status->setText(message);
        updateLibraryHint();
        if (ok) {
            startSearch();
            m_api->facets();
        }
    });
    connect(m_syncButton, &QPushButton::clicked, this, &MapHallPage::syncLibrary);
    connect(m_api, &HallAPI::probeDone, this, [this](bool ok, int elapsedMs, const QString& detail) {
        m_probeResult->setVisible(true);
        m_probeResult->setEnabled(true);
        m_probeResult->setText(QStringLiteral("%1 %2 ms · %3")
                                   .arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"))
                                   .arg(elapsedMs)
                                   .arg(detail));
        m_probeResult->setStyleSheet(ok ? QStringLiteral("color: %1;").arg(ui::confidenceColor(Confidence::High).name())
                                        : QStringLiteral("color: %1;").arg(ui::noticeColorName()));
    });

    /**
     * 封面统一走 MapCoverLoader：直连拿不到就自动换浏览器通道（SNI 阻断只有
     * Chromium 的 ECH 能过），成功一次之后同类主机都直接走浏览器 + 磁盘缓存。
     */
    auto* covers = MapCoverLoader::instance();
    covers->setNetwork(m_api->network());
    covers->setTransportMode(APPLICATION->settings()->get("MapHallTransport").toString());
    connect(covers, &MapCoverLoader::coverReady, this, [this](const QString& url, const QImage& image) {
        if (!m_coverPending.contains(url))
            return;
        m_coverPending.remove(url);
        applyCover(url, QPixmap::fromImage(image));
    });
    connect(covers, &MapCoverLoader::coverFailed, this, [this](const QString& url, const QString&) {
        m_coverPending.remove(url);
        // 记下来别再重试：被阻断的主机每次都要等超时
        m_coverFailed.insert(url);
    });
}

QIcon MapHallPage::icon() const
{
    // 图标主题里地图类资源叫 worlds（见 resources/*/scalable/worlds.svg）
    return QIcon::fromTheme(QStringLiteral("worlds"));
}

void MapHallPage::openedImpl()
{
    // 页面第一次被选中才发请求：没人点这个页签就不该占带宽
    /**
     * 打开这一页**只读本地**。
     *
     * 拉取交给启动时的后台任务（`MainWindow::maybeUpdateHallLibrary`）或用户点
     * 「更新数据」，这里再不现拉 —— 以前在这里同步，用户要干等二三十秒。
     * 每次打开都从磁盘重读一次，后台刚更新完也能立刻看到。
     */
    m_api->reloadLibrary();
    updateLibraryHint();

    // 「地图设置 → 大厅通道」改过之后，封面也跟着换通道（否则要重启才生效）
    MapCoverLoader::instance()->setTransportMode(APPLICATION->settings()->get("MapHallTransport").toString());

    /**
     * 本地库**不完整**也要自动补：旧版同步的详情按 maps 表默认顺序取前 1000，
     * 和列表的 slug 集合对不上，缺详情的图点开就只能干等网络（用户实测"一直加载不出来"）。
     * 检测在 HallCache::detailsCoverList —— 有缓存但覆盖不全 → 直接重新同步。
     */
    if (m_api->hasLibrary() && m_api->libraryIncomplete()) {
        m_status->setText(tr("本地库缺了部分地图详情（旧版同步留下的），正在自动补全…"));
        syncLibrary();
    } else if (m_api->hasLibrary()) {
        startSearch();
        m_api->facets();
        const QDateTime at = m_api->libraryFetchedAt().toLocalTime();
        m_status->setText(tr("本地库：%1 张地图 · 更新于 %2")
                              .arg(m_api->libraryItemCount())
                              .arg(at.isValid() ? at.toString(QStringLiteral("MM-dd HH:mm")) : tr("未知")));
    } else {
        if (m_leftStack != nullptr)
            m_leftStack->setCurrentWidget(m_list);
        m_status->setText(tr("还没有本地地图库。点「下载地图库」拉一次（约 20–40 秒），之后打开即用。"));
    }
    m_started = true;

    // 从别的来源页切过来时，对话框里可能还留着那边的任务；
    // 这里没准备好导入任务就先禁用「确定」，避免拿别人的任务建实例。
    if (m_hint->text().isEmpty())
        m_dialog->setSuggestedPack(m_dialog->instName(), nullptr);
}

void MapHallPage::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // ── 搜索与筛选 ──
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索地图名 / 系列 / 作者（留空 = 最新发布）"));
    m_search->setClearButtonEnabled(true);
    auto* go = new QPushButton(tr("搜索"), this);

    m_sort = new QComboBox(this);
    m_sort->addItem(tr("最新发布"), QStringLiteral("release_desc"));
    m_sort->addItem(tr("评分最高"), QStringLiteral("rating_desc"));
    m_sort->addItem(tr("最多人玩"), QStringLiteral("popularity_desc"));
    m_sort->addItem(tr("时长最长"), QStringLiteral("playtime_desc"));

    m_series = new QComboBox(this);
    m_series->addItem(tr("系列：不限"), QString());
    m_difficulty = new QComboBox(this);
    m_difficulty->addItem(tr("难度：不限"), QString());

    m_length = new QComboBox(this);
    m_length->addItem(tr("长度：不限"), QString());
    m_year = new QComboBox(this);
    m_year->addItem(tr("年份：不限"), QString());

    m_syncButton = new QPushButton(tr("下载地图库"), this);
    m_syncButton->setToolTip(tr("一次把整库拉到本地（几百张图 + 评论），之后搜索、详情、评论都在本地算；\n"
                                "有新图时再点一次即可。"));

    m_resetFilters = new QToolButton(this);
    m_resetFilters->setText(tr("重置"));
    m_resetFilters->setAutoRaise(true);
    m_resetFilters->setToolTip(tr("清空搜索词与所有筛选条件，回到「最新发布」"));

    /**
     * 宽度按「最长条目」算（2026-11-02 用户反馈：固定 4 个字宽把「难度：不限」裁成了「难度：不…」）。
     * 系列这一项带 stretch，窗口再窄也只是它被压缩，其余下拉保持可读。
     */
    for (auto* combo : { m_sort, m_series, m_difficulty, m_length, m_year }) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMinimumWidth(84);
    }

    /**
     * 两行筛选（2026-11-02 调整）：
     *   第一行只放搜索与两个动作 —— 搜索框需要横向拉伸；
     *   第二行放排序 + 四个筛选下拉 + 重置，系列占掉剩余宽度。
     * 挤在一行时，加到第四个下拉就会把搜索框压到只剩几十像素。
     */
    auto* filterTop = new QHBoxLayout();
    filterTop->setSpacing(6);
    filterTop->addWidget(m_search, 1);
    filterTop->addWidget(go);
    filterTop->addWidget(m_syncButton);
    layout->addLayout(filterTop);

    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);
    filterRow->addWidget(m_sort);
    filterRow->addWidget(m_series, 1);
    filterRow->addWidget(m_difficulty);
    filterRow->addWidget(m_length);
    filterRow->addWidget(m_year);
    filterRow->addWidget(m_resetFilters);
    layout->addLayout(filterRow);

    // ── 列表 + 详情 ──
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_leftStack = new QStackedWidget(splitter);

    m_list = new QListWidget(m_leftStack);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFrameShape(QFrame::NoFrame);
    m_leftStack->addWidget(m_list);

    // ── 失败面板（照 AIO renderHallFail：标题 / 说明 / 建议 / 动作 / 技术细节） ──
    m_failBox = new QWidget(m_leftStack);
    auto* failLayout = new QVBoxLayout(m_failBox);
    failLayout->setContentsMargins(16, 16, 16, 16);
    failLayout->setSpacing(8);

    m_failTitle = new QLabel(m_failBox);
    QFont failTitleFont = m_failTitle->font();
    failTitleFont.setBold(true);
    failTitleFont.setPointSizeF(failTitleFont.pointSizeF() * 1.15);
    m_failTitle->setFont(failTitleFont);
    m_failTitle->setWordWrap(true);
    failLayout->addWidget(m_failTitle);

    m_failLead = new QLabel(m_failBox);
    m_failLead->setWordWrap(true);
    failLayout->addWidget(m_failLead);

    m_failTips = new QLabel(m_failBox);
    m_failTips->setWordWrap(true);
    m_failTips->setTextFormat(Qt::RichText);
    failLayout->addWidget(m_failTips);

    auto* failActions = new QHBoxLayout();
    auto* retryButton = new QPushButton(tr("重试"), m_failBox);
    auto* probeButton = new QPushButton(tr("出网诊断"), m_failBox);
    auto* netButton = new QPushButton(tr("网络设置"), m_failBox);
    auto* siteButton = new QPushButton(tr("打开原站"), m_failBox);
    // 只有"出口不对"这类失败才给反代：HTTP/解析错误给这个只会误导
    m_relayButton = new QPushButton(tr("反代模板…"), m_failBox);
    m_relayButton->setVisible(false);
    failActions->addWidget(retryButton);
    failActions->addWidget(probeButton);
    failActions->addWidget(netButton);
    failActions->addWidget(m_relayButton);
    failActions->addWidget(siteButton);
    failActions->addStretch(1);
    failLayout->addLayout(failActions);

    m_probeResult = new QLabel(m_failBox);
    m_probeResult->setWordWrap(true);
    m_probeResult->setVisible(false);
    failLayout->addWidget(m_probeResult);

    m_failDetailToggle = new QToolButton(m_failBox);
    m_failDetailToggle->setText(tr("▸ 技术细节"));
    m_failDetailToggle->setCheckable(true);
    m_failDetailToggle->setAutoRaise(true);
    failLayout->addWidget(m_failDetailToggle);

    m_failDetail = new QLabel(m_failBox);
    m_failDetail->setWordWrap(true);
    m_failDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_failDetail->setVisible(false);
    failLayout->addWidget(m_failDetail);
    failLayout->addStretch(1);

    connect(m_failDetailToggle, &QToolButton::toggled, this, [this](bool on) {
        m_failDetail->setVisible(on);
        m_failDetailToggle->setText(on ? tr("▾ 技术细节") : tr("▸ 技术细节"));
    });
    connect(retryButton, &QPushButton::clicked, this, [this] {
        // 重试前先让出口重新探活：用户可能刚把代理打开
        HallAPI::invalidateProxyCache();
        startSearch();
    });
    connect(probeButton, &QPushButton::clicked, this, &MapHallPage::runProbe);
    connect(m_relayButton, &QPushButton::clicked, this, &MapHallPage::showRelayHelp);
    connect(netButton, &QPushButton::clicked, this, [this] {
        MapSettingsDialog dialog(this);
        dialog.exec();
        HallAPI::invalidateProxyCache();
    });
    connect(siteButton, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.ctmmaps.com"))); });

    m_leftStack->addWidget(m_failBox);
    splitter->addWidget(m_leftStack);

    auto* detail = new QWidget(splitter);
    detail->setMinimumWidth(320);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(8, 2, 2, 2);
    detailLayout->setSpacing(6);

    /**
     * 头部：缩略图 + 标题，横排。
     *
     * 以前封面是 260×150 的大图，把下半屏全占了，而简介和评论被挤进一个小滚动框 ——
     * 既看不清评论，中间又空。现在封面缩成 96×54 的缩略图，剩下的高度全给
     * 「简介 / 评论」两个页签。
     */
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);

    m_cover = new QLabel(detail);
    m_cover->setFixedSize(DETAIL_COVER_W, DETAIL_COVER_H);
    m_cover->setAlignment(Qt::AlignCenter);
    m_cover->setScaledContents(false);
    head->addWidget(m_cover, 0, Qt::AlignTop);

    auto* headText = new QVBoxLayout();
    headText->setContentsMargins(0, 0, 0, 0);
    headText->setSpacing(2);

    m_title = new QLabel(detail);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    headText->addWidget(m_title);

    m_subtitle = new QLabel(detail);
    m_subtitle->setWordWrap(true);
    m_subtitle->setEnabled(false);
    headText->addWidget(m_subtitle);

    m_statBox = new QWidget(detail);
    m_statRow = new QHBoxLayout(m_statBox);
    m_statRow->setContentsMargins(0, 0, 0, 0);
    m_statRow->setSpacing(4);
    m_statRow->addStretch(1);
    headText->addWidget(m_statBox);
    headText->addStretch(1);

    head->addLayout(headText, 1);
    detailLayout->addLayout(head);

    // ── 简介 / 评论 两个页签：各自独立滚动，谁也挤不着谁 ──
    m_tabs = new QTabWidget(detail);
    m_tabs->setDocumentMode(true);

    auto* about = new QWidget(m_tabs);
    auto* aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(4, 4, 4, 4);
    aboutLayout->setSpacing(6);

    m_facts = new QLabel(about);
    m_facts->setWordWrap(true);
    m_facts->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutLayout->addWidget(m_facts);

    auto* descScroll = new QScrollArea(about);
    descScroll->setWidgetResizable(true);
    descScroll->setFrameShape(QFrame::NoFrame);
    m_description = new QLabel(descScroll);
    m_description->setWordWrap(true);
    m_description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    descScroll->setWidget(m_description);
    aboutLayout->addWidget(descScroll, 1);

    m_tabs->addTab(about, tr("简介"));

    auto* reviewsTab = new QWidget(m_tabs);
    auto* reviewsLayout = new QVBoxLayout(reviewsTab);
    reviewsLayout->setContentsMargins(4, 4, 4, 4);
    reviewsLayout->setSpacing(4);

    auto* reviewsScroll = new QScrollArea(reviewsTab);
    reviewsScroll->setWidgetResizable(true);
    reviewsScroll->setFrameShape(QFrame::NoFrame);
    // 评论卡片按宽度换行，永远不该出现横向滚动条（2026-11-02 用户反馈）
    reviewsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_reviewBox = new QWidget(reviewsScroll);
    m_reviewLayout = new QVBoxLayout(m_reviewBox);
    m_reviewLayout->setContentsMargins(0, 0, 4, 0);
    m_reviewLayout->setSpacing(10);
    m_reviewLayout->addStretch(1);
    reviewsScroll->setWidget(m_reviewBox);
    reviewsLayout->addWidget(reviewsScroll, 1);

    m_reviewMore = new QPushButton(tr("加载更多评论"), reviewsTab);
    m_reviewMore->setVisible(false);
    reviewsLayout->addWidget(m_reviewMore);

    m_tabs->addTab(reviewsTab, tr("评论"));
    m_reviewsTabIndex = m_tabs->count() - 1;

    // 「我的」：本机视角 —— 哪张实例关联了这张图、它的评分笔记与游玩记录
    auto* mineTab = new QWidget(m_tabs);
    auto* mineLayout = new QVBoxLayout(mineTab);
    mineLayout->setContentsMargins(4, 8, 4, 4);
    mineLayout->setSpacing(6);

    m_mineState = new QLabel(mineTab);
    m_mineState->setWordWrap(true);
    m_mineState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mineState->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    mineLayout->addWidget(m_mineState);
    mineLayout->addStretch(1);

    m_tabs->addTab(mineTab, tr("我的"));

    detailLayout->addWidget(m_tabs, 1);

    auto* actions = new QHBoxLayout();
    auto* openButton = new QPushButton(tr("打开下载页"), detail);
    auto* pickButton = new QPushButton(tr("选择已下载的文件…"), detail);
    actions->addWidget(openButton);
    actions->addWidget(pickButton);
    actions->addStretch(1);
    detailLayout->addLayout(actions);

    m_hint = new QLabel(detail);
    m_hint->setWordWrap(true);
    m_hint->setEnabled(false);
    detailLayout->addWidget(m_hint);

    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_loadMore = new QPushButton(tr("加载更多…"), this);
    m_loadMore->setVisible(false);
    layout->addWidget(m_loadMore);

    connect(go, &QPushButton::clicked, this, &MapHallPage::startSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &MapHallPage::startSearch);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int) { startSearch(); });
    connect(m_series, &QComboBox::currentIndexChanged, this, [this](int) { startSearch(); });
    connect(m_difficulty, &QComboBox::currentIndexChanged, this, [this](int) { startSearch(); });
    connect(m_length, &QComboBox::currentIndexChanged, this, [this](int) { startSearch(); });
    connect(m_year, &QComboBox::currentIndexChanged, this, [this](int) { startSearch(); });
    connect(m_resetFilters, &QToolButton::clicked, this, [this] {
        // 只挡信号会漏掉"搜索词被清空"的联动，直接逐个改完再统一搜一次
        const QSignalBlocker b1(m_sort), b2(m_series), b3(m_difficulty), b4(m_length), b5(m_year);
        m_search->clear();
        m_sort->setCurrentIndex(0);
        m_series->setCurrentIndex(0);
        m_difficulty->setCurrentIndex(0);
        m_length->setCurrentIndex(0);
        m_year->setCurrentIndex(0);
        startSearch();
    });
    connect(m_loadMore, &QPushButton::clicked, this, &MapHallPage::loadMore);
    connect(m_reviewMore, &QPushButton::clicked, this, [this] {
        if (!m_reviewMapUuid.isEmpty())
            requestReviews(m_reviews.size());
    });
    connect(m_list, &QListWidget::currentRowChanged, this, &MapHallPage::showRow);

    connect(openButton, &QPushButton::clicked, this, [this] {
        const QString url = currentDownloadUrl();
        if (url.isEmpty()) {
            // 没选行（或者大厅连不上）时，至少让人能去站上逛
            m_status->setText(tr("已在浏览器打开 CTM 地图站；挑好后下载，再回来点「选择已下载的文件…」。"));
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.ctmmaps.com")));
            return;
        }
        m_status->setText(tr("已在浏览器打开下载页；下载完回到这里点「选择已下载的文件…」。"));
        QDesktopServices::openUrl(QUrl(url));
    });
    connect(pickButton, &QPushButton::clicked, this, &MapHallPage::pickLocalArchive);

    m_status->setText(tr("列表来自 CTMMaps 的公开数据；下载交给浏览器，不会在启动器里偷偷拉。"));
}

HallQuery MapHallPage::currentQuery() const
{
    HallQuery query;
    query.text = m_search->text().trimmed();
    query.sort = m_sort->currentData().toString();
    query.series = m_series->currentData().toString();
    const QString difficulty = m_difficulty->currentData().toString();
    if (!difficulty.isEmpty())
        query.difficulties = { difficulty };
    const QString length = m_length->currentData().toString();
    if (!length.isEmpty())
        query.lengths = { length };
    query.year = m_year->currentData().toString();
    query.limit = 20;
    return query;
}

void MapHallPage::syncLibrary()
{
    if (m_syncing)
        return;

    m_syncing = true;
    m_syncButton->setEnabled(false);
    // 同步时把失败面板收起来：那上面显示的是上一次的失败，会跟进度混在一起
    if (m_leftStack != nullptr)
        m_leftStack->setCurrentWidget(m_list);
    m_status->setText(tr("正在下载地图库…（4 个请求，可能需要十几秒）"));
    m_api->syncLibrary();
}

void MapHallPage::updateLibraryHint()
{
    if (m_api->hasLibrary()) {
        const QDateTime at = m_api->libraryFetchedAt().toLocalTime();
        m_syncButton->setText(tr("更新数据"));
        m_syncButton->setToolTip(tr("本地库：%1 张地图 · 更新于 %2 · 缓存 %3 MB\n"
                                    "搜索、详情、评论都在本地算；有新图时点一次即可。")
                                     .arg(m_api->libraryItemCount())
                                     .arg(at.isValid() ? at.toString(QStringLiteral("MM-dd HH:mm")) : tr("未知"))
                                     .arg(m_api->libraryBytes() / 1024.0 / 1024.0, 0, 'f', 1));
        return;
    }
    m_syncButton->setText(tr("下载地图库"));
    m_syncButton->setToolTip(tr("一次把整库拉到本地（几百张图 + 评论），之后搜索、详情、评论都在本地算。"));
}

void MapHallPage::startSearch()
{
    HallQuery query = currentQuery();
    query.offset = 0;
    m_loadingMore = false;
    m_loadMore->setVisible(false);
    // 浏览器通道一条请求要几秒：先说明白，别让人以为卡住了
    m_status->setText(m_api->hasLibrary() ? tr("正在搜索（本地库）…")
                                          : tr("正在查询…（走浏览器通道时需要几秒）"));
    m_api->search(query);
}

void MapHallPage::loadMore()
{
    if (m_items.isEmpty())
        return;
    HallQuery query = currentQuery();
    query.offset = static_cast<int>(m_items.size());
    m_loadingMore = true;
    m_status->setText(tr("正在加载更多…"));
    m_api->search(query);
}

void MapHallPage::onSearchDone(const QList<HallItem>& items)
{
    const bool append = m_loadingMore;
    m_loadingMore = false;

    if (!append) {
        m_items.clear();
        m_list->clear();
    }

    // 分页之间服务端排序不保证稳定，同一张图可能回来两次
    QSet<QString> seen;
    for (const auto& existing : m_items)
        seen.insert(existing.slug);

    for (const auto& item : items) {
        if (seen.contains(item.slug))
            continue;
        seen.insert(item.slug);
        m_items.append(item);

        auto* row = new QListWidgetItem(m_list);
        row->setSizeHint(QSize(0, ROW_COVER_H + 20));
        auto* widget = new HallRow(item, m_list);
        m_list->setItemWidget(row, widget);
        if (m_covers.contains(item.coverUrl))
            widget->setCover(m_covers.value(item.coverUrl));
        else
            requestCover(item.coverUrl);
    }

    // facets 要在 search 之后取：HallAPI 只有一个在途请求，同时发会互相 abort
    if (!m_facetsRequested) {
        m_facetsRequested = true;
        m_api->facets();
    }

    m_loadMore->setVisible(items.size() >= 20);
    if (m_leftStack != nullptr)
        m_leftStack->setCurrentWidget(m_list);

    // 本地命中时写「本地库」，别再拿"浏览器通道/直连"误导人（那次搜索根本没出网）
    const QString via = m_api->hasLibrary() ? tr("本地库") : m_api->transportNote();
    m_status->setText((m_items.isEmpty() ? tr("没有匹配的地图。换个关键词，或清空搜索看最新发布。")
                                         : tr("已显示 %1 张。点一行看详情。").arg(m_items.size()))
                      + QStringLiteral("  ·  ") + via);

    if (!append && !m_items.isEmpty() && m_list->currentRow() < 0)
        m_list->setCurrentRow(0);
}

void MapHallPage::showRow(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    const HallItem& item = m_items.at(index);
    m_title->setText(item.title);

    QStringList sub;
    if (!item.series.isEmpty())
        sub.append(item.series);
    if (!item.author.isEmpty())
        sub.append(item.author);
    if (!item.version.isEmpty())
        sub.append(tr("版本 %1").arg(item.version));
    m_subtitle->setText(sub.join(QStringLiteral(" · ")));

    m_facts->clear();
    m_description->setText(tr("正在读取简介与评论…"));
    clearReviews();
    refreshMineTab();
    m_cover->setPixmap(m_covers.contains(item.coverUrl)
                           ? cropTo(m_covers.value(item.coverUrl), DETAIL_COVER_W, DETAIL_COVER_H)
                           : makePlaceholderCover(item.title, DETAIL_COVER_W, DETAIL_COVER_H));
    requestCover(item.coverUrl);

    m_api->detail(item.slug);
}

void MapHallPage::onDetail(const HallDetail& detail)
{
    m_details.insert(detail.item.slug, detail);

    // 异步回来时用户可能已经点了别的行
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size() || m_items.at(row).slug != detail.item.slug)
        return;

    const auto& item = detail.item;

    // 11 项属性：缺的就不写，别显示「-0」
    QStringList facts;
    const auto addFact = [&facts](const QString& label, const QString& value) {
        if (!value.isEmpty())
            facts.append(QStringLiteral("%1：%2").arg(label, value));
    };
    addFact(tr("系列"), item.series);
    addFact(tr("版本"), item.version);
    addFact(tr("难度"), item.difficulty);
    addFact(tr("长度"), item.length);
    addFact(tr("类型"), item.edition);
    if (item.objectives >= 0)
        addFact(tr("目标"), QString::number(item.objectives));
    if (item.bonuses >= 0)
        addFact(tr("奖励目标"), QString::number(item.bonuses));
    if (item.rating >= 0)
        addFact(tr("评分"), QString::number(item.rating, 'f', 1));
    if (item.minutes >= 0)
        addFact(tr("平均时长"), formatLength(item.minutes));
    if (item.plays >= 0)
        addFact(tr("游玩人数"), QString::number(item.plays));
    addFact(tr("发布"), item.releaseDate);
    if (item.lostMedia)
        addFact(tr("提醒"), tr("原下载已失效"));
    m_facts->setText(facts.join(QStringLiteral("\n")));

    // 统计 chip：★ 评分 / 平均时长 / 游玩人数（AIO hall-detail 顶部那排 stat）
    while (m_statRow->count() > 1) {
        QLayoutItem* layoutItem = m_statRow->takeAt(0);
        if (QWidget* widget = layoutItem->widget())
            widget->deleteLater();
        delete layoutItem;
    }
    QStringList stats;
    if (item.rating >= 0)
        stats.append(QStringLiteral("★ %1").arg(item.rating, 0, 'f', 1));
    if (item.minutes > 0)
        stats.append(formatLength(item.minutes));
    if (item.plays > 0)
        stats.append(tr("%1 人玩过").arg(item.plays));
    for (const auto& text : stats) {
        auto* chip = new QLabel(text, m_statBox);
        chip->setStyleSheet(ui::chipStyle(ui::accentColor()));
        m_statRow->insertWidget(m_statRow->count() - 1, chip);
    }

    QString description = detail.description;
    if (description.size() > 1200)
        description = description.left(1200) + QStringLiteral("…");
    m_description->setText(description.isEmpty() ? tr("（这张图没有简介）") : description);

    // 简介拿到之后再取评论：HallAPI 同一时刻只允许一个在途请求
    if (m_tabs != nullptr)
        m_tabs->setCurrentIndex(0);
    requestReviews(0);

    if (!detail.downloadLinks.isEmpty() && item.pageUrl.isEmpty())
        m_hint->setText(tr("可以直接打开下载直链。"));
}

void MapHallPage::onFacets(const HallFacets& facets)
{
    const auto refill = [](QComboBox* combo, const QString& placeholder, const QStringList& values,
                           QString (*label)(const QString&) = nullptr) {
        const QString current = combo->currentData().toString();
        combo->clear();
        combo->addItem(placeholder, QString());
        for (const auto& value : values)
            combo->addItem(label != nullptr ? label(value) : value, value);
        const int index = combo->findData(current);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };

    QStringList lengths = facets.lengths;
    std::sort(lengths.begin(), lengths.end(), [](const QString& a, const QString& b) {
        return lengthRank(a) < lengthRank(b);
    });

    QSignalBlocker s1(m_series), s2(m_difficulty), s3(m_length), s4(m_year);
    refill(m_series, tr("系列：不限"), facets.series);
    refill(m_difficulty, tr("难度：不限"), facets.difficulties, &difficultyLabel);
    refill(m_length, tr("长度：不限"), lengths, &lengthLabel);
    refill(m_year, tr("年份：不限"), facets.years);
}

void MapHallPage::onFailed(HallError kind, const QString& humanReason, const QString& techDetail)
{
    // 「没有匹配的地图」不是故障：列表留空 + 一句提示就够了，
    // 弹失败面板反而让人以为网络坏了（AIO 里这两种状态也是分开的）
    if (kind == HallError::Empty) {
        if (m_syncing) {
            m_syncing = false;
            m_syncButton->setEnabled(true);
        }
        m_items.clear();
        m_list->clear();
        m_loadMore->setVisible(false);
        if (m_leftStack != nullptr)
            m_leftStack->setCurrentWidget(m_list);
        m_status->setText(humanReason);
        return;
    }

    /**
     * **「更新数据」失败不能把已有的大厅弄没**（2026-11-02 用户反馈）。
     *
     * 本地库是全量快照：搜索、详情、评论、封面都从磁盘读，一个请求都不发。
     * 更新只是"看看站上有没有新图"，失败了最坏也就是数据旧一点，绝不该
     * 用一张失败面板把列表顶掉 —— 那会让人以为整个大厅挂了。
     */
    if (m_syncing && m_api->hasLibrary()) {
        m_syncing = false;
        m_syncButton->setEnabled(true);
        if (m_leftStack != nullptr)
            m_leftStack->setCurrentWidget(m_list);
        updateLibraryHint();

        const QDateTime at = m_api->libraryFetchedAt().toLocalTime();
        m_status->setText(tr("更新没成功：%1\n本地库仍是 %2 的数据（%3 张地图），搜索、详情、评论、封面都照常可用；"
                             "过一会儿再点一次「更新数据」就行。")
                              .arg(humanReason,
                                   at.isValid() ? at.toString(QStringLiteral("MM-dd HH:mm")) : tr("上次同步"),
                                   QString::number(m_api->libraryItemCount())));
        return;
    }

    if (m_syncing) {
        m_syncing = false;
        m_syncButton->setEnabled(true);
    }

    QString text = humanReason;
    if (!techDetail.isEmpty())
        text += QStringLiteral("\n（技术细节：%1）").arg(techDetail);
    if (kind == HallError::Dns || kind == HallError::Tls || kind == HallError::Timeout ||
        kind == HallError::Blocked) {
        // 这个接口在部分网络下会被直接重置（docs/map-launcher R2）。
        // 大厅只是"发现地图"的入口，必须给一条不依赖它的路。
        text += tr("\n\n可以：① 在「设置 → 网络」里配一个可用代理后点「搜索」重试；"
                   "② 点「打开下载页」到站点上挑，下载完用「选择已下载的文件…」导入 —— 这条路完全离线可用。");
    }
    m_status->setText(text + QStringLiteral("\n（当前通道：%1）").arg(m_api->transportNote()));
    m_loadMore->setVisible(false);
    showFailure(kind, humanReason, techDetail);
}

void MapHallPage::showFailure(HallError kind, const QString& reason, const QString& detail)
{
    QString title;
    QString lead;
    QStringList tips;
    switch (kind) {
        case HallError::Tls:
            title = tr("连不上 CTMMaps：握手被重置");
            lead = tr("程序走 TCP，刚发出 TLS 握手就被打断。浏览器靠 QUIC 走 UDP 才可能绕过，"
                      "所以会出现「网页能打开、程序不行」。");
            tips = { tr("再点一次「重试」——链路有时要探一次才通"),
                     tr("换一条网络（例如手机热点）确认是不是本机线路的问题"),
                     tr("在「地图设置 → 大厅代理」填一个能到 supabase 的代理；"
                        "也可以把「大厅数据地址」换成你自己的反代") };
            break;
        case HallError::Blocked:
            title = tr("连不上 CTMMaps：数据主机把连接掐断了");
            lead = tr("域名解析正常、TCP 也连上了，但发不出一个完整的 HTTP 请求就被打断 —— "
                      "这条线路到 %1 的数据主机（Cloudflare）被阻断。相对地，www.ctmmaps.com "
                      "本身是能打开的，所以会出现「网页能看、列表刷不出来」。\n"
                      "浏览器之所以能看，是因为它走 HTTP/3（QUIC/UDP）；启动器用的 Qt 只有 "
                      "TCP（HTTP/1.1、HTTP/2），没有 HTTP/3 —— AIO 能通也是靠 Electron 里"
                      "自带的 Chromium 发请求，而不是 Node 网络层。")
                       .arg(QStringLiteral("supabase"));
            tips = { tr("不用管：默认「大厅通道 = 自动」会立刻改用本机浏览器（Edge/Chrome）重发，"
                        "浏览器走 ECH，握手内容里看不出目标域名，所以能通 —— 这条不需要任何代理"),
                     tr("如果仍然失败，把「地图设置 → 大厅通道」改成「只用浏览器」再点重试；"
                        "浏览器通道每次请求约 2 秒，慢但能拿到数据"),
                     tr("有代理的话：把「大厅通道」改回「只用网络」会更快（代理直连 Qt 网络）"),
                     tr("完全不联网也能用：点「打开下载页」到站点上挑，下载完用"
                        "「选择已下载的文件…」导入"),
                     tr("「出网诊断」会分别试代理和直连两条路，告诉你是哪条通") };
            break;
        case HallError::Timeout:
            title = tr("连不上 CTMMaps：没有响应");
            lead = tr("请求发出去了，但在限定时间内没有任何回音。");
            tips = { tr("重试一次（线路拥塞常见于瞬时）"),
                     tr("确认「地图设置 → 大厅代理」里的代理还活着") };
            break;
        case HallError::Dns:
            title = tr("连不上 CTMMaps：域名解析失败");
            lead = tr("解析不出数据地址里的主机名。");
            tips = { tr("检查系统 DNS，或换成 223.5.5.5 / 1.1.1.1"),
                     tr("如果用的是自建反代地址，确认它还能解析") };
            break;
        case HallError::Http:
            title = tr("CTMMaps 拒绝了这次请求");
            lead = tr("网络是通的，是对方返回了错误状态码。");
            tips = { tr("可能是 anon key 轮换了，或站点临时限制了读取"),
                     tr("过一会儿再试；持续失败请把「技术细节」里的状态码记下来") };
            break;
        case HallError::Parse:
            title = tr("CTMMaps 返回的数据看不懂");
            lead = tr("网络是通的，但返回的内容不是预期的 JSON —— 站方可能改了接口。");
            tips = { tr("过一会儿再试"), tr("把「技术细节」里的原文记下来反馈") };
            break;
        default:
            title = tr("连不上 CTMMaps");
            lead = tr("没有取到数据。");
            tips = { tr("重试一次"), tr("用「出网诊断」看看到底卡在哪一步") };
            break;
    }

    m_failTitle->setText(title);
    m_failLead->setText(lead);
    QStringList bullets;
    for (const auto& tip : tips)
        bullets.append(QStringLiteral("• %1").arg(tip.toHtmlEscaped()));
    m_failTips->setText(bullets.join(QStringLiteral("<br/>")));

    m_failDetail->setText(tr("数据地址：%1\n服务端通道：%2\n当前出口：%3")
                              .arg(m_api->endpoint(),
                                   detail.isEmpty() ? reason : QStringLiteral("%1（%2）").arg(reason, detail),
                                   m_api->transportNote()));
    m_failDetail->setVisible(m_failDetailToggle->isChecked());
    m_probeResult->setVisible(false);
    if (m_relayButton != nullptr) {
        m_relayButton->setVisible(kind == HallError::Blocked || kind == HallError::Tls ||
                                  kind == HallError::Timeout);
    }

    if (m_leftStack != nullptr)
        m_leftStack->setCurrentWidget(m_failBox);
}

void MapHallPage::showRelayHelp()
{
    /**
     * 为什么给"贴代码"而不是"帮你部署"：反代必须部署在**能访问 supabase 的网络**里，
     * 而启动器所在的机器恰恰不能 —— 这一步只能由用户（或他的服务器）完成。
     * Cloudflare Workers 免费额度足够个人使用，20 行就够转发 REST 请求。
     */
    const QString code = QStringLiteral(
        "// Cloudflare Workers → 新建 Worker，粘贴这段，部署后得到 https://xxx.workers.dev\n"
        "const UPSTREAM = 'https://qtxjbukjyuylqjgiphxs.supabase.co';\n"
        "\n"
        "export default {\n"
        "  async fetch(request) {\n"
        "    const url = new URL(request.url);\n"
        "    const target = UPSTREAM + url.pathname + url.search;\n"
        "    const headers = new Headers(request.headers);\n"
        "    headers.set('Host', new URL(UPSTREAM).host);\n"
        "    const init = { method: request.method, headers };\n"
        "    if (request.method !== 'GET' && request.method !== 'HEAD') init.body = request.body;\n"
        "    const response = await fetch(target, init);\n"
        "    const out = new Response(response.body, response);\n"
        "    out.headers.set('Access-Control-Allow-Origin', '*');\n"
        "    return out;\n"
        "  },\n"
        "};\n");

    QDialog dialog(this);
    dialog.setWindowTitle(tr("反代模板（Cloudflare Workers）"));
    dialog.resize(680, 460);
    auto* layout = new QVBoxLayout(&dialog);

    auto* steps = new QLabel(tr(
                                 "① 打开 dash.cloudflare.com → Workers & Pages → Create → Worker；\n"
                                 "② 把下面的代码整体粘贴进去，Deploy；\n"
                                 "③ 复制它给的 https://<名字>.<账号>.workers.dev；\n"
                                 "④ 回到「地图设置 → 大厅数据地址」填这个域名（不要带 /rest/v1），保存。\n"
                                 "之后大厅、封面、评论全部走你的 Worker，不再受本机线路影响。"),
                             &dialog);
    steps->setWordWrap(true);
    steps->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(steps);

    auto* codeView = new QPlainTextEdit(&dialog);
    codeView->setPlainText(code);
    codeView->setReadOnly(true);
    codeView->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(codeView, 1);

    auto* buttons = new QHBoxLayout();
    auto* copy = new QPushButton(tr("复制代码"), &dialog);
    auto* openDash = new QPushButton(tr("打开 Cloudflare 控制台"), &dialog);
    auto* close = new QPushButton(tr("关闭"), &dialog);
    buttons->addWidget(copy);
    buttons->addWidget(openDash);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(copy, &QPushButton::clicked, &dialog, [this, code, copy] {
        QApplication::clipboard()->setText(code);
        copy->setText(tr("已复制 ✓"));
    });
    connect(openDash, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://dash.cloudflare.com/?to=/:account/workers-and-pages")));
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MapHallPage::runProbe()
{
    m_probeResult->setVisible(true);
    m_probeResult->setEnabled(false);
    m_probeResult->setText(tr("诊断中…"));
    HallAPI::invalidateProxyCache();
    m_api->probe();
}

void MapHallPage::requestCover(const QString& url)
{
    if (url.isEmpty() || m_covers.contains(url) || m_coverPending.contains(url))
        return;

    if (m_coverFailed.contains(url))
        return;

    auto* covers = MapCoverLoader::instance();
    // 磁盘/内存里已经有就直接用，不必等信号（翻页回来时最常见）
    if (const auto image = covers->cached(url); !image.isNull()) {
        applyCover(url, QPixmap::fromImage(image));
        return;
    }

    m_coverPending.insert(url);
    covers->request(url);
}

void MapHallPage::applyCover(const QString& url, const QPixmap& pixmap)
{
    if (pixmap.isNull())
        return;

    // 只留缩略图：原图动辄几 MB（AIO 实测 158 MiB → 2.98 MiB）
    m_covers.insert(url, cropTo(pixmap, 288, ROW_COVER_H * 2));

    // 回填所有引用这个 url 的行
    for (int i = 0; i < m_list->count() && i < m_items.size(); ++i) {
        if (m_items.at(i).coverUrl != url)
            continue;
        if (auto* widget = dynamic_cast<HallRow*>(m_list->itemWidget(m_list->item(i))))
            widget->setCover(m_covers.value(url));
    }

    const int current = m_list->currentRow();
    if (current >= 0 && current < m_items.size() && m_items.at(current).coverUrl == url)
        m_cover->setPixmap(cropTo(m_covers.value(url), DETAIL_COVER_W, DETAIL_COVER_H));
}

QString MapHallPage::currentDownloadUrl() const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size())
        return {};
    const auto& item = m_items.at(row);
    const auto detail = m_details.constFind(item.slug);
    if (detail != m_details.constEnd() && !detail->downloadLinks.isEmpty())
        return detail->downloadLinks.first();
    return item.pageUrl;
}

void MapHallPage::refreshMineTab()
{
    if (m_mineState == nullptr)
        return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size()) {
        m_mineState->setText(tr("先在左边选一张图。"));
        return;
    }

    const HallItem& item = m_items.at(row);

    /**
     * 找本机关联：遍历实例目录里的 hall-link.json。
     * 实例量级是几十个，每个只读一个小 JSON，毫秒级，不需要索引。
     */
    MinecraftInstance* linked = nullptr;
    MapHallLink link;
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        const MapHallLink candidate = MapHallLink::load(instance->instanceRoot());
        if (candidate.isValid() && candidate.slug == item.slug) {
            linked = instance;
            link = candidate;
            break;
        }
    }

    if (linked == nullptr) {
        m_mineState->setText(tr("本机还没有关联到这张图的实例。\n\n"
                                "用下面的「选择已下载的文件…」把它导入成实例之后，\n"
                                "在实例窗口的「信息 → 简介」页可以把它关联到这里 ——\n"
                                "那时这一页会显示它的中文名、我的评分、笔记和游玩记录。"));
        return;
    }

    const MapNotes notes = MapNotes::load(linked->instanceRoot());
    const MapMeta meta = MapMeta::load(linked->instanceRoot());

    QStringList lines;
    lines.append(tr("本机实例：<b>%1</b>").arg(linked->name().toHtmlEscaped()));
    if (link.title != item.title)
        lines.append(tr("关联标题：%1").arg(link.title.toHtmlEscaped()));

    lines.append(QString());
    lines.append(tr("中文名：%1").arg(notes.alias.isEmpty() ? tr("（未设置）") : notes.alias.toHtmlEscaped()));
    if (notes.rating > 0) {
        const int full = std::clamp(notes.rating, 0, MapNotes::MAX_RATING) / 2;
        lines.append(tr("我的评分：%1  %2 / 5")
                         .arg(QString(full, QChar(0x2605)) + QString(5 - full, QChar(0x2606)),
                              QString::number(notes.rating / 2.0, 'f', 1)));
    } else
        lines.append(tr("我的评分：未评分"));
    lines.append(tr("笔记：%1").arg(notes.note.trimmed().isEmpty() ? tr("（空）") : notes.note.trimmed().toHtmlEscaped()));

    lines.append(QString());
    if (meta.stats.playSeconds > 0) {
        const int minutes = static_cast<int>(meta.stats.playSeconds / 60);
        lines.append(tr("游玩时长：%1").arg(minutes >= 60 ? tr("%1 小时").arg(minutes / 60.0, 0, 'f', 1)
                                                          : tr("%1 分钟").arg(minutes)));
    } else
        lines.append(tr("游玩时长：还没有记录"));
    if (meta.stats.lastPlayed.isValid())
        lines.append(tr("上次游玩：%1").arg(meta.stats.lastPlayed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    if (meta.stats.lastExit == QLatin1String("safe"))
        lines.append(tr("退出状态：正常落盘"));
    else if (meta.stats.lastExit == QLatin1String("crash"))
        lines.append(tr("退出状态：异常退出"));

    lines.append(QString());
    lines.append(tr("实例目录：%1").arg(linked->instanceRoot().toHtmlEscaped()));
    m_mineState->setText(lines.join(QStringLiteral("<br/>")));
}

void MapHallPage::clearReviews()
{
    m_reviews.clear();
    m_reviewMapUuid.clear();
    m_reviewTotal = -1;
    m_reviewTotalKnown = false;
    m_reviewsLoading = false;
    m_reviewNameLabels.clear();

    if (m_reviewLayout != nullptr) {
        while (QLayoutItem* layoutItem = m_reviewLayout->takeAt(0)) {
            if (QWidget* widget = layoutItem->widget())
                widget->deleteLater();
            delete layoutItem;
        }
        /**
         * 清空时连末尾的 stretch 一起删掉了 —— 不补回去的话，评论只有一两条时
         * 卡片会被拉满整个滚动区（头像在顶、正文掉到底部），2026-11-02 用户截图即此。
         */
        m_reviewLayout->addStretch(1);
    }
    if (m_tabs != nullptr)
        m_tabs->setTabText(m_reviewsTabIndex, tr("评论"));
    if (m_reviewMore != nullptr)
        m_reviewMore->setVisible(false);
}

void MapHallPage::requestReviews(int offset)
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size())
        return;

    const HallItem& item = m_items.at(row);
    if (item.mapUuid.isEmpty()) {
        // 没有 uuid 就没法按图筛评论（评论表按 map_uuid 关联，不是 slug）
        m_tabs->setTabText(m_reviewsTabIndex, tr("评论"));
        return;
    }

    if (offset == 0) {
        clearReviews();
        m_reviewMapUuid = item.mapUuid;
    }
    if (m_reviewMapUuid != item.mapUuid)
        return;  // 切换选中后旧的翻页请求直接丢掉

    m_reviewsLoading = true;
    m_tabs->setTabText(m_reviewsTabIndex, offset == 0 ? tr("评论 …") : tr("评论"));
    m_reviewMore->setVisible(false);
    m_api->reviews(item.mapUuid, REVIEWS_PER_PAGE, offset);
}

QWidget* MapHallPage::buildReviewCard(const HallReview& review)
{
    QLabel* nameLabel = nullptr;
    auto* card = makeReviewCard(m_reviewBox, review, &nameLabel);
    // 名字是异步补的：先用 uuid 认人，拿到 users 再改文字
    if (!review.userUuid.isEmpty() && nameLabel != nullptr)
        m_reviewNameLabels.insert(review.userUuid, nameLabel);
    return card;
}

void MapHallPage::onReviews(const QList<HallReview>& rows, int total, bool totalKnown, int offset)
{
    if (m_reviewMapUuid.isEmpty())
        return;
    m_reviewsLoading = false;

    m_reviewTotal = total;
    m_reviewTotalKnown = totalKnown;

    for (const auto& review : rows) {
        m_reviews.append(review);
        // 插在末尾的 stretch 之前，卡片才会从上往下排
        m_reviewLayout->insertWidget(m_reviewLayout->count() - 1, buildReviewCard(review));
    }

    // 标题：知道总数就写出来；浏览器通道拿不到 Content-Range，只写已加载
    if (totalKnown && total >= 0)
        m_tabs->setTabText(m_reviewsTabIndex, tr("评论（%1）").arg(total));
    else if (m_reviews.isEmpty())
        m_tabs->setTabText(m_reviewsTabIndex, tr("评论"));
    else
        m_tabs->setTabText(m_reviewsTabIndex, tr("评论（%1）").arg(m_reviews.size()));

    if (m_reviews.isEmpty()) {
        auto* empty = new QLabel(tr("还没有人写评论。"), m_reviewBox);
        empty->setEnabled(false);
        m_reviewLayout->insertWidget(m_reviewLayout->count() - 1, empty);
    }

    // 还有更多：知道总数就比总数，不知道就按"这一页拿满了"判断
    const bool more = totalKnown ? (total > m_reviews.size()) : (rows.size() >= REVIEWS_PER_PAGE);
    m_reviewMore->setVisible(more && !m_reviews.isEmpty());

    // 名字：缺名字的 uuid 一次性补齐（v1 的 comments 没有外键，只能二次查）
    QStringList missing;
    for (const auto& review : rows) {
        if (!review.userUuid.isEmpty() && review.userName.isEmpty() && !missing.contains(review.userUuid))
            missing.append(review.userUuid);
    }
    if (!missing.isEmpty())
        m_api->userNames(missing);
}

void MapHallPage::onUserNames(const QHash<QString, QString>& names)
{
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (auto* label = m_reviewNameLabels.value(it.key()))
            label->setText(it.value());
    }
}

void MapHallPage::pickLocalArchive()
{
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("选择已下载的地图压缩包"), downloads,
                                                      tr("Minecraft 地图 (*.zip *.rar *.7z *.mcworld);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto scanned = MapArchiveScanner::scan(path);
    QApplication::restoreOverrideCursor();

    if (!scanned) {
        CustomMessageBox::selectable(this, tr("导入地图"), scanned.error(), QMessageBox::Critical)->show();
        return;
    }
    if (scanned->worlds.isEmpty()) {
        CustomMessageBox::selectable(this, tr("导入地图"), tr("这个压缩包里没有找到任何世界。"), QMessageBox::Warning)
            ->show();
        return;
    }

    const auto options = MapImportDialog::ask(*scanned, this);
    if (!options)
        return;

    // 交给对话框「确定」按钮：任务本身是 InstanceTask，走的是同一套 staging 流程
    m_dialog->setSuggestedPack(options->displayName, new MapImportTask(scanned->archivePath, *scanned, *options));
    m_hint->setText(tr("已准备好「%1」。点「确定」就会创建实例并解压存档。").arg(options->displayName));
    m_status->setText(tr("确认后开始导入；导入过程中会先把世界解到临时目录再改名，失败不会留半个存档。"));
}

}  // namespace maps
