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
#include "MapSettingsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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
#include "MapLibrary.h"
#include "MapMeta.h"
#include "MapImportTask.h"
#include "MapRepository.h"
#include "MapInfoCard.h"
#include "MapStyle.h"
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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /**
     * 页头：标题 + 一行徽章，下面一条细线把页头和页签分开。
     *
     * 以前标题下面是一句 "1.16.5 · 置信度 HIGH" 的长文本，扫起来要在标点里找字段；
     * 改成彩色小徽章之后，版本 / 加载器 / 置信度各自成一个视觉块，
     * 与右侧地图卡片的写法一致。
     */
    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(ui::kPageMargin, ui::kPageMargin, ui::kPageMargin, ui::kSpaceS);
    headerLayout->setSpacing(ui::kSpaceS);

    m_title = ui::makePageTitle(QString(), header);
    headerLayout->addWidget(m_title);

    m_badgeBox = new QWidget(header);
    m_badgeRow = new QHBoxLayout(m_badgeBox);
    m_badgeRow->setContentsMargins(0, 0, 0, 0);
    m_badgeRow->setSpacing(ui::kSpaceXS);
    m_badgeRow->addStretch(1);
    headerLayout->addWidget(m_badgeBox);

    layout->addWidget(header);
    layout->addWidget(ui::makeSeparator(this));

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
     * 会丢掉拉伸约束，行距被撑开半屏（见 06 §48）。
     */
    auto* minePage = new QWidget(this);
    auto* mineLayout = new QVBoxLayout(minePage);
    mineLayout->setContentsMargins(ui::pageMargins());
    mineLayout->setSpacing(ui::kSpaceM);

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
    buttons->setSpacing(ui::kSpaceS);
    auto* openInstance = new QPushButton(tr("打开实例目录"), this);
    auto* openArchive = new QPushButton(tr("打开压缩包所在目录"), this);
    auto* reinstall = new QPushButton(tr("重装地图…"), this);
    ui::applySecondaryButton(openInstance);
    ui::applySecondaryButton(openArchive);
    ui::applySecondaryButton(reinstall);
    buttons->addWidget(openInstance);
    buttons->addWidget(openArchive);
    buttons->addWidget(reinstall);
    buttons->addStretch(1);
    mineLayout->addSpacing(ui::kSpaceXS);
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
     * 四个页签：简介 / 评论 / 我的 / 启动。
     *
     * 前两页来自大厅本地库（`hall-cache.json`），后两页是本地信息（上面直接建好的）。
     * 「启动」页装的是实例级的地图启动设置 —— 原来在「设置 → 地图」页签里，
     * 2026-11-12 挪过来：打开实例先看到这张图，顺手就能改它怎么进游戏。
     *
     * 评论页按**页面指针**取，不写死索引（非地图实例没有「启动」页）。
     */
    {
        m_tabs = new QTabWidget(this);
        m_tabs->setDocumentMode(true);
        m_tabs->addTab(buildAboutPage(), tr("简介"));
        m_tabs->addTab(buildReviewsPage(), tr("评论"));
        m_tabs->addTab(minePage, tr("我的"));

        // 地图实例才有启动设置；非地图实例这一页本来就不显示（shouldDisplay）
        if (shouldDisplay()) {
            m_launchSettings = new MapSettingsWidget(m_instance, this);
            m_tabs->addTab(buildLaunchPage(), tr("启动"));
            // 全局设置改过之后，没被实例覆盖的项要跟着变（原来这条连接在 InstanceSettingsPage）
            connect(APPLICATION, &Application::globalSettingsApplied, m_launchSettings, &MapSettingsWidget::loadSettings);
        }

        layout->addWidget(m_tabs, 1);

        // 切到「评论」页才去拉（见 refreshHallTabs 里的说明）
        const int reviewsIndex = m_tabs->indexOf(m_reviewsPage);
        connect(m_tabs, &QTabWidget::currentChanged, this, [this, reviewsIndex](int index) {
            if (index != reviewsIndex || m_reviewsRequested || m_hallUuid.isEmpty() || m_hall == nullptr)
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
    return tr("地图");
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
    // 「启动」页的值可能被别的路径改过（实例设置页、全局设置），每次进页面重新读一次
    if (m_launchSettings != nullptr)
        m_launchSettings->loadSettings();
}

bool MapInstancePage::apply()
{
    // 原来这一步在 InstanceSettingsPage::apply 里（「设置 → 地图」页签）；
    // 页签搬到这边之后，保存责任也跟着过来 —— 否则改完关窗口不会落盘。
    if (m_launchSettings != nullptr)
        m_launchSettings->saveSettings();
    return true;
}

QWidget* MapInstancePage::buildLaunchPage()
{
    // MapSettingsWidget 自己带滚动区与内边距，这里只做一层容器，别再套边距
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_launchSettings, 1);
    return page;
}

QWidget* MapInstancePage::buildAboutPage()
{
    m_aboutPage = new QWidget(this);
    m_aboutLayout = new QVBoxLayout(m_aboutPage);
    m_aboutLayout->setContentsMargins(ui::pageMargins());
    m_aboutLayout->setSpacing(ui::kSpaceM);

    /**
     * 顶部一条「封面在左、基本信息在右」：
     * 站上简介页的封面是 16:9 大图，放左边当视觉锚点；作者/版本/难度/长度这些
     * 短字段放右侧，竖排的键值列比横着铺开更好扫（2026-11-02 用户要求）。
     *
     * 信息列改成 QGridLayout 之后：字段名次要色、值正常色、值可换行，
     * 长作者名单不会再把整行挤成一句话。
     */
    auto* hero = new QHBoxLayout();
    hero->setContentsMargins(0, 0, 0, 0);
    hero->setSpacing(ui::kSpaceL);
    // 整组（封面 + 信息）在页面里水平居中
    hero->addStretch(1);

    m_aboutCover = new QLabel(m_aboutPage);
    m_aboutCover->setFixedSize(ABOUT_COVER_W, ABOUT_COVER_H);
    m_aboutCover->setAlignment(Qt::AlignCenter);
    m_aboutCover->setStyleSheet(QStringLiteral("border: 1px solid rgba(127, 127, 127, 48); border-radius: 6px;"));
    m_aboutCover->setPixmap(MapCoverLoader::placeholderCover(QStringLiteral("?"), ABOUT_COVER_W, ABOUT_COVER_H));
    hero->addWidget(m_aboutCover, 0, Qt::AlignTop);

    auto* infoColumn = new QWidget(m_aboutPage);
    auto* infoLayout = new QVBoxLayout(infoColumn);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(ui::kSpaceM);

    m_aboutInfo = new QWidget(infoColumn);
    m_aboutInfoLayout = new QVBoxLayout(m_aboutInfo);
    m_aboutInfoLayout->setContentsMargins(0, 0, 0, 0);
    m_aboutInfoLayout->setSpacing(ui::kSpaceS);
    // 给信息列一个稳定的宽度区间：太窄字段会折行，太宽整组就不像"一块"了
    m_aboutInfo->setMinimumWidth(320);
    m_aboutInfo->setMaximumWidth(560);
    infoLayout->addWidget(m_aboutInfo);

    auto* linkRow = new QHBoxLayout();
    linkRow->setSpacing(ui::kSpaceS);
    m_aboutLinkButton = new QPushButton(tr("关联到 CTM 地图…"), m_aboutPage);
    ui::applySecondaryButton(m_aboutLinkButton);
    connect(m_aboutLinkButton, &QPushButton::clicked, this, &MapInstancePage::pickHallMap);
    m_aboutUnlinkButton = new QPushButton(tr("解除关联"), m_aboutPage);
    ui::applySecondaryButton(m_aboutUnlinkButton);
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
    infoLayout->addLayout(linkRow);
    infoLayout->addStretch(1);

    hero->addWidget(infoColumn, 0);
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

void MapInstancePage::setAboutFacts(const QList<QPair<QString, QString>>& facts)
{
    if (m_aboutInfoLayout == nullptr)
        return;

    ui::clearLayout(m_aboutInfoLayout);
    if (facts.isEmpty())
        return;

    // 两列：字段名次要色、值正常色并允许换行。比原来"<b>键</b>：值<br/>"堆成一段好扫得多，
    // 长作者名单也不会把后面的字段挤到看不见。
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(ui::kSpaceM + 2);
    grid->setVerticalSpacing(ui::kSpaceS);
    grid->setColumnStretch(1, 1);

    int row = 0;
    for (const auto& fact : facts) {
        grid->addWidget(ui::makeKeyLabel(fact.first, m_aboutInfo), row, 0, Qt::AlignTop);
        auto* value = new QLabel(fact.second, m_aboutInfo);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(value, row, 1);
        ++row;
    }
    m_aboutInfoLayout->addLayout(grid);
}

QWidget* MapInstancePage::buildReviewsPage()
{
    m_reviewsPage = new QWidget(this);
    auto* outer = new QVBoxLayout(m_reviewsPage);
    outer->setContentsMargins(ui::pageMargins());
    outer->setSpacing(ui::kSpaceS);

    m_reviewsState = new QLabel(m_reviewsPage);
    m_reviewsState->setWordWrap(true);
    m_reviewsState->setFont(ui::metaFont(m_reviewsState->font()));
    m_reviewsState->setStyleSheet(QStringLiteral("color: %1;").arg(ui::mutedTextColor().name()));
    outer->addWidget(m_reviewsState);

    auto* scroll = new QScrollArea(m_reviewsPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    m_reviewsLayout = new QVBoxLayout(body);
    m_reviewsLayout->setContentsMargins(0, 0, ui::kSpaceS, 0);
    m_reviewsLayout->setSpacing(ui::kSpaceM);
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
        QList<QPair<QString, QString>> facts;
        const auto& item = detail.item;
        const auto add = [&facts](const QString& key, const QString& value) {
            if (!value.isEmpty())
                facts.append({ key, value });
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

        setAboutFacts(facts);
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

    // 库里对应的那个包跟着转成"已确认"：这条关联是玩家自己点的，不是匹配器猜的
    const auto meta = MapMeta::load(m_instance->instanceRoot());
    const QString libraryFile = QFileInfo(meta.archive.libraryCopy).fileName();
    if (!libraryFile.isEmpty())
        MapLibrary::markHallVerified(libraryFile, item.slug);

    refreshHallTabs();
}

void MapInstancePage::pickHallMap()
{
    ensureHallApi();
    const auto items = m_hall->libraryItems();
    if (items.isEmpty()) {
        CustomMessageBox::selectable(
            this, tr("还没有大厅数据"),
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
        QString text = tr("还没关联到 CTM 地图 —— 关联后这里会显示站上的简介、评分和玩家评论。\n"
                          "不会改存档，只写一个 hall-link.json。");
        if (!suggestion.isEmpty())
            text += tr("\n本地库里有一张《%1》看着就是它，点下面的按钮即可关联。").arg(suggestion);
        m_aboutState->setText(text);
        setAboutFacts({});
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
            setAboutFacts({});
        requestAboutCover({}, m_link.title);
        m_reviewsState->setText(tr("还没有大厅数据，暂时没有评论可显示。"));
        return;
    }

    m_aboutState->setText(tr("正在读取《%1》的简介…").arg(m_link.title));
    setAboutFacts({});
    requestAboutCover({}, m_link.title);
    m_hall->detail(m_link.slug);

    /**
     * 评论按需拉：本地库只装了"最近 1000 条"，某张图往往落在窗口外，
     * 这时候要发一次网络请求（几秒）。所以等用户真的点开「评论」页再去拉，
     * 打开页面本身不会卡住。
     */
    m_reviewsRequested = false;
    m_reviewsState->setText(tr("点开「评论」页时再读取。"));
    // 按页面指针判断当前是不是「评论」页 —— 页签数量会随功能开关变，别写死索引
    if (m_tabs != nullptr && m_tabs->currentWidget() == m_reviewsPage)
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

    // 版本 / 加载器 / 置信度：三个小徽章，色相分别取自强调色、次要色与置信度色。
    // 清空时连 stretch 一起删，重建后补回来（否则第二次刷新会越堆越多）。
    if (m_badgeRow != nullptr) {
        while (QLayoutItem* item = m_badgeRow->takeAt(0)) {
            if (auto* widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        int chipCount = 0;
        const auto addChip = [this, &chipCount](const QString& text, const QColor& color) {
            if (text.isEmpty())
                return;
            auto* chip = new QLabel(text, m_badgeBox);
            chip->setStyleSheet(ui::chipStyle(color));
            chip->setToolTip(text);
            m_badgeRow->addWidget(chip);
            ++chipCount;
        };
        addChip(m_meta.version.mcVersion, ui::accentColor());
        if (!m_meta.loader.type.isEmpty() && m_meta.loader.type != QLatin1String("vanilla")) {
            addChip(m_meta.loader.version.isEmpty()
                        ? m_meta.loader.type
                        : QStringLiteral("%1 %2").arg(m_meta.loader.type, m_meta.loader.version),
                    ui::mutedTextColor());
        }
        // 版本都判不出来时不摆一个 "置信度 LOW" 的空徽章：非地图实例这一页也会构造
        if (!m_meta.version.mcVersion.isEmpty())
            addChip(tr("置信度 %1").arg(confidenceLabel(m_meta.version.confidence)),
                    ui::confidenceColor(m_meta.version.confidence));
        m_badgeBox->setVisible(chipCount > 0);
        m_badgeRow->addStretch(1);
    }

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

    // 同一张图可能有多个语言版本（原版 / 简中 / 繁中）。重装时让玩家选，
    // 这样"换成英文原版"与"换成汉化版"都是同一套流程，不用删实例重导。
    MapLibrary library;
    library.reload();
    const QString currentArchive = MapRepository::resolveArchivePath(m_meta);
    const QString currentFile = QFileInfo(currentArchive).fileName();

    QString groupKey;
    for (const auto& item : library.items()) {
        if (item.file == currentFile) {
            groupKey = item.groupKey();
            break;
        }
    }
    const auto variants = groupKey.isEmpty() ? QList<LibraryItem>{} : library.groupItems(groupKey);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("重装地图"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QLabel(tr("会用选中的压缩包重新解压世界目录；实例本身（版本、加载器、模组）保持不变。"), &dialog);
    text->setWordWrap(true);
    layout->addWidget(text);

    auto* form = new QFormLayout();
    auto* archiveCombo = new QComboBox(&dialog);
    for (const auto& item : variants) {
        archiveCombo->addItem(tr("%1 · MC %2 · %3").arg(item.variantSummary(), item.mcVersion, item.file), item.path);
        if (item.file == currentFile)
            archiveCombo->setCurrentIndex(archiveCombo->count() - 1);
    }
    if (archiveCombo->count() == 0)
        archiveCombo->addItem(tr("（按 map.json 记录的来源）"), QString());
    form->addRow(tr("来源版本"), archiveCombo);
    layout->addLayout(form);

    auto* backupCheck = new QCheckBox(tr("备份现有存档到 saves/.mcml-backup/ 目录下"), &dialog);
    backupCheck->setChecked(true);
    layout->addWidget(backupCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("重装"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool doBackup = backupCheck->isChecked();
    const QString chosenPath = archiveCombo->currentData().toString();

    // 选中的那条的语言身份；换版本时一并写回 map.json
    variant::Info chosenVariant;
    for (const auto& item : variants) {
        if (item.path == chosenPath) {
            chosenVariant = item.variant;
            break;
        }
    }
    chosenVariant.isDefault = m_meta.variant.isDefault;

    auto task = std::make_unique<MapReinstallTask>(m_instance->instanceRoot(), m_meta, doBackup, chosenPath,
                                                   chosenVariant);
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
