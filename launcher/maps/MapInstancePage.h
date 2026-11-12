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
#pragma once

#include <QWidget>

#include <memory>

#include "MapL10nProbe.h"
#include "MapMeta.h"
#include "MapHallLink.h"
#include "MapNotes.h"
#include "ui/pages/BasePage.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QPushButton;
class MinecraftInstance;

namespace maps {

class HallAPI;
class MapSettingsWidget;
struct HallItem;

namespace l10n {
class MapL10nTask;
}  // namespace l10n

/// 五星评分控件（半星粒度，内部 1~10），定义在 MapInstancePage.cpp
class StarRatingWidget;

/**
 * 实例窗口里的「地图」页（原「信息」页）：map.json 部分**只读**，「我的记录」可编辑。
 *
 * 四个页签：简介 / 评论 / 我的 / 启动。
 *   · 简介、评论来自大厅本地库；
 *   · 我的 = notes.json（玩家自己的数据）；
 *   · 启动 = 实例级的地图启动设置（自动进图 / 退出即关 / 跳过实验性确认 / 进图延迟），
 *     原来是「设置 → 地图」那个页签，2026-11-12 挪到这里 —— 打开实例窗口先看到图，
 *     顺手就能改它怎么进游戏，不用先切到设置再找页签。
 *
 * 为什么叫「地图」不叫「信息」：这一页就是这张地图本身，名字跟「设置」「日志」
 * 这些工具页并列时，"信息"太泛。
 *
 * 不列"世界目录"：一个地图对应一个实例，实例里只有它自己的 saves/<世界>，
 * 把「实例内目录 ↔ 压缩包内路径」这张表摆出来只会让人以为要手动搬目录。
 *
 * 为什么要分开：
 *   · map.json 是导入时判定的留痕 —— 在这里改了就无法复现"当时凭什么判成这个版本"。
 *     要改版本请走「重装地图」；要删就删实例。
 *   · 中文名 / 我的评分 / 笔记是**玩家自己的数据**（notes.json），照 AIO 的分工，
 *     任何自动流程（重装、重导入）都不该覆盖它们，所以给编辑入口。
 */
class MapInstancePage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit MapInstancePage(MinecraftInstance* instance, QWidget* parent = nullptr);

    QString id() const override { return QStringLiteral("map"); }
    QString displayName() const override;
    QIcon icon() const override;
    /// 只有带 map.json 的实例才显示这一页
    bool shouldDisplay() const override;

    void openedImpl() override;
    /// 「启动」页签的设置写回实例（PageContainer 在关窗口时对所有页调用一次）
    bool apply() override;

   private:
    void reload();
    void reinstallWorlds();

    // ── 「简介 / 评论」两个页签：读本地大厅库，没关联就说明原因 ──
    QWidget* buildAboutPage();
    QWidget* buildReviewsPage();
    /// 「启动」页签：实例级地图启动设置（原「设置 → 地图」）
    QWidget* buildLaunchPage();
    void refreshHallTabs();
    /// 简介页封面：先画占位、再异步换真图（拿不到就一直是占位，不留空白）
    void requestAboutCover(const QString& url, const QString& title);
    void applyAboutCover(const QImage& image);
    void pickHallMap();
    void linkTo(const HallItem& item);
    void ensureHallApi();

    /// 把 notes.json 的当前值刷到界面（中文名 / 星级 / 笔记 / 状态文字）
    void applyNotesToUi();
    /// 写 notes.json 并刷新状态文字
    void saveNotes();

    // ── 「汉化」页 ──
    /// 主世界目录（`saves/<folder>`）；拿不到返回空串
    QString worldRoot() const;
    QWidget* buildL10nPage();
    /// deep = true 才扫区块（几十秒）；false 只读资源包与数据包
    void startL10nProbe(bool deep);
    void applyL10nResult(const l10n::ProbeResult& result);
    void finishL10nProbe();
    /// 体检进行中的按钮/状态互斥
    void setL10nBusy(bool busy);
    /// 把待译清单写到 `<实例>/l10n/`
    void exportL10n();
    /// 用 OpenAI 兼容接口把 strings.json 里的待译条目翻成中文（就地写回）
    void translateL10n();
    /// 把译文写进**存档副本**（C 档：注入式汉化），源存档只读；
    /// 文本都在资源包语言表里时自动改走 `applyL10nToPack`
    void applyL10n();
    /// A 档：只往世界自带资源包写 `zh_cn.json`，存档数据一个字节不动
    void applyL10nToPack(const QHash<QString, QString>& keyTranslations);
    /// 用汉化版副本顶替本实例存档：旧存档先打包成 zip 备份（不删实例，随时可还原）
    void replaceWithL10nCopy(const QString& newWorldRoot);
    /// 备份目录（`<实例>/l10n/backups`）
    QString l10nBackupDir() const;
    /// 把「有几份备份」刷到界面上
    void refreshL10nBackups();
    /// 从备份还原原图（当前存档先挪开，成功后才删）
    void restoreL10nBackup();

    QWidget* m_l10nPage = nullptr;
    QLabel* m_l10nMode = nullptr;
    QLabel* m_l10nDesc = nullptr;
    QLabel* m_l10nTotal = nullptr;
    QLabel* m_l10nPack = nullptr;
    QLabel* m_l10nDatapack = nullptr;
    QLabel* m_l10nRegion = nullptr;
    QLabel* m_l10nState = nullptr;
    QLabel* m_l10nNotes = nullptr;
    QLabel* m_l10nBackupState = nullptr;
    QPushButton* m_l10nDeep = nullptr;
    QPushButton* m_l10nExport = nullptr;
    QPushButton* m_l10nTranslate = nullptr;
    QPushButton* m_l10nApply = nullptr;
    QPushButton* m_l10nRestore = nullptr;
    QPushButton* m_l10nOpenBackups = nullptr;
    l10n::MapL10nTask* m_l10nTask = nullptr;
    /// 最近一次体检结果：导出待译清单要用（切页签不会丢）
    l10n::ProbeResult m_l10nResult;
    /// 快检只跑一次（reload 会被调用多次）
    bool m_l10nFastDone = false;

    MinecraftInstance* m_instance = nullptr;
    MapMeta m_meta;
    MapNotes m_notes;
    /// 非空 = 这个实例是地图实例，「启动」页签才存在
    MapSettingsWidget* m_launchSettings = nullptr;

    QLineEdit* m_aliasEdit = nullptr;
    QPushButton* m_aliasSave = nullptr;
    QLabel* m_aliasState = nullptr;
    StarRatingWidget* m_stars = nullptr;
    QLabel* m_ratingValue = nullptr;
    QPlainTextEdit* m_noteEdit = nullptr;
    QPushButton* m_noteSave = nullptr;
    QLabel* m_noteCount = nullptr;
    QLabel* m_noteState = nullptr;

    QLabel* m_title = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_archive = nullptr;
    QLabel* m_stats = nullptr;
    QLabel* m_evidence = nullptr;
    /// 实验性设置 / 缺加载器 这类「不做就不能正常玩」的提示
    QLabel* m_notice = nullptr;

    // ── 大厅联动 ──
    QTabWidget* m_tabs = nullptr;
    /// 「简介」页
    QWidget* m_aboutPage = nullptr;
    QVBoxLayout* m_aboutLayout = nullptr;
    /// 简介页顶部：封面在左、基本信息在右（2026-11-02 按用户要求排版）
    QLabel* m_aboutCover = nullptr;
    QLabel* m_aboutInfo = nullptr;
    /// 当前封面 URL：异步回图时用它判断是不是这张图，避免快速切图串味
    QString m_aboutCoverUrl;
    QLabel* m_aboutState = nullptr;
    QPushButton* m_aboutLinkButton = nullptr;
    QPushButton* m_aboutUnlinkButton = nullptr;
    /// 评论是"点开评论页才拉"（按图请求要几秒，别在打开页面时就卡住）
    bool m_reviewsRequested = false;
    /// 「评论」页
    QWidget* m_reviewsPage = nullptr;
    QVBoxLayout* m_reviewsLayout = nullptr;
    QPushButton* m_reviewsMore = nullptr;
    QLabel* m_reviewsState = nullptr;
    HallAPI* m_hall = nullptr;
    MapHallLink m_link;
    /// 当前「简介/评论」属于哪张图（异步回来时防止串数据）
    QString m_hallSlug;
    QString m_hallUuid;
    int m_reviewsShown = 0;
};

}  // namespace maps
