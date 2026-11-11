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
#include <QCheckBox>
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
#include <QLocale>
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
#include "Application.h"
#include "HallAPI.h"
#include "HallCache.h"
#include "MapCoverLoader.h"
#include "MapHallLink.h"
#include "MapImportTask.h"
#include "MapRepository.h"
#include "MapInfoCard.h"
#include "MapL10nApply.h"
#include "MapL10nExport.h"
#include "MapL10nProbe.h"
#include "MapL10nTask.h"
#include "MapL10nTranslate.h"
#include "MapTheme.h"
#include "MapReviewCard.h"
#include "StarRatingWidget.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "minecraft/MinecraftInstance.h"

namespace maps {
namespace {
/// 简介页封面：16:9，和站上的 1920×1080 同比例。
/// 宽度上限受 MapCoverLoader 的落盘宽度（480px）约束，再大就会糊。
constexpr int ABOUT_COVER_W = 400;
constexpr int ABOUT_COVER_H = 225;

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
    mineLayout->addWidget(ui::makeCard(minePage, tr("我的记录"), &notesBody));

    auto* notesGrid = new QGridLayout();
    notesGrid->setHorizontalSpacing(12);
    notesGrid->setVerticalSpacing(8);
    notesGrid->setColumnStretch(1, 1);

    notesGrid->addWidget(ui::makeKeyLabel(tr("中文名"), minePage), 0, 0);
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

    notesGrid->addWidget(ui::makeKeyLabel(tr("我的评分"), minePage), 1, 0, Qt::AlignVCenter);
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

    notesGrid->addWidget(ui::makeKeyLabel(tr("笔记"), minePage), 2, 0, Qt::AlignTop);
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
    mineLayout->addWidget(ui::makeCard(minePage, tr("来源与统计"), &sourceBody));

    auto* sourceGrid = new QGridLayout();
    sourceGrid->setHorizontalSpacing(12);
    sourceGrid->setVerticalSpacing(8);
    sourceGrid->setColumnStretch(1, 1);

    sourceGrid->addWidget(ui::makeKeyLabel(tr("来源压缩包"), minePage), 0, 0, Qt::AlignTop);
    m_archive = new QLabel(minePage);
    m_archive->setWordWrap(true);
    m_archive->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceGrid->addWidget(m_archive, 0, 1);

    sourceGrid->addWidget(ui::makeKeyLabel(tr("游玩统计"), minePage), 1, 0, Qt::AlignTop);
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
    mineLayout->addWidget(ui::makeCard(minePage, tr("版本判定依据"), &evidenceBody));
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
     * 四个页签：简介 / 评论 / 我的 / 汉化。
     *
     * 前两页来自大厅本地库（`hall-cache.json`），后两页是本地信息（上面直接建好的）。
     */
    {
        m_tabs = new QTabWidget(this);
        m_tabs->setDocumentMode(true);
        m_tabs->addTab(buildAboutPage(), tr("简介"));
        m_tabs->addTab(buildReviewsPage(), tr("评论"));
        m_tabs->addTab(minePage, tr("我的"));
        m_tabs->addTab(buildL10nPage(), tr("汉化"));
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
    m_aboutLayout->addWidget(ui::makeCard(m_aboutPage, QString(), &descBody));
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

    // 封面放大到 400×225 之后，窗口拉窄时「封面 + 信息列」会顶出可视区（最低约 760px）。
    // 放进滚动区，窄窗口下是滚动而不是把信息列裁掉。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(m_aboutPage);
    return scroll;
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

// ────────────────────────────── 汉化页 ──────────────────────────────

QString MapInstancePage::worldRoot() const
{
    if (m_instance == nullptr || m_meta.worlds.isEmpty())
        return {};
    return FS::PathCombine(m_instance->instanceRoot(), QStringLiteral(".minecraft"), QStringLiteral("saves"),
                           m_meta.worlds.at(0).folder);
}

QWidget* MapInstancePage::buildL10nPage()
{
    m_l10nPage = new QWidget(this);
    auto* layout = new QVBoxLayout(m_l10nPage);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(10);

    // ── 卡片 1：结论 ──
    QVBoxLayout* modeBody = nullptr;
    layout->addWidget(ui::makeCard(m_l10nPage, tr("这张图能不能汉化"), &modeBody));

    m_l10nMode = new QLabel(tr("还没体检"), m_l10nPage);
    QFont modeFont = m_l10nMode->font();
    modeFont.setBold(true);
    modeFont.setPointSizeF(modeFont.pointSizeF() * 1.15);
    m_l10nMode->setFont(modeFont);
    modeBody->addWidget(m_l10nMode);

    m_l10nDesc = new QLabel(tr("先摸清楚文本藏在哪：资源包的语言表、数据包里的函数、还是直接写在区块数据里。\n"
                              "体检全程只读，不改存档、不联网。"),
                            m_l10nPage);
    m_l10nDesc->setWordWrap(true);
    m_l10nDesc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modeBody->addWidget(m_l10nDesc);

    // ── 卡片 2：载体明细 ──
    QVBoxLayout* carrierBody = nullptr;
    layout->addWidget(ui::makeCard(m_l10nPage, tr("文本在哪"), &carrierBody));

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    const auto addRow = [&](int row, const QString& name) -> QLabel* {
        grid->addWidget(ui::makeKeyLabel(name, m_l10nPage), row, 0, Qt::AlignTop);
        auto* value = new QLabel(m_l10nPage);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setText(tr("—"));
        grid->addWidget(value, row, 1);
        return value;
    };
    m_l10nPack = addRow(0, tr("资源包"));
    m_l10nDatapack = addRow(1, tr("数据包"));
    m_l10nRegion = addRow(2, tr("区块"));
    carrierBody->addLayout(grid);

    // ── 卡片 3：待译与下一步 ──
    QVBoxLayout* actionBody = nullptr;
    layout->addWidget(ui::makeCard(m_l10nPage, tr("待译与下一步"), &actionBody));

    m_l10nTotal = new QLabel(tr("—"), m_l10nPage);
    QFont totalFont = m_l10nTotal->font();
    totalFont.setBold(true);
    m_l10nTotal->setFont(totalFont);
    actionBody->addWidget(m_l10nTotal);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    m_l10nDeep = new QPushButton(tr("扫描全部区块"), m_l10nPage);
    m_l10nDeep->setToolTip(tr("把 region / entities / data 全部读一遍，得到完整结论。\n"
                              "大图要几十秒，可以随时取消。只读，不写任何文件。"));
    m_l10nExport = new QPushButton(tr("导出待译文本…"), m_l10nPage);
    m_l10nExport->setEnabled(false);
    m_l10nExport->setToolTip(tr("导出成 strings.json（给程序）和 strings.csv（给 Excel 改）。\n"
                                "先跑一次体检才有东西可导。"));
    m_l10nTranslate = new QPushButton(tr("自动翻译…"), m_l10nPage);
    m_l10nTranslate->setEnabled(false);
    m_l10nTranslate->setToolTip(tr("用 设置 → 地图 → 汉化翻译 里配的 OpenAI 兼容接口，\n"
                                   "把 strings.json 里还没翻的条目译成中文并写回。"));
    m_l10nApply = new QPushButton(tr("生成汉化版（改存档）…"), m_l10nPage);
    m_l10nApply->setEnabled(false);
    m_l10nApply->setToolTip(tr("把译文写进**存档副本**：复制一份世界，只在副本上改文本。\n"
                               "原存档一个字节都不动；改完会逐区块做结构比对，不一致的区块直接丢弃。\n"
                               "生成完会问要不要用它替换本实例存档 —— 旧图先打包成 zip 备份，随时能还原。"));
    buttonRow->addWidget(m_l10nDeep);
    buttonRow->addWidget(m_l10nExport);
    buttonRow->addWidget(m_l10nTranslate);
    buttonRow->addWidget(m_l10nApply);
    buttonRow->addStretch(1);
    actionBody->addLayout(buttonRow);

    m_l10nState = new QLabel(m_l10nPage);
    m_l10nState->setWordWrap(true);
    m_l10nState->setEnabled(false);
    actionBody->addWidget(m_l10nState);

    connect(m_l10nDeep, &QPushButton::clicked, this, [this] { startL10nProbe(true); });
    connect(m_l10nExport, &QPushButton::clicked, this, &MapInstancePage::exportL10n);
    connect(m_l10nTranslate, &QPushButton::clicked, this, &MapInstancePage::translateL10n);
    connect(m_l10nApply, &QPushButton::clicked, this, &MapInstancePage::applyL10n);

    // ── 已知限制：写在页面上，别让玩家自己撞 ──
    m_l10nNotes = new QLabel(m_l10nPage);
    m_l10nNotes->setWordWrap(true);
    m_l10nNotes->setEnabled(false);
    m_l10nNotes->setText(tr("已知限制：1.12 及更早的命令方块里是裸字符串，暂不处理；\n"
                            "图片化的文字（地图画、位图字体整段渲染）看不见；\n"
                            "中文比英文宽，告示牌和书页可能被裁 —— 工具只能报警，不会自动缩排。"));
    layout->addWidget(m_l10nNotes);

    layout->addStretch(1);
    return m_l10nPage;
}

void MapInstancePage::setL10nBusy(bool busy)
{
    if (m_l10nDeep != nullptr)
        m_l10nDeep->setEnabled(!busy);
    if (m_l10nExport != nullptr)
        m_l10nExport->setEnabled(!busy && m_l10nResult.isValid());
    if (m_l10nTranslate != nullptr)
        m_l10nTranslate->setEnabled(!busy && m_l10nResult.isValid());
    if (m_l10nApply != nullptr)
        m_l10nApply->setEnabled(!busy && m_l10nResult.isValid());
    if (m_l10nState != nullptr) {
        m_l10nState->setEnabled(!busy);
        if (!busy && m_l10nState->text().isEmpty())
            m_l10nState->clear();
    }
}

void MapInstancePage::exportL10n()
{
    if (m_instance == nullptr || !m_l10nResult.isValid())
        return;

    const QString directory = FS::PathCombine(m_instance->instanceRoot(), QStringLiteral("l10n"));
    const auto result = l10n::exportStrings(m_l10nResult, directory);
    if (!result.error.isEmpty()) {
        CustomMessageBox::selectable(this, tr("导出失败"), result.error, QMessageBox::Critical)->show();
        return;
    }

    QString text = tr("已导出 %1 条待译文本：资源包 %2 条、存档 %3 条。")
                       .arg(QLocale().toString(result.total))
                       .arg(QLocale().toString(result.fromPack))
                       .arg(QLocale().toString(result.fromWorld));
    if (!result.regionsScanned)
        text += tr("\n\n注意：区块还没扫过，这份清单只含资源包与数据包，会偏少。");
    text += tr("\n\nstrings.csv 可以直接用 Excel 打开填中文（带 BOM，不会乱码）；\n"
               "strings.json 是给后面的翻译与写回流程用的。");

    QMessageBox box(this);
    box.setWindowTitle(tr("导出完成"));
    box.setIcon(QMessageBox::Information);
    box.setText(text);
    auto* openButton = box.addButton(tr("打开目录"), QMessageBox::ActionRole);
    box.addButton(tr("好"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == openButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MapInstancePage::finishL10nProbe()
{
    setL10nBusy(false);
    if (m_l10nTask != nullptr) {
        m_l10nTask->deleteLater();
        m_l10nTask = nullptr;
    }
}

void MapInstancePage::startL10nProbe(bool deep)
{
    if (m_l10nTask != nullptr)
        return;

    const QString root = worldRoot();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        if (m_l10nState != nullptr)
            m_l10nState->setText(tr("找不到这个实例的存档目录，没法体检。"));
        return;
    }

    auto* task = new l10n::MapL10nTask(root, m_meta.version.mcVersion, deep);
    m_l10nTask = task;
    setL10nBusy(true);
    if (m_l10nState != nullptr)
        m_l10nState->setText(deep ? tr("正在扫描区块…") : tr("正在读取资源包与数据包…"));

    connect(task, &Task::status, this, [this](const QString& text) {
        if (m_l10nState != nullptr)
            m_l10nState->setText(text);
    });
    connect(task, &Task::failed, this, [this](const QString& reason) {
        if (m_l10nState != nullptr)
            m_l10nState->setText(reason);
    });

    if (deep) {
        // 进度对话框自己跑事件循环，结束（完成或取消）后才返回 —— 任务的生命周期在这里收
        ProgressDialog dialog(this);
        dialog.showSkipButton();
        dialog.execWithTask(task);
        applyL10nResult(task->result());
        if (m_l10nState != nullptr && m_l10nTask == task)
            m_l10nState->clear();
        finishL10nProbe();
        return;
    }

    // 快检：静默跑，不弹窗
    connect(task, &Task::succeeded, this, [this, task] {
        applyL10nResult(task->result());
        if (m_l10nState != nullptr)
            m_l10nState->clear();
        finishL10nProbe();
    });
    connect(task, &Task::aborted, this, &MapInstancePage::finishL10nProbe);
    task->start();
}

void MapInstancePage::applyL10nResult(const l10n::ProbeResult& result)
{
    if (m_l10nPage == nullptr)
        return;

    if (!result.error.isEmpty()) {
        m_l10nMode->setText(tr("体检失败"));
        m_l10nMode->setStyleSheet(QStringLiteral("color: %1;").arg(ui::noticeColorName()));
        m_l10nDesc->setText(result.error);
        return;
    }
    if (result.root.isEmpty())
        return;  // 取消得太早，什么都没有，保持原样

    m_l10nFastDone = true;
    m_l10nResult = result;
    if (m_l10nExport != nullptr)
        m_l10nExport->setEnabled(true);
    if (m_l10nTranslate != nullptr)
        m_l10nTranslate->setEnabled(true);
    if (m_l10nApply != nullptr)
        m_l10nApply->setEnabled(true);

    if (result.regionsScanned) {
        const auto mode = result.mode();
        m_l10nMode->setText(l10n::modeLabel(mode));
        m_l10nDesc->setText(l10n::modeDescription(mode));
        // 颜色只区分"能不能只靠资源包"，不吓唬人：改存档也是正路，只是要留副本
        const QColor color = mode == l10n::Mode::Key ? ui::confidenceColor(Confidence::High)
                                                     : ui::confidenceColor(Confidence::Medium);
        m_l10nMode->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    } else {
        m_l10nMode->setText(tr("还没扫区块"));
        m_l10nMode->setStyleSheet(QString());
        m_l10nDesc->setText(tr("资源包和数据包已经读完，区块还没看 —— 只凭这两处下结论会漏掉大半文本。\n"
                               "点下面的「扫描全部区块」拿完整结论。"));
    }

    // 载体明细
    if (!result.pack.found) {
        m_l10nPack->setText(tr("没有自带资源包"));
    } else if (result.pack.enKeys == 0 && result.pack.locales.isEmpty()) {
        m_l10nPack->setText(tr("%1（没读到语言表）").arg(result.pack.path));
    } else {
        const QString ns = result.pack.namespaceName.isEmpty() ? tr("未知") : result.pack.namespaceName;
        m_l10nPack->setText(tr("命名空间 %1 · 英文 %2 键 · 中文 %3 键 · %4 种语言")
                                .arg(ns)
                                .arg(QLocale().toString(result.pack.enKeys))
                                .arg(QLocale().toString(result.pack.zhKeys))
                                .arg(result.pack.locales.size()));
    }

    if (result.datapackFiles == 0) {
        m_l10nDatapack->setText(tr("没有数据包"));
    } else {
        m_l10nDatapack->setText(tr("%1 个文件 · %2 处语言表引用（%3 个键）· %4 处硬编码")
                                    .arg(QLocale().toString(result.datapackFiles))
                                    .arg(QLocale().toString(result.datapacks.refs))
                                    .arg(QLocale().toString(result.datapacks.keys.size()))
                                    .arg(QLocale().toString(result.datapacks.literalOccurrences)));
    }

    if (!result.regionsScanned) {
        m_l10nRegion->setText(tr("还没扫描（%1 个区块文件待读）").arg(QLocale().toString(result.regionFiles)));
    } else {
        m_l10nRegion->setText(tr("%1 个文件 · %2 个区块 · %3 处硬编码（%4 条唯一）")
                                  .arg(QLocale().toString(result.regionFiles))
                                  .arg(QLocale().toString(result.chunks))
                                  .arg(QLocale().toString(result.regions.literalOccurrences))
                                  .arg(QLocale().toString(result.regions.uniqueTexts())));
    }

    // 待译合计
    const int packMissing = result.pack.missingProse;
    const int hardcoded = result.datapacks.uniqueTexts() + result.regions.uniqueTexts();
    if (!result.regionsScanned) {
        m_l10nTotal->setText(tr("待译：资源包 %1 条 · 存档 %2 条（区块未扫描，偏少）")
                                 .arg(QLocale().toString(packMissing))
                                 .arg(QLocale().toString(hardcoded)));
    } else if (packMissing + hardcoded == 0) {
        m_l10nTotal->setText(tr("待译：0 条 —— 这张图的文本可能已经全部是中文，或者都藏在图片里。"));
    } else {
        m_l10nTotal->setText(tr("待译：%1 条（资源包 %2 + 存档 %3）")
                                 .arg(QLocale().toString(packMissing + hardcoded))
                                 .arg(QLocale().toString(packMissing))
                                 .arg(QLocale().toString(hardcoded)));
    }

    // 补充说明：只放真正需要玩家知道的风险，不重复上面的话
    QStringList extras = result.notes;
    if (result.regions.errors > 0)
        extras.append(tr("有 %1 处区块/文本解析失败，统计可能偏少。").arg(QLocale().toString(result.regions.errors)));
    if (m_l10nNotes != nullptr && !extras.isEmpty()) {
        QString text = m_l10nNotes->text();
        const int marker = text.indexOf(QStringLiteral("\n\n"));
        if (marker >= 0)
            text = text.left(marker);
        m_l10nNotes->setText(text + QStringLiteral("\n\n") + extras.join(QLatin1Char('\n')));
    }
}

void MapInstancePage::translateL10n()
{
    if (m_instance == nullptr)
        return;

    const QString directory = FS::PathCombine(m_instance->instanceRoot(), QStringLiteral("l10n"));
    const QString json = QDir(directory).filePath(QStringLiteral("strings.json"));
    if (!QFile::exists(json)) {
        CustomMessageBox::selectable(this, tr("还没有待译清单"),
                                     tr("先点「导出待译文本…」生成 strings.json，再回来自动翻译。"),
                                     QMessageBox::Information)
            ->show();
        return;
    }

    const auto settings = APPLICATION->settings();
    const QString base = settings->get("MapL10nApiBase").toString().trimmed();
    const QString key = settings->get("MapL10nApiKey").toString().trimmed();
    const QString model = settings->get("MapL10nModel").toString().trimmed();
    if (base.isEmpty() || key.isEmpty()) {
        CustomMessageBox::selectable(this, tr("没有配置翻译接口"),
                                     tr("去「设置 → 地图 → 汉化翻译」填接口地址和 API Key。\n\n"
                                        "不想用在线翻译也可以：导出的 strings.csv 用 Excel 填完中文再导入。"),
                                     QMessageBox::Information)
            ->show();
        return;
    }

    auto task = std::make_unique<l10n::MapL10nTranslateTask>(json, base, key,
                                                             model.isEmpty() ? QStringLiteral("gpt-4o-mini") : model);

    // 失败原因先存下来，等进度对话框关掉再提示 —— 免得弹窗叠在弹窗上
    QString failure;
    connect(task.get(), &Task::failed, this, [&failure](const QString& reason) { failure = reason; });

    ProgressDialog dialog(this);
    dialog.showSkipButton();
    dialog.execWithTask(task.get());

    const int translated = task->translated();
    const int failed = task->failed();

    if (!failure.isEmpty()) {
        CustomMessageBox::selectable(this,
                                     tr("自动翻译中断"),
                                     tr("%1\n\n已翻好的 %2 条已经写回 strings.json，不会白跑。")
                                         .arg(failure)
                                         .arg(QLocale().toString(translated)),
                                     QMessageBox::Warning)
            ->show();
        return;
    }
    if (translated == 0) {
        CustomMessageBox::selectable(this, tr("没有翻出东西"),
                                     tr("接口没有返回任何译文。检查模型名与接口地址是否正确。"),
                                     QMessageBox::Warning)
            ->show();
        return;
    }

    QString text = tr("已翻译 %1 条，写回 strings.json。").arg(QLocale().toString(translated));
    if (failed > 0)
        text += tr("\n有 %1 条没翻出来，会保持原文。").arg(QLocale().toString(failed));
    text += tr("\n\n下一步点「生成汉化版（改存档）…」把译文写进存档副本；"
               "生成完可以直接选「替换本实例存档」——旧图会先打包成 zip 备份。");

    QMessageBox box(this);
    box.setWindowTitle(tr("自动翻译完成"));
    box.setIcon(QMessageBox::Information);
    box.setText(text);
    auto* openButton = box.addButton(tr("打开目录"), QMessageBox::ActionRole);
    box.addButton(tr("好"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == openButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MapInstancePage::applyL10n()
{
    if (m_instance == nullptr)
        return;

    const QString directory = FS::PathCombine(m_instance->instanceRoot(), QStringLiteral("l10n"));
    const QString json = QDir(directory).filePath(QStringLiteral("strings.json"));
    if (!QFile::exists(json)) {
        CustomMessageBox::selectable(this, tr("还没有译文"),
                                     tr("先「导出待译文本…」，再把中文填进 zh 列（自动翻译或 CSV 手填）。"),
                                     QMessageBox::Information)
            ->show();
        return;
    }

    int available = 0;
    QString error;
    const auto mapping = l10n::loadTranslations(json, &available, &error);
    if (!error.isEmpty()) {
        CustomMessageBox::selectable(this, tr("读不了译文"), error, QMessageBox::Critical)->show();
        return;
    }
    if (available == 0) {
        CustomMessageBox::selectable(this, tr("一条译文都没有"),
                                     tr("strings.json 里 zh 全是空的。先自动翻译，或用 Excel 填完再导入。"),
                                     QMessageBox::Information)
            ->show();
        return;
    }

    const QString world = worldRoot();
    if (world.isEmpty() || !QFileInfo(world).isDir()) {
        CustomMessageBox::selectable(this, tr("找不到存档"), tr("这个实例没有可用的存档目录。"),
                                     QMessageBox::Critical)
            ->show();
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString outputRoot =
        FS::PathCombine(m_instance->instanceRoot(), QStringLiteral(".mcml-l10n"), stamp, QStringLiteral("MapWorld"));

    QMessageBox box(this);
    box.setWindowTitle(tr("生成汉化版"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("会把这份存档**复制**到：\n%1\n\n然后在副本上替换 %2 条文本。"
                   "\n\n原存档一个字节都不会动；改完会逐区块做结构比对，"
                   "结构对不上的区块会直接丢弃（保留原文），不会写坏。\n\n继续吗？")
                    .arg(QDir::toNativeSeparators(outputRoot))
                    .arg(QLocale().toString(available)));
    box.setTextFormat(Qt::PlainText);
    auto* yesButton = box.addButton(tr("开始"), QMessageBox::AcceptRole);
    auto* noButton = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(noButton);
    box.exec();
    if (box.clickedButton() != yesButton)
        return;

    auto task = std::make_unique<l10n::MapL10nApplyTask>(world, outputRoot, mapping);

    QString failure;
    connect(task.get(), &Task::failed, this, [&failure](const QString& reason) { failure = reason; });

    ProgressDialog dialog(this);
    dialog.showSkipButton();
    dialog.execWithTask(task.get());

    const auto stats = task->stats();
    if (!failure.isEmpty()) {
        CustomMessageBox::selectable(this, tr("生成失败"), failure, QMessageBox::Critical)->show();
        return;
    }

    QString text = tr("已生成汉化版：\n%1\n\n"
                      "替换 %2 处文本，涉及 %3 个区块；结构比对差异 %4 处。")
                       .arg(QDir::toNativeSeparators(outputRoot))
                       .arg(QLocale().toString(stats.replaced))
                       .arg(QLocale().toString(stats.patchedChunks))
                       .arg(QLocale().toString(stats.structureDiffs));
    if (stats.discarded > 0)
        text += tr("\n\n有 %1 个区块被丢弃（保留原文）—— 明细见日志。").arg(QLocale().toString(stats.discarded));
    text += tr("\n\n要不要现在就用它替换本实例的存档？\n"
               "旧存档会先打包成 zip 备份，替换后启动游戏就是汉化版。");

    QMessageBox done(this);
    done.setWindowTitle(tr("汉化版已生成"));
    done.setIcon(QMessageBox::Question);
    done.setTextFormat(Qt::PlainText);
    done.setText(text);
    auto* replaceButton = done.addButton(tr("替换本实例存档"), QMessageBox::AcceptRole);
    auto* openButton = done.addButton(tr("打开目录"), QMessageBox::ActionRole);
    done.addButton(tr("稍后"), QMessageBox::RejectRole);
    done.setDefaultButton(replaceButton);
    done.exec();

    if (done.clickedButton() == openButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputRoot).absolutePath()));
    else if (done.clickedButton() == replaceButton)
        replaceWithL10nCopy(outputRoot);
}

void MapInstancePage::replaceWithL10nCopy(const QString& newWorldRoot)
{
    if (m_instance == nullptr)
        return;

    const QString world = worldRoot();
    if (world.isEmpty() || !QFileInfo(world).isDir()) {
        CustomMessageBox::selectable(this, tr("找不到存档"), tr("这个实例没有可用的存档目录。"),
                                     QMessageBox::Critical)
            ->show();
        return;
    }

    // 游戏开着的时候替换存档目录：Windows 上会挪不动（session.lock / region 文件被占用），
    // 硬来时还可能把正在保存的世界写坏。直接拒绝更省事。
    if (m_instance->isRunning()) {
        CustomMessageBox::selectable(this, tr("游戏正在运行"),
                                     tr("先关掉游戏再替换存档。现在替换会和游戏争抢存档文件。"),
                                     QMessageBox::Warning)
            ->show();
        return;
    }

    const QString backupDir = FS::PathCombine(m_instance->instanceRoot(), QStringLiteral("l10n"),
                                              QStringLiteral("backups"));
    const QString worldFolder = QFileInfo(world).fileName();

    QMessageBox box(this);
    box.setWindowTitle(tr("替换存档"));
    box.setIcon(QMessageBox::Question);
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("会用刚生成的汉化版顶替这个实例的存档：\n%1\n\n"
                   "旧存档先打包成 zip 备份，放在：\n%2\n\n"
                   "替换后启动游戏就是汉化版；想回到原图，把备份解压回 saves 目录即可"
                   "（也可以直接用「重装地图」从地图库还原）。\n\n继续吗？")
                    .arg(QDir::toNativeSeparators(world), QDir::toNativeSeparators(backupDir)));
    auto* yesButton = box.addButton(tr("替换"), QMessageBox::AcceptRole);
    auto* noButton = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(noButton);
    box.exec();
    if (box.clickedButton() != yesButton)
        return;

    auto task = std::make_unique<l10n::MapL10nReplaceTask>(world, newWorldRoot, backupDir);

    QString failure;
    connect(task.get(), &Task::failed, this, [&failure](const QString& reason) { failure = reason; });

    ProgressDialog dialog(this);
    dialog.showSkipButton();
    dialog.execWithTask(task.get());

    if (!failure.isEmpty()) {
        CustomMessageBox::selectable(this, tr("替换失败"), failure, QMessageBox::Critical)->show();
        return;
    }

    const auto replaceStats = task->stats();
    QString text = tr("已替换成汉化版。\n\n"
                      "旧存档备份：\n%1\n（%2，共 %3 个文件）\n\n"
                      "世界目录：%4\n\n"
                      "现在启动游戏就是汉化版；没翻到的条目仍然是原文。")
                       .arg(QDir::toNativeSeparators(replaceStats.backupPath))
                       .arg(QLocale().formattedDataSize(replaceStats.backupBytes))
                       .arg(QLocale().toString(replaceStats.files))
                       .arg(replaceStats.worldFolder);

    QMessageBox finished(this);
    finished.setWindowTitle(tr("替换完成"));
    finished.setIcon(QMessageBox::Information);
    finished.setText(text);
    auto* revealButton = finished.addButton(tr("打开备份目录"), QMessageBox::ActionRole);
    finished.addButton(tr("好"), QMessageBox::AcceptRole);
    finished.exec();

    if (finished.clickedButton() == revealButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(replaceStats.backupPath).absolutePath()));

    // 存档换过了，之前那份体检结果已经过期（导出/翻译/生成都基于旧存档）
    m_l10nResult = l10n::ProbeResult{};
    m_l10nFastDone = false;
    setL10nBusy(false);
    if (m_l10nState != nullptr)
        m_l10nState->setText(tr("已替换成汉化版，旧存档备份在 %1。重新体检可以看到新的待译条数。")
                                 .arg(QDir::toNativeSeparators(replaceStats.backupPath)));
}

void MapInstancePage::reload()
{
    if (m_instance == nullptr)
        return;

    m_meta = MapMeta::load(m_instance->instanceRoot());
    m_notes = MapNotes::load(m_instance->instanceRoot());
    applyNotesToUi();

    // 「汉化」页：第一次打开实例时静默跑一次快检（只读资源包与数据包，毫秒级）。
    // reload() 会被多次调用（存笔记、切页签），所以只跑一次；区块扫描永远要玩家自己点。
    if (!m_l10nFastDone && m_l10nTask == nullptr && m_l10nPage != nullptr)
        startL10nProbe(false);

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

    QMessageBox box(this);
    box.setWindowTitle(tr("重装地图"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("会用来源压缩包重新解压世界目录，实例本身（版本、加载器、模组）保持不变。\n\n继续吗？"));

    auto* backupCheck = new QCheckBox(tr("备份现有存档到 saves/.mcml-backup/ 目录下"), &box);
    backupCheck->setChecked(true);
    box.setCheckBox(backupCheck);

    auto* yesButton = box.addButton(tr("是"), QMessageBox::YesRole);
    auto* noButton = box.addButton(tr("否"), QMessageBox::NoRole);
    box.setDefaultButton(noButton);

    box.exec();
    if (box.clickedButton() != yesButton)
        return;

    const bool doBackup = backupCheck->isChecked();

    auto task = std::make_unique<MapReinstallTask>(m_instance->instanceRoot(), m_meta, doBackup);
    connect(task.get(), &Task::failed, this, [this](const QString& reason) {
        CustomMessageBox::selectable(this, tr("重装失败"), reason, QMessageBox::Critical)->show();
    });
    connect(task.get(), &Task::succeeded, this, [this, doBackup]() {
        const QString successMsg = doBackup
            ? tr("世界已重新解压。旧存档在 saves/.mcml-backup/ 下。")
            : tr("世界已重新解压。");
        CustomMessageBox::selectable(this, tr("重装完成"), successMsg, QMessageBox::Information)
            ->show();
        reload();
    });

    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(task.get());
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
