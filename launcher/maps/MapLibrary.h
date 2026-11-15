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

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "MapVariant.h"

namespace maps {

/// 地图库里的分类。id 落盘（`map-library.json`），纯英文显示名。
enum class MapCategory { Rpg, Aad, Ctm, Minigame, Other };

QString categoryId(MapCategory category);
MapCategory categoryFromId(const QString& id);
/// 「RPG」「AAD」等纯英文显示名
QString categoryLabel(MapCategory category);
QList<MapCategory> allCategories();

/// 分类信息（支持用户在 map-library.json 中自定义增删改）
struct CategoryInfo {
    QString id;    ///< 分类唯一标识（小写），例如 "rpg", "ctm", "parkour"
    QString name;  ///< 显示名称，例如 "RPG", "CTM", "Parkour"
};

/// 库里的一个压缩包。字段来自 `map-library.json`，缺失就是空串 —— 不猜。
struct LibraryItem {
    QString file;          ///< 库目录内的文件名，也是索引键
    QString path;          ///< 绝对路径（装载时补上，不落盘）
    QString category;      ///< 分类 ID（小写）
    QString mcVersion;     ///< 例如 "1.8.9"、"1.7.x"（区间用 x 表示，不细分）
    QString englishName;
    QString mapVersion;    ///< 地图自己的版本号，例如 v1.1.15
    QString chineseName;
    QString note;
    qint64 bytes = 0;
    bool installed = false;         ///< 是否有实例仍指向它
    QStringList installedInstances;  ///< 实例 id，给 tooltip 用

    /// 关联的 CTM 地图（大厅 slug）。空 = 还没关联
    QString hallSlug;
    /// 关联是不是人工确认过的。自动匹配也会写库，但只写 false（[06 §72]）；
    /// 实例页里玩家点过一次「关联」才转 true。
    bool hallVerified = false;
    /// 语言变体：原版 / 官中 / 汉化，整包还是资源包
    variant::Info variant;

    /// 显示名：中文名 → 英文名 → 文件名
    QString displayName() const;
    /// 徽章文本：`[1.16.5] Final Paradox v1.1.15`
    QString badge() const;
    /// 分组键：关联了大厅就用 `hall:<slug>`，否则退回 `local:<归一化图名>`。
    /// **不落盘**：这样改名不会把同一张图拆成两组，换机器只要有 slug 就还在同一组。
    QString groupKey() const;
    /// 「简中汉化 · 整包」「原版」这类一行摘要（界面用）
    QString variantSummary() const;
};

/**
 * 本地地图库：`<数据目录>/maps`（设置项 `MapLibraryDir` 可改）里的压缩包 + 分类索引。
 *
 * 为什么还要一份索引：分类、中文名这些是"玩家的组织方式"，压缩包里没有。
 * 索引按**文件名**存（不是路径），所以整体挪库、改库目录都不会让分类失效；
 * 文件被手动改名则退化成"新文件 + 一条孤儿索引"，不会张冠李戴。
 *
 * 方案 B：允许自定义分类。map-library.json 中存储 `categories` 列表；
 * 默认五项为 RPG, CTM, AAD, MiniGame, Other；支持用户增删改名。
 *
 * 「已安装」是算出来的，不落盘：每次 reload 时拿实例的 map.json 去解析
 * （MapRepository::resolveArchivePath），文件名对上就算。这样重装/删除实例后永远是对的。
 */
class MapLibrary : public QObject {
    Q_OBJECT

   public:
    explicit MapLibrary(QObject* parent = nullptr);

    /// 库目录（与 MapRepository 同一口径）
    static QString libraryDir();
    static QString indexPath();
    /// 库目录里的压缩包后缀
    static QStringList archiveSuffixes();

    /// 默认分类列表（5 项纯英文）
    static QList<CategoryInfo> defaultCategories();

    /// 重新读索引 + 扫目录 + 计算「已安装」。目录里新出现的文件会以 other 分类补进来。
    void reload();

    QList<LibraryItem> items() const { return m_items; }
    QList<LibraryItem> items(const QString& categoryId) const;
    QList<LibraryItem> items(MapCategory category) const;
    int count(const QString& categoryId) const;
    int count(MapCategory category) const;

    /// 当前可用的分类列表（包含默认与自定义分类）
    QList<CategoryInfo> categories() const { return m_categories; }
    /// 获取指定分类 ID 的显示名
    QString categoryName(const QString& id) const;

    /// 新建分类（返回生成的 id，重名或非法返回空串）
    QString addCategory(const QString& name);
    /// 重命名分类（成功返回 true）
    bool renameCategory(const QString& id, const QString& newName);
    /// 删除分类（"other" 保护不可删；删除后属于该分类的项目移至 "other"）
    bool deleteCategory(const QString& id);

    /// 改分类并立即落盘；失败返回 false（写不了索引时界面会提示）
    bool setCategory(const QString& file, const QString& categoryId);
    bool setCategory(const QString& file, MapCategory category);
    /// 整组改分类（组标题右键用）：一次写盘，不是 N 次
    bool setCategoryForItems(const QStringList& files, const QString& categoryId);
    /// 改「地图自己的版本号」（`mapVersion`）。界面默认按 1.0 显示，但**不写进索引**：
    /// 只有玩家真填了才落盘。传空串 = 清掉，回到默认显示。
    bool setMapVersion(const QString& file, const QString& version);

    /// 关联/解除关联 CTM 地图（slug 为空 = 解除）。verified 由人工确认时置 true。
    /// 默认 **false**：自动化路径一律不替玩家宣布"已确认"，只有人在界面上点过才传 true。
    bool setHallLink(const QString& file, const QString& hallSlug, bool verified = false);
    /**
     * 把某条目的关联标成"人工确认"（实例页点过「关联到 CTM 地图」后调用）。
     *
     * 静态方法、只改一个字段、不 reload：实例页手里只有实例，没有库页面对象；
     * 为了标记一个 bool 去扫一遍库目录不划算。条目不在索引里就返回 false（不算错）。
     */
    static bool markHallVerified(const QString& file, const QString& hallSlug);
    /// 改写语言变体（原版/官中/汉化、整包/资源包、来源）
    bool setVariant(const QString& file, const variant::Info& info);
    /// 改索引里的英文名。没有大厅关联时，分组键就是它 —— 用来把包合并到某个本地分组
    bool setEnglishName(const QString& file, const QString& englishName);
    /// 把同组里的默认变体设为这一个（其它同组条目的 default 清掉）
    bool setDefaultVariant(const QString& file);
    /// 取同一组的全部条目（含自己），给「这张图有哪几个语言版本」用
    QList<LibraryItem> groupItems(const QString& groupKey) const;

    /// 导入成功后把识别到的元数据补进索引（不动分类，也不覆盖已有的中文名）。
    /// hallSlug / variant 是导入时玩家确认过的关联与语言，写了就覆盖（那是明确意图）。
    static void rememberImported(const QString& archivePath, const QString& mcVersion, const QString& englishName,
                                 const QString& chineseName, const QString& hallSlug = {},
                                 const variant::Info& variant = {});

   signals:
    void changed();

   private:
    bool saveIndex();

    QJsonObject m_root;           ///< 索引原样保留，改一个字段就写回，未知字段不丢
    QList<CategoryInfo> m_categories;
    QList<LibraryItem> m_items;
};

}  // namespace maps
