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
#include "HallDialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "Application.h"
#include "HallAPI.h"
#include "MapCoverLoader.h"
#include "settings/SettingsObject.h"

namespace maps {

HallDialog::HallDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("地图大厅"));
    resize(900, 560);

    m_api = new HallAPI(this);
    // 设置项里可以换成等价反代（见 docs/atlas 02 §3.6）
    m_api->setEndpoint(APPLICATION->settings()->get("MapHallEndpoint").toString());

    auto* layout = new QVBoxLayout(this);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("搜索地图名 / 系列 / 活动（留空 = 最新发布）"));
    auto* searchButton = new QPushButton(tr("搜索"), this);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(searchButton);
    layout->addLayout(searchRow);

    // 筛选与排序：值全部来自站方数据（facets），不写死枚举
    auto* filterRow = new QHBoxLayout();
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem(tr("最新发布"), QStringLiteral("release_desc"));
    m_sortCombo->addItem(tr("评分最高"), QStringLiteral("rating_desc"));
    m_sortCombo->addItem(tr("最多人玩"), QStringLiteral("popularity_desc"));
    m_sortCombo->addItem(tr("时长最长"), QStringLiteral("playtime_desc"));
    m_sortCombo->addItem(tr("时长最短"), QStringLiteral("playtime_asc"));

    m_seriesCombo = new QComboBox(this);
    m_seriesCombo->addItem(tr("系列：不限"), QString());
    m_difficultyCombo = new QComboBox(this);
    m_difficultyCombo->addItem(tr("难度：不限"), QString());
    m_lengthCombo = new QComboBox(this);
    m_lengthCombo->addItem(tr("时长：不限"), QString());

    filterRow->addWidget(new QLabel(tr("排序"), this));
    filterRow->addWidget(m_sortCombo);
    filterRow->addWidget(m_seriesCombo);
    filterRow->addWidget(m_difficultyCombo);
    filterRow->addWidget(m_lengthCombo);
    filterRow->addStretch(1);
    layout->addLayout(filterRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("名称"), tr("系列"), tr("难度"), tr("评分"), tr("发布时间") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    // 封面缩略图：固定 16:9，行高跟着放大一点
    m_table->setIconSize(QSize(72, 40));
    m_table->verticalHeader()->setDefaultSectionSize(46);
    layout->addWidget(m_table, 1);

    m_loadMore = new QPushButton(tr("加载更多…"), this);
    m_loadMore->setVisible(false);
    layout->addWidget(m_loadMore);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    // 评论：只读、分页、名字按 uuid 补
    m_reviewsStatus = new QLabel(tr("点一行地图看详情与评论"), this);
    layout->addWidget(m_reviewsStatus);
    m_reviews = new QListWidget(this);
    m_reviews->setMaximumHeight(180);
    m_reviews->setWordWrap(true);
    m_reviews->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_reviews);
    m_moreReviews = new QPushButton(tr("加载更多评论"), this);
    m_moreReviews->setVisible(false);
    layout->addWidget(m_moreReviews);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_openButton = buttons->addButton(tr("在浏览器中打开下载页"), QDialogButtonBox::ActionRole);
    m_openButton->setEnabled(false);
    layout->addWidget(buttons);

    connect(searchButton, &QPushButton::clicked, this, &HallDialog::startSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &HallDialog::startSearch);
    connect(m_openButton, &QPushButton::clicked, this, &HallDialog::openSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::cellClicked, this, &HallDialog::showDetails);

    connect(m_api, &HallAPI::searchDone, this, [this](const QList<HallItem>& items) {
        // 分页追加：不清空已有结果，接着往下排
        const bool append = m_loadingMore;
        m_loadingMore = false;

        const int startRow = append ? m_table->rowCount() : 0;
        if (!append) {
            m_items.clear();
            m_details.clear();
            m_table->setRowCount(0);
        }

        // 分页之间服务端排序不保证稳定，同一张图可能回来两次 —— 按 slug 去重
        QSet<QString> seen;
        for (const auto& existing : m_items)
            seen.insert(existing.slug);

        QList<HallItem> fresh;
        fresh.reserve(items.size());
        for (const auto& item : items) {
            if (seen.contains(item.slug))
                continue;
            seen.insert(item.slug);
            fresh.append(item);
        }
        m_items += fresh;
        m_table->setRowCount(m_items.size());

        for (int i = 0; i < fresh.size(); ++i) {
            const int row = startRow + i;
            const auto& item = fresh.at(i);
            m_table->setItem(row, 0, new QTableWidgetItem(item.title));
            m_table->setItem(row, 1, new QTableWidgetItem(item.series));
            m_table->setItem(row, 2, new QTableWidgetItem(item.difficulty));
            m_table->setItem(row, 3, new QTableWidgetItem(item.rating < 0 ? QStringLiteral("-")
                                                                          : QString::number(item.rating, 'f', 1)));
            m_table->setItem(row, 4, new QTableWidgetItem(item.releaseDate));
            if (!item.coverUrl.isEmpty())
                requestCover(item.coverUrl);
        }

        // 返回满一页就当作「可能还有下一页」（PostgREST 的总数只有列表接口才给）
        m_loadMore->setVisible(items.size() >= 40);
        m_status->setText(tr("已显示 %1 张地图。点一行看详情；下载会交给系统浏览器。").arg(m_items.size()));
    });

    connect(m_api, &HallAPI::reviewsDone, this, &HallDialog::onReviews);
    connect(m_api, &HallAPI::userNamesDone, this, &HallDialog::onUserNames);
    connect(m_moreReviews, &QPushButton::clicked, this, &HallDialog::loadMoreReviews);
    connect(m_loadMore, &QPushButton::clicked, this, &HallDialog::loadMoreResults);

    // 封面：与大厅页共用同一个加载器（磁盘缓存 + 浏览器通道）
    auto* covers = MapCoverLoader::instance();
    covers->setNetwork(m_api->network());
    covers->setTransportMode(APPLICATION->settings()->get("MapHallTransport").toString());
    connect(covers, &MapCoverLoader::coverReady, this, [this](const QString& url, const QImage& image) {
        if (!m_coverPending.contains(url))
            return;
        m_coverPending.remove(url);
        m_coverCache.insert(url, QPixmap::fromImage(image).scaled(144, 81, Qt::KeepAspectRatioByExpanding,
                                                                 Qt::SmoothTransformation));
        applyCoverIcon(url);
    });
    connect(covers, &MapCoverLoader::coverFailed, this, [this](const QString& url, const QString&) {
        m_coverPending.remove(url);
    });

    connect(m_api, &HallAPI::detailDone, this, [this](const HallDetail& detail) {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        // 按 slug 存：请求是异步的，回来时用户可能已经点了别的行
        m_details.insert(detail.item.slug, detail);

        // 评论按 map_uuid 关联（不是 slug）
        m_currentMapUuid = detail.item.mapUuid;
        m_reviewRows.clear();
        m_reviewTotal = -1;
        m_reviews->clear();
        m_moreReviews->setVisible(false);
        m_reviewsStatus->setText(tr("正在读取评论…"));
        if (!m_currentMapUuid.isEmpty())
            m_api->reviews(m_currentMapUuid, 6, 0);
        m_openButton->setEnabled(!detail.downloadLinks.isEmpty() || !detail.item.pageUrl.isEmpty());

        // 属性行：照 CTMMaps 详情页那 11 项来，缺的就跳过（不显示 "-0"）
        QStringList facts;
        const auto addFact = [&facts](const QString& label, const QString& value) {
            if (!value.isEmpty())
                facts.append(QStringLiteral("%1：%2").arg(label, value));
        };
        addFact(tr("系列"), detail.item.series);
        addFact(tr("版本"), detail.item.version);
        addFact(tr("难度"), detail.item.difficulty);
        addFact(tr("长度"), detail.item.length);
        addFact(tr("版本类型"), detail.item.edition);
        if (detail.item.objectives >= 0)
            addFact(tr("目标"), QString::number(detail.item.objectives));
        if (detail.item.bonuses >= 0)
            addFact(tr("奖励目标"), QString::number(detail.item.bonuses));
        if (detail.item.rating >= 0)
            addFact(tr("评分"), QString::number(detail.item.rating, 'f', 1));
        if (detail.item.minutes >= 0)
            addFact(tr("平均时长"), tr("%1 分钟").arg(detail.item.minutes));
        if (detail.item.plays >= 0)
            addFact(tr("游玩人数"), QString::number(detail.item.plays));
        addFact(tr("发布"), detail.item.releaseDate);
        if (detail.item.lostMedia)
            addFact(tr("提醒"), tr("原下载已失效"));

        QString description = detail.description;
        if (description.size() > 400)
            description = description.left(400) + QStringLiteral("…");
        if (description.isEmpty())
            description = tr("（没有简介）");

        QString text = facts.join(QStringLiteral("  ·  "));
        if (!text.isEmpty())
            text += QStringLiteral("\n\n");
        text += description;
        m_status->setText(QStringLiteral("%1\n%2").arg(detail.item.title, text));
    });

    connect(m_api, &HallAPI::failed, this, &HallDialog::onFailed);
    connect(m_api, &HallAPI::facetsDone, this, &HallDialog::applyFacets);

    // 打开时把筛选选项取回来（失败不打扰用户，只是下拉框里没有值）
    m_api->facets();
}

void HallDialog::applyFacets(const HallFacets& facets)
{
    // 重新拉 facets 时不重复堆积
    const auto refill = [](QComboBox* combo, const QString& placeholder, const QStringList& values) {
        const QString current = combo->currentData().toString();
        combo->clear();
        combo->addItem(placeholder, QString());
        for (const auto& value : values)
            combo->addItem(value, value);
        const int index = combo->findData(current);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };

    refill(m_seriesCombo, tr("系列：不限"), facets.series);
    refill(m_difficultyCombo, tr("难度：不限"), facets.difficulties);
    refill(m_lengthCombo, tr("时长：不限"), facets.lengths);
}

HallQuery HallDialog::currentQuery() const
{
    HallQuery query = m_query;
    query.text = m_searchEdit->text().trimmed();
    query.sort = m_sortCombo->currentData().toString();
    query.series = m_seriesCombo->currentData().toString();

    const QString difficulty = m_difficultyCombo->currentData().toString();
    if (!difficulty.isEmpty())
        query.difficulties = { difficulty };
    const QString length = m_lengthCombo->currentData().toString();
    if (!length.isEmpty())
        query.lengths = { length };

    // 列表页比详情页便宜，一屏多取一些（站方上限 60）
    query.limit = 40;
    return query;
}

void HallDialog::startSearch()
{
    HallQuery query = currentQuery();
    query.offset = 0;
    m_loadingMore = false;
    m_loadMore->setVisible(false);

    m_status->setText(tr("正在查询…"));
    m_api->search(query);
}

void HallDialog::loadMoreResults()
{
    if (m_items.isEmpty())
        return;

    HallQuery query = currentQuery();
    query.offset = static_cast<int>(m_items.size());
    m_loadingMore = true;

    m_status->setText(tr("正在加载更多…"));
    m_api->search(query);
}

void HallDialog::showDetails(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_items.size())
        return;
    m_openButton->setEnabled(true);
    m_api->detail(m_items.at(row).slug);
}

void HallDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_items.size())
        return;

    // 优先用当前这一行的详情里的直链；没有详情就退回到站点页面
    QString url = m_items.at(row).pageUrl;
    const auto detail = m_details.constFind(m_items.at(row).slug);
    if (detail != m_details.constEnd() && !detail->downloadLinks.isEmpty())
        url = detail->downloadLinks.first();

    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void HallDialog::loadMoreReviews()
{
    if (m_currentMapUuid.isEmpty())
        return;
    m_api->reviews(m_currentMapUuid, 6, static_cast<int>(m_reviewRows.size()));
}

void HallDialog::onReviews(const QList<HallReview>& rows, int total, bool totalKnown, int offset)
{
    if (offset == 0) {
        m_reviewRows.clear();
        m_reviews->clear();
    }
    m_reviewTotal = totalKnown ? total : -1;

    QStringList unknown;
    for (const auto& review : rows) {
        m_reviewRows.append(review);
        if (!review.userUuid.isEmpty())
            unknown.append(review.userUuid);
    }

    // 先把评论画出来（名字暂时「匿名玩家」），用户名回来再改
    for (const auto& review : rows) {
        QString text = QStringLiteral("%1  %2").arg(review.playedDate, QStringLiteral("· 匿名玩家"));
        if (review.rating >= 0)
            text += QStringLiteral(" · %1 星").arg(review.rating);
        if (review.playtimeMinutes >= 0)
            text += QStringLiteral(" · %1 分钟").arg(review.playtimeMinutes);
        if (review.speedrun)
            text += QStringLiteral(" · 速通");
        if (!review.text.isEmpty())
            text += QStringLiteral("\n") + review.text;
        auto* item = new QListWidgetItem(text, m_reviews);
        item->setData(Qt::UserRole, review.userUuid);
    }

    if (m_reviewRows.isEmpty()) {
        m_reviewsStatus->setText(tr("这张图还没有评论。"));
        m_moreReviews->setVisible(false);
    } else if (m_reviewTotal >= 0) {
        m_reviewsStatus->setText(tr("已显示 %1 / 共 %2 条").arg(m_reviewRows.size()).arg(m_reviewTotal));
        m_moreReviews->setVisible(m_reviewRows.size() < m_reviewTotal);
    } else {
        // 拿不到总数就只说「已显示 N 条」，不编一个数字
        m_reviewsStatus->setText(tr("已显示 %1 条").arg(m_reviewRows.size()));
        m_moreReviews->setVisible(rows.size() >= 6);
    }

    if (!unknown.isEmpty())
        m_api->userNames(unknown);
}

void HallDialog::onUserNames(const QHash<QString, QString>& nameByUuid)
{
    for (int i = 0; i < m_reviews->count() && i < m_reviewRows.size(); ++i) {
        auto* item = m_reviews->item(i);
        const QString name = nameByUuid.value(item->data(Qt::UserRole).toString());
        if (name.isEmpty())
            continue;
        item->setText(QString(item->text()).replace(QStringLiteral("匿名玩家"), name));
    }
}

void HallDialog::requestCover(const QString& url)
{
    if (url.isEmpty() || m_coverCache.contains(url) || m_coverPending.contains(url))
        return;

    auto* covers = MapCoverLoader::instance();
    if (const auto image = covers->cached(url); !image.isNull()) {
        m_coverCache.insert(url, QPixmap::fromImage(image).scaled(144, 81, Qt::KeepAspectRatioByExpanding,
                                                                 Qt::SmoothTransformation));
        applyCoverIcon(url);
        return;
    }
    if (covers->isKnownBad(url))
        return;

    m_coverPending.insert(url);
    covers->request(url);
}

void HallDialog::applyCoverIcon(const QString& url)
{
    const auto pixmap = m_coverCache.value(url);
    if (pixmap.isNull())
        return;
    // 把缩略图回填到所有引用这个 url 的行（同名封面可能出现在多行）
    for (int row = 0; row < m_table->rowCount() && row < m_items.size(); ++row) {
        if (m_items.at(row).coverUrl != url)
            continue;
        if (auto* item = m_table->item(row, 0); item != nullptr)
            item->setIcon(QIcon(pixmap));
    }
}

void HallDialog::onFailed(HallError kind, const QString& humanReason, const QString& techDetail)
{
    QString text = humanReason;
    if (!techDetail.isEmpty())
        text += QStringLiteral("\n\n（技术细节：%1）").arg(techDetail);
    m_status->setText(text);
    // 断网时明确告诉用户还能干什么，而不是只留一个空列表
    if (kind == HallError::Dns || kind == HallError::Tls || kind == HallError::Timeout) {
        m_status->setText(text + tr("\n大厅只是「发现地图」的入口；本地导入不受影响。"));
    }
}

}  // namespace maps
