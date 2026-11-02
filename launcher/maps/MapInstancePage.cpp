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
#include "MapInstancePage.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

#include "FileSystem.h"
#include "HallAPI.h"
#include "HallCache.h"
#include "MapCoverLoader.h"
#include "MapHallLink.h"
#include "MapImportTask.h"
#include "MapRepository.h"
#include "MapTheme.h"
#include "MapReviewCard.h"
#include "StarRatingWidget.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "minecraft/MinecraftInstance.h"

namespace maps {
namespace {
/// 简介页封面：16:9，和站上的 1920×1080 同比例
constexpr int ABOUT_COVER_W = 200;
constexpr int ABOUT_COVER_H = 113;

/// 信息卡片：外观与评论卡片一致（MapTheme::ui::cardStyle），标题可选
QFrame* makeCard(QWidget* parent, const QString& title, QVBoxLayout** bodyOut = nullptr)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("mapCard"));
    card->setStyleSheet(ui::cardStyle());

    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);
    if (!title.isEmpty()) {
        auto* head = new QLabel(title, card);
        QFont headFont = head->font();
        headFont.setBold(true);
        head->setFont(headFont);
        outer->addWidget(head);
    }
    auto* body = new QVBoxLayout();
    body->setSpacing(6);
    outer->addLayout(body);
    if (bodyOut != nullptr)
        *bodyOut = body;
    return card;
}

/// 表单左侧的字段名：次要色，和值拉开层次
QLabel* makeKey(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    return label;
}
}  // namespace


MapInstancePage::MapInstancePage(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);

    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_badge = new QLabel(this);
    layout->addWidget(m_badge);

    /**
     * 「我的」页容器：下面的内容**直接建进这一页**。
     *
     * 踩过的坑：先建到顶层、再用 takeAt 搬进页签，结果 QFormLayout 失去了
     * 正确的拉伸约束，行与行被撑开一大截（用户截图里「中文名」和输入框
     * 隔了半屏）。直接建进容器就没有搬运这一步，也就没有能丢的约束。
     */
    /**
     * 「我的」页：三张卡片（我的记录 / 来源与统计 / 版本判定依据）+ 底部动作。
     * 卡片外观与评论一致（MapTheme::ui::cardStyle），字段名用次要色、值靠右占满剩余宽度。
     *
     * 注意内容**直接建进这一页**：以前先建到顶层再用 takeAt 搬进页签，QFormLayout
     * 会丢掉拉伸约束，行距被撑开半屏（见 §5.2 第 48 条）。
     */
    auto* minePage = new QWidget(this);
    auto* mineLayout = new QVBoxLayout(minePage);
    mineLayout->setContentsMargins(4, 8, 4, 4);
    mineLayout->setSpacing(10);

    // ── 卡片 1：我的记录（notes.json）：中文名 / 我的评分 / 笔记 ──
    // 与 map.json 分开存：那是"这张图是什么"，这是"我怎么看这张图"（照 AIO 的 UserNote）。
    // 所以重装地图不会冲掉这里的东西。
    QVBoxLayout* notesBody = nullptr;
    mineLayout->addWidget(makeCard(minePage, tr("我的记录"), &notesBody));

    auto* notesGrid = new QGridLayout();
    notesGrid->setHorizontalSpacing(12);
    notesGrid->setVerticalSpacing(8);
    notesGrid->setColumnStretch(1, 1);

    notesGrid->addWidget(makeKey(tr("中文名"), minePage), 0, 0);
    auto* aliasRow = new QHBoxLayout();
    aliasRow->setSpacing(6);
    m_aliasEdit = new QLineEdit(minePage);
    m_aliasEdit->setMaxLength(MapNotes::MAX_ALIAS);
    m_aliasEdit->setPlaceholderText(tr("留空 = 用导入时识别出的标题"));
    m_aliasSave = new QPushButton(tr("保存"), minePage);
    m_aliasSave->setEnabled(false);
    m_aliasState = new QLabel(minePage);
    aliasRow->addWidget(m_aliasEdit, 1);
    aliasRow->addWidget(m_aliasSave);
    aliasRow->addWidget(m_aliasState);
    notesGrid->addLayout(aliasRow, 0, 1);

    notesGrid->addWidget(makeKey(tr("我的评分"), minePage), 1, 0, Qt::AlignVCenter);
    auto* ratingRow = new QHBoxLayout();
    ratingRow->setSpacing(6);
    m_stars = new StarRatingWidget(minePage);
    m_ratingValue = new QLabel(minePage);
    auto* clearRating = new QPushButton(tr("清除"), minePage);
    ratingRow->addWidget(m_stars);
    ratingRow->addWidget(m_ratingValue);
    ratingRow->addWidget(clearRating);
    ratingRow->addStretch(1);
    notesGrid->addLayout(ratingRow, 1, 1);

    notesGrid->addWidget(makeKey(tr("笔记"), minePage), 2, 0, Qt::AlignTop);
    auto* noteColumn = new QVBoxLayout();
    noteColumn->setSpacing(4);
    m_noteEdit = new QPlainTextEdit(minePage);
    m_noteEdit->setPlaceholderText(tr("记点什么：卡在哪、该装哪些模组、下次从哪继续…"));
    m_noteEdit->setMinimumHeight(88);
    m_noteEdit->setMaximumHeight(130);
    auto* noteFooter = new QHBoxLayout();
    m_noteCount = new QLabel(minePage);
    m_noteState = new QLabel(minePage);
    m_noteSave = new QPushButton(tr("保存笔记"), minePage);
    m_noteSave->setEnabled(false);
    noteFooter->addWidget(m_noteCount);
    noteFooter->addStretch(1);
    noteFooter->addWidget(m_noteState);
    noteFooter->addWidget(m_noteSave);
    noteColumn->addWidget(m_noteEdit);
    noteColumn->addLayout(noteFooter);
    notesGrid->addLayout(noteColumn, 2, 1);

    notesBody->addLayout(notesGrid);

    connect(m_aliasSave, &QPushButton::clicked, this, [this] {
        m_notes.alias = m_aliasEdit->text().trimmed();
        saveNotes();
    });
    connect(m_aliasEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_aliasSave->setEnabled(text.trimmed() != m_notes.alias);
    });
    m_stars->onChanged = [this](int value) {
        m_notes.rating = value;
        saveNotes();
    };
    connect(clearRating, &QPushButton::clicked, this, [this] {
        m_stars->setValue(0);
        if (m_stars->onChanged)
            m_stars->onChanged(0);
    });
    connect(m_noteEdit, &QPlainTextEdit::textChanged, this, [this] {
        QString text = m_noteEdit->toPlainText();
        if (text.size() > MapNotes::MAX_NOTE) {
            // QPlainTextEdit 没有 maxLength：硬截断，但别让信号打回来
            const QSignalBlocker blocker(m_noteEdit);
            text = text.left(MapNotes::MAX_NOTE);
            m_noteEdit->setPlainText(text);
        }
        m_noteCount->setText(tr("%1 / %2").arg(text.size()).arg(MapNotes::MAX_NOTE));
        m_noteSave->setEnabled(text != m_notes.note);
    });
    connect(m_noteSave, &QPushButton::clicked, this, [this] {
        m_notes.note = m_noteEdit->toPlainText();
        saveNotes();
    });

    // ── 卡片 2：来源与统计 ──
    QVBoxLayout* sourceBody = nullptr;
    mineLayout->addWidget(makeCard(minePage, tr("来源与统计"), &sourceBody));

    auto* sourceGrid = new QGridLayout();
    sourceGrid->setHorizontalSpacing(12);
    sourceGrid->setVerticalSpacing(8);
    sourceGrid->setColumnStretch(1, 1);

    sourceGrid->addWidget(makeKey(tr("来源压缩包"), minePage), 0, 0, Qt::AlignTop);
    m_archive = new QLabel(minePage);
    m_archive->setWordWrap(true);
    m_archive->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceGrid->addWidget(m_archive, 0, 1);

    sourceGrid->addWidget(makeKey(tr("游玩统计"), minePage), 1, 0, Qt::AlignTop);
    m_stats = new QLabel(minePage);
    m_stats->setWordWrap(true);
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceGrid->addWidget(m_stats, 1, 1);

    sourceBody->addLayout(sourceGrid);

    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setVisible(false);
    mineLayout->addWidget(m_notice);

    // ── 卡片 3：版本判定依据 ──
    QVBoxLayout* evidenceBody = nullptr;
    mineLayout->addWidget(makeCard(minePage, tr("版本判定依据"), &evidenceBody));
    m_evidence = new QLabel(minePage);
    m_evidence->setWordWrap(true);
    m_evidence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    evidenceBody->addWidget(m_evidence);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* openInstance = new QPushButton(tr("打开实例目录"), this);
    auto* openArchive = new QPushButton(tr("打开压缩包所在目录"), this);
    auto* reinstall = new QPushButton(tr("重装地图…"), this);
    buttons->addWidget(openInstance);
    buttons->addWidget(openArchive);
    buttons->addWidget(reinstall);
    buttons->addStretch(1);
    mineLayout->addLayout(buttons);
    mineLayout->addStretch(1);

    connect(openInstance, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_instance->instanceRoot()));
    });
    connect(reinstall, &QPushButton::clicked, this, &MapInstancePage::reinstallWorlds);
    connect(openArchive, &QPushButton::clicked, this, [this] {
        const QString path = MapRepository::resolveArchivePath(m_meta);
        if (path.isEmpty())
            return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });

    /**
     * 三个页签：简介 / 评论 / 我的。
     *
     * 前两页来自大厅本地库（`hall-cache.json`），第三页是本地信息（上面直接建好的）。
     */
    {
        m_tabs = new QTabWidget(this);
        m_tabs->setDocumentMode(true);
        m_tabs->addTab(buildAboutPage(), tr("简介"));
        m_tabs->addTab(buildReviewsPage(), tr("评论"));
        m_tabs->addTab(minePage, tr("我的"));
        layout->addWidget(m_tabs, 1);

        // 切到「评论」页才去拉（见 refreshHallTabs 里的说明）
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (index != 1 || m_reviewsRequested || m_hallUuid.isEmpty() || m_hall == nullptr)
                return;
            m_reviewsRequested = true;
            m_reviewsState->setText(tr("正在读取评论…"));
            m_hall->reviews(m_hallUuid, 20, 0);
        });
    }

    reload();
}

QString MapInstancePage::displayName() const
{
    return tr("信息");
}

QIcon MapInstancePage::icon() const
{
    auto icon = QIcon::fromTheme(QStringLiteral("worlds"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("world"));
    return icon;
}

bool MapInstancePage::shouldDisplay() const
{
    return m_instance != nullptr && QFile::exists(FS::PathCombine(m_instance->instanceRoot(), MapMeta::fileName()));
}

void MapInstancePage::openedImpl()
{
    reload();
}

QWidget* MapInstancePage::buildAboutPage()
{
    m_aboutPage = new QWidget(this);
    m_aboutLayout = new QVBoxLayout(m_aboutPage);
    m_aboutLayout->setContentsMargins(4, 4, 4, 4);
    m_aboutLayout->setSpacing(8);

    /**
     * 顶部一条「封面在左、基本信息在右」：
     * 站上简介页的封面是 16:9 大图，放左边当视觉锚点；作者/版本/难度/长度这些
     * 短字段放右侧，竖排一列比横着铺开更好扫（2026-11-02 用户要求）。
     */
    auto* hero = new QHBoxLayout();
    hero->setContentsMargins(0, 0, 0, 0);
    hero->setSpacing(14);
    // 整组（封面 + 信息）在页面里水平居中，封面相对信息列垂直居中（用户反馈：以前贴着左边、上下不齐）
    hero->addStretch(1);

    m_aboutCover = new QLabel(m_aboutPage);
    m_aboutCover->setFixedSize(ABOUT_COVER_W, ABOUT_COVER_H);
    m_aboutCover->setAlignment(Qt::AlignCenter);
    m_aboutCover->setStyleSheet(QStringLiteral("border: 1px solid rgba(127, 127, 127, 55);"));
    m_aboutCover->setPixmap(MapCoverLoader::placeholderCover(QStringLiteral("?"), ABOUT_COVER_W, ABOUT_COVER_H));
    hero->addWidget(m_aboutCover, 0, Qt::AlignVCenter);

    m_aboutInfo = new QLabel(m_aboutPage);
    m_aboutInfo->setWordWrap(true);
    m_aboutInfo->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_aboutInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 给信息列一个稳定的宽度区间：太窄字段会折行，太宽整组就不像"一块"了
    m_aboutInfo->setMinimumWidth(340);
    m_aboutInfo->setMaximumWidth(560);
    hero->addWidget(m_aboutInfo, 0);
    hero->addStretch(1);
    m_aboutLayout->addLayout(hero);

    // 封面是异步取的：缓存命中会立刻回，没命中走浏览器通道，回来再换图
    connect(MapCoverLoader::instance(), &MapCoverLoader::coverReady, this,
            [this](const QString& url, const QImage& image) {
                if (!m_aboutCoverUrl.isEmpty() && url == m_aboutCoverUrl)
                    applyAboutCover(image);
            });

    // 简介正文：单独一张卡片，和评论卡片同一套外观
    QVBoxLayout* descBody = nullptr;
    m_aboutLayout->addWidget(makeCard(m_aboutPage, QString(), &descBody));
    m_aboutState = new QLabel(m_aboutPage);
    m_aboutState->setWordWrap(true);
    m_aboutState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_aboutState->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descBody->addWidget(m_aboutState);

    auto* linkRow = new QHBoxLayout();
    m_aboutLinkButton = new QPushButton(tr("关联到 CTM 地图…"), m_aboutPage);
    connect(m_aboutLinkButton, &QPushButton::clicked, this, &MapInstancePage::pickHallMap);
    m_aboutUnlinkButton = new QPushButton(tr("解除关联"), m_aboutPage);
    m_aboutUnlinkButton->setVisible(false);
    connect(m_aboutUnlinkButton, &QPushButton::clicked, this, [this] {
        if (m_instance == nullptr)
            return;
        MapHallLink::clear(m_instance->instanceRoot());
        m_link = MapHallLink{};
        refreshHallTabs();
    });
    linkRow->addWidget(m_aboutLinkButton);
    linkRow->addWidget(m_aboutUnlinkButton);
    linkRow->addStretch(1);
    m_aboutLayout->addLayout(linkRow);
    m_aboutLayout->addStretch(1);
    return m_aboutPage;
}

void MapInstancePage::requestAboutCover(const QString& url, const QString& title)
{
    if (m_aboutCover == nullptr)
        return;

    m_aboutCoverUrl = url;
    m_aboutCover->setPixmap(MapCoverLoader::placeholderCover(title, ABOUT_COVER_W, ABOUT_COVER_H));
    if (url.isEmpty())
        return;

    auto* covers = MapCoverLoader::instance();
    if (const auto image = covers->cached(url); !image.isNull()) {
        applyAboutCover(image);
        return;
    }
    covers->request(url);
}

void MapInstancePage::applyAboutCover(const QImage& image)
{
    if (m_aboutCover == nullptr || image.isNull())
        return;

    const QPixmap scaled = QPixmap::fromImage(image).scaled(ABOUT_COVER_W, ABOUT_COVER_H,
                                                           Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_aboutCover->setPixmap(scaled.copy((scaled.width() - ABOUT_COVER_W) / 2, (scaled.height() - ABOUT_COVER_H) / 2,
                                        ABOUT_COVER_W, ABOUT_COVER_H));
}

QWidget* MapInstancePage::buildReviewsPage()
{
    m_reviewsPage = new QWidget(this);
    auto* outer = new QVBoxLayout(m_reviewsPage);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_reviewsState = new QLabel(m_reviewsPage);
    m_reviewsState->setWordWrap(true);
    outer->addWidget(m_reviewsState);

    auto* scroll = new QScrollArea(m_reviewsPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    m_reviewsLayout = new QVBoxLayout(body);
    m_reviewsLayout->setContentsMargins(0, 0, 4, 0);
    m_reviewsLayout->setSpacing(10);
    m_reviewsLayout->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    m_reviewsMore = new QPushButton(tr("加载更多评论"), m_reviewsPage);
    m_reviewsMore->setVisible(false);
    connect(m_reviewsMore, &QPushButton::clicked, this, [this] {
        if (m_hallUuid.isEmpty() || m_hall == nullptr)
            return;
        m_reviewsMore->setEnabled(false);
        m_hall->reviews(m_hallUuid, 20, m_reviewsShown);
    });
    outer->addWidget(m_reviewsMore);
    return m_reviewsPage;
}

void MapInstancePage::ensureHallApi()
{
    if (m_hall != nullptr)
        return;

    m_hall = new HallAPI(this);
    connect(m_hall, &HallAPI::detailDone, this, [this](const HallDetail& detail) {
        if (detail.item.slug != m_hallSlug || m_aboutState == nullptr)
            return;
        QStringList lines;
        const auto& item = detail.item;
        const auto add = [&lines](const QString& key, const QString& value) {
            if (!value.isEmpty())
                lines.append(QStringLiteral("<b>%1</b>：%2").arg(key.toHtmlEscaped(), value.toHtmlEscaped()));
        };
        add(tr("系列"), item.series);
        add(tr("作者"), item.author);
        add(tr("版本"), item.version);
        add(tr("难度"), item.difficulty);
        add(tr("长度"), item.length);
        add(tr("类型"), item.edition);
        if (item.rating >= 0)
            add(tr("评分"), QString::number(item.rating, 'f', 1));
        if (item.minutes > 0)
            add(tr("平均时长"), item.minutes >= 60 ? tr("%1 小时").arg(item.minutes / 60.0, 0, 'f', 1)
                                                  : tr("%1 分钟").arg(item.minutes));
        if (item.plays > 0)
            add(tr("游玩人数"), QString::number(item.plays));
        add(tr("发布"), item.releaseDate);

        m_aboutInfo->setText(lines.join(QStringLiteral("<br/>")));
        const QString description = detail.description.trimmed();
        m_aboutState->setText(description.isEmpty() ? tr("（站上没写简介）")
                                                    : description.toHtmlEscaped().replace("\n", "<br/>"));
        requestAboutCover(item.coverUrl, item.title.isEmpty() ? m_link.title : item.title);
    });
    connect(m_hall, &HallAPI::reviewsDone, this,
            [this](const QList<HallReview>& rows, int total, bool totalKnown, int offset) {
                if (m_reviewsLayout == nullptr)
                    return;
                m_reviewsMore->setEnabled(true);
                m_reviewsShown += rows.size();

                for (const auto& review : rows) {
                    // 与大厅详情共用同一张卡片，样式不会各走各的
                    auto* card = makeReviewCard(m_reviewsPage, review);
                    m_reviewsLayout->insertWidget(m_reviewsLayout->count() - 1, card);
                }

                m_reviewsState->setText(totalKnown && total >= 0
                                            ? tr("站上共 %1 条评论，已显示 %2 条。").arg(total).arg(m_reviewsShown)
                                            : tr("已显示 %1 条评论。").arg(m_reviewsShown));
                m_reviewsMore->setVisible(totalKnown ? total > m_reviewsShown : rows.size() >= 20);
            });
    connect(m_hall, &HallAPI::failed, this, [this](HallError, const QString& reason, const QString&) {
        if (m_aboutState != nullptr)
            m_aboutState->setText(tr("读取这张图的简介失败：%1").arg(reason));
    });
}

void MapInstancePage::linkTo(const HallItem& item)
{
    if (m_instance == nullptr)
        return;

    m_link.slug = item.slug;
    m_link.mapUuid = item.mapUuid;
    m_link.title = item.title;
    m_link.linkedAt = QDateTime::currentDateTimeUtc();
    QString error;
    if (!m_link.save(m_instance->instanceRoot(), &error)) {
        CustomMessageBox::selectable(this, tr("关联失败"), error, QMessageBox::Critical)->show();
        return;
    }
    refreshHallTabs();
}

void MapInstancePage::pickHallMap()
{
    ensureHallApi();
    const auto items = m_hall->libraryItems();
    if (items.isEmpty()) {
        CustomMessageBox::selectable(
            this, tr("还没有本地地图库"),
            tr("大厅数据还没下载。打开「添加实例 → CTM 地图」，点一次「下载地图库」，回来就能在这里关联。"),
            QMessageBox::Information)
            ->show();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("关联到 CTM 地图"));
    dialog.resize(520, 520);
    auto* layout = new QVBoxLayout(&dialog);

    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(tr("按标题筛选…"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);

    auto* list = new QListWidget(&dialog);
    layout->addWidget(list, 1);

    const auto refill = [list, items](const QString& text) {
        list->clear();
        const QString needle = text.trimmed().toLower();
        for (const auto& item : items) {
            if (!needle.isEmpty() && !item.title.toLower().contains(needle))
                continue;
            auto* row = new QListWidgetItem(item.title, list);
            row->setData(Qt::UserRole, item.slug);
            row->setToolTip(QStringLiteral("%1 · %2").arg(item.slug, item.author));
        }
    };
    refill({});

    connect(filter, &QLineEdit::textChanged, &dialog, [refill](const QString& text) { refill(text); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto* current = list->currentItem();
    if (current == nullptr)
        return;

    const QString slug = current->data(Qt::UserRole).toString();
    for (const auto& item : items) {
        if (item.slug == slug) {
            linkTo(item);
            return;
        }
    }
}

void MapInstancePage::refreshHallTabs()
{
    if (m_aboutState == nullptr || m_instance == nullptr)
        return;

    ensureHallApi();

    // 清空上一张图的评论
    if (m_reviewsLayout != nullptr) {
        while (m_reviewsLayout->count() > 1) {
            QLayoutItem* item = m_reviewsLayout->takeAt(0);
            if (auto* widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    }
    m_reviewsShown = 0;
    m_reviewsRequested = false;
    m_reviewsMore->setVisible(false);
    m_hallSlug.clear();
    m_hallUuid.clear();

    if (!m_link.isValid()) {
        /**
         * 没关联：先把话说清楚（"还没链接到大数据库"），再给一条能走的建议。
         * 建议来自本地库的标题匹配，不联网。
         */
        const QString suggestion = MapHallLink::suggestSlugFor(m_title->text().isEmpty()
                                                                  ? m_instance->name()
                                                                  : m_title->text());
        QString text = tr("这个实例还没关联到 CTM 地图。\n\n"
                          "关联之后，这里会显示这张图在站上的简介、评分和玩家评论；\n"
                          "关联只写一个 hall-link.json，不会改动存档或实例配置。");
        if (!suggestion.isEmpty())
            text += tr("\n\n本地库里有一张《%1》看起来就是它 —— 点下面的按钮即可关联。").arg(suggestion);
        m_aboutState->setText(text);
        if (m_aboutInfo != nullptr)
            m_aboutInfo->clear();
        requestAboutCover({}, m_title->text().isEmpty() ? m_instance->name() : m_title->text());
        m_aboutLinkButton->setText(suggestion.isEmpty() ? tr("关联到 CTM 地图…") : tr("关联到《%1》").arg(suggestion));
        // 按钮点击时如果已经有建议，直接用它（先把所有旧连接断干净，别越连越多）
        disconnect(m_aboutLinkButton, nullptr, this, nullptr);
        m_aboutUnlinkButton->setVisible(false);
        if (suggestion.isEmpty()) {
            connect(m_aboutLinkButton, &QPushButton::clicked, this, &MapInstancePage::pickHallMap);
        } else {
            connect(m_aboutLinkButton, &QPushButton::clicked, this, [this, suggestion] {
                for (const auto& item : m_hall->libraryItems()) {
                    if (item.slug == suggestion) {
                        linkTo(item);
                        return;
                    }
                }
                pickHallMap();
            });
        }
        m_aboutLinkButton->setVisible(true);

        m_reviewsState->setText(tr("还没关联到 CTM 地图，所以没有评论可以显示。"));
        m_tabs->setTabText(1, tr("评论"));
        return;
    }

    m_aboutLinkButton->setText(tr("换一张…"));
    disconnect(m_aboutLinkButton, nullptr, this, nullptr);
    connect(m_aboutLinkButton, &QPushButton::clicked, this, &MapInstancePage::pickHallMap);
    m_aboutLinkButton->setVisible(true);
    m_aboutUnlinkButton->setVisible(true);

    m_hallSlug = m_link.slug;
    m_hallUuid = m_link.mapUuid;

    if (!m_hall->hasLibrary()) {
        m_aboutState->setText(tr("已关联到《%1》，但本地还没有地图库（简介和评论存在库里）。\n"
                                 "打开「添加实例 → CTM 地图」点一次「下载地图库」即可。")
                                  .arg(m_link.title));
        if (m_aboutInfo != nullptr)
            m_aboutInfo->clear();
        requestAboutCover({}, m_link.title);
        m_reviewsState->setText(tr("还没有本地地图库，暂时没有评论可显示。"));
        return;
    }

    m_aboutState->setText(tr("正在读取《%1》的简介…").arg(m_link.title));
    if (m_aboutInfo != nullptr)
        m_aboutInfo->clear();
    requestAboutCover({}, m_link.title);
    m_hall->detail(m_link.slug);

    /**
     * 评论按需拉：本地库只装了"最近 1000 条"，某张图往往落在窗口外，
     * 这时候要发一次网络请求（几秒）。所以等用户真的点开「评论」页再去拉，
     * 打开页面本身不会卡住。
     */
    m_reviewsRequested = false;
    m_reviewsState->setText(tr("点开「评论」页时再读取。"));
    if (m_tabs != nullptr && m_tabs->currentIndex() == 1)
        m_reviewsRequested = true, m_hall->reviews(m_link.mapUuid, 20, 0);
}

void MapInstancePage::reload()
{
    if (m_instance == nullptr)
        return;

    m_meta = MapMeta::load(m_instance->instanceRoot());
    m_notes = MapNotes::load(m_instance->instanceRoot());
    applyNotesToUi();

    m_title->setText(m_meta.title.isEmpty() ? m_instance->name() : m_meta.title);

    QStringList badge{ m_meta.version.mcVersion };
    if (!m_meta.loader.type.isEmpty() && m_meta.loader.type != QLatin1String("vanilla")) {
        badge.append(m_meta.loader.version.isEmpty() ? m_meta.loader.type
                                                     : QStringLiteral("%1 %2").arg(m_meta.loader.type, m_meta.loader.version));
    }
    badge.append(tr("置信度 %1").arg(confidenceToString(m_meta.version.confidence)));
    m_badge->setText(badge.join(QStringLiteral(" · ")));

    /**
     * 导入即移动（2026-11-02 起）：压缩包只有库里一份，`libraryCopy` 就是本体。
     * `archive.path` 退化为"导入前在哪"的留痕 —— 文件已经不在那儿了，只作参考。
     */
    const QString resolvedArchive = MapRepository::resolveArchivePath(m_meta);
    QString archiveText = resolvedArchive.isEmpty() ? tr("（找不到压缩包文件）") : resolvedArchive;
    if (!resolvedArchive.isEmpty() && !m_meta.archive.path.isEmpty() &&
        m_meta.archive.path != resolvedArchive && QFileInfo::exists(m_meta.archive.path))
        archiveText += QStringLiteral("\n") + tr("（原始位置：%1）").arg(m_meta.archive.path);
    if (m_meta.archive.size > 0)
        archiveText += QStringLiteral("\n") + tr("体积：%1 MiB").arg(m_meta.archive.size / 1024.0 / 1024.0, 0, 'f', 1);
    m_archive->setText(archiveText);

    const qint64 minutes = m_meta.stats.playSeconds / 60;
    QString statsText = tr("%1 分钟").arg(minutes);
    if (m_meta.stats.lastPlayed.isValid())
        statsText += tr(" · 上次游玩 %1").arg(m_meta.stats.lastPlayed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (!m_meta.stats.lastExit.isEmpty()) {
        const QString exit = m_meta.stats.lastExit;
        statsText += tr(" · 上次退出：%1")
                         .arg(exit == QLatin1String("safe") ? tr("正常落盘")
                              : exit == QLatin1String("crash") ? tr("异常（未确认落盘）")
                                                               : tr("未判定"));
    }
    m_stats->setText(statsText);

    m_evidence->setText(m_meta.version.evidence.isEmpty() ? tr("（没有判定证据）")
                                                          : m_meta.version.evidence.join(QStringLiteral("\n")));

    QStringList notices;
    if (!m_meta.experimentalFeatures.isEmpty()) {
        notices.append(tr("这张图保存时开着实验性设置（%1）。游戏打开前会自己弹一次确认框，"
                          "这是游戏的安全检查，启动器不会替你点掉。")
                           .arg(m_meta.experimentalFeatures.join(QStringLiteral(", "))));
    }
    if (m_meta.loader.type != QLatin1String("vanilla") && m_meta.loader.version.isEmpty()) {
        notices.append(tr("存档来自 %1 环境，但导入时没有指定加载器版本 —— 需要到「版本」页安装 %1，"
                          "否则模组方块会被原版客户端删掉。")
                           .arg(m_meta.loader.type));
    }
    if (!m_meta.requiredMods.isEmpty()) {
        const QStringList shown = m_meta.requiredMods.mid(0, 8);
        QString list = shown.join(QStringLiteral(", "));
        if (m_meta.requiredMods.size() > shown.size())
            list += tr(" 等 %1 个").arg(m_meta.requiredMods.size());
        notices.append(tr("存档声明需要这些模组：%1").arg(list));
    }
    if (m_meta.requiresExternalModpack) {
        // 只有存档自己声明了模组依赖才说这句；"作者用 Forge/Fabric 保存过"不算
        notices.append(tr("存档声明依赖模组（压缩包里没带 mods/）：需要自备这些模组，"
                          "先在「版本」页装 Forge 加载器，再把模组放进 mods/。"));
    }
    m_notice->setText(notices.join(QStringLiteral("\n\n")));
    m_notice->setVisible(!notices.isEmpty());
    // 颜色跟随主题：深色底下用 #c62828 几乎读不出来
    m_notice->setStyleSheet(QStringLiteral("color: %1;").arg(ui::noticeColorName()));

    // 大厅联动：关联存在就拉那两张图；没有就给出"可能是哪张"的建议
    m_link = MapHallLink::load(m_instance->instanceRoot());
    refreshHallTabs();
}

void MapInstancePage::reinstallWorlds()
{
    if (m_instance == nullptr)
        return;

    if (m_instance->isRunning()) {
        CustomMessageBox::selectable(this, tr("重装地图"), tr("这个地图正在运行。先退出游戏再重装 —— 否则会把正在读写的存档挪走。"),
                                     QMessageBox::Warning)
            ->show();
        return;
    }

    const auto answer = QMessageBox::question(
        this, tr("重装地图"),
        tr("会用来源压缩包重新解压世界目录，实例本身（版本、加载器、模组）保持不变。\n\n"
           "现有存档不会删除，会整体移动到 saves/.mcml-backup/<时间戳>/ 下。\n\n继续吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // 大包解压要几十秒；这里同步做，至少给玩家一个等待光标
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = MapImportTask::reinstallWorlds(m_instance->instanceRoot(), m_meta);
    QApplication::restoreOverrideCursor();

    if (!result) {
        CustomMessageBox::selectable(this, tr("重装失败"), result.error(), QMessageBox::Critical)->show();
        return;
    }
    CustomMessageBox::selectable(this, tr("重装完成"),
                                 tr("世界已重新解压。旧存档在 saves/.mcml-backup/ 下。"), QMessageBox::Information)
        ->show();
    reload();
}

void MapInstancePage::applyNotesToUi()
{
    {
        const QSignalBlocker blocker(m_aliasEdit);
        m_aliasEdit->setText(m_notes.alias);
    }
    m_aliasSave->setEnabled(false);
    m_aliasState->setText(m_notes.alias.isEmpty() ? tr("未设置") : tr("已保存"));

    m_stars->setValue(m_notes.rating);
    m_ratingValue->setText(m_notes.rating > 0 ? m_notes.ratingText() : tr("未评分"));

    {
        const QSignalBlocker blocker(m_noteEdit);
        m_noteEdit->setPlainText(m_notes.note);
    }
    m_noteCount->setText(tr("%1 / %2").arg(m_notes.note.size()).arg(MapNotes::MAX_NOTE));
    m_noteSave->setEnabled(false);
    m_noteState->setText(m_notes.updatedAt.isValid()
                             ? tr("上次编辑 · %1")
                                   .arg(m_notes.updatedAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")))
                             : tr("尚未保存"));
}

void MapInstancePage::saveNotes()
{
    if (m_instance == nullptr)
        return;

    QString error;
    if (!m_notes.save(m_instance->instanceRoot(), &error)) {
        CustomMessageBox::selectable(this, tr("保存失败"), error, QMessageBox::Critical)->show();
        return;
    }
    // 重新读一次，拿到写盘时生成的 updatedAt
    m_notes = MapNotes::load(m_instance->instanceRoot());
    applyNotesToUi();
}

}  // namespace maps
