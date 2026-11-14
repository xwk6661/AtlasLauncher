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

#include <QDialog>

#include <optional>

#include "MapArchiveScanner.h"
#include "MapImportTask.h"
#include "MapVariant.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QListWidget;

namespace maps {

/**
 * 导入确认对话框：名称 / 世界 / 版本 / 加载器 + 判定证据。
 *
 * 为什么要给用户看证据：LOW 置信度的地图（1.7.10~1.8.9 那批没有版本号的存档）
 * 判错就会以错误版本打开，而 MC 会**按旧格式回写世界** —— 不可逆。所以宁可多一步确认。
 */
class MapImportDialog : public QDialog {
    Q_OBJECT

   public:
    MapImportDialog(const MapScanResult& scan, QWidget* parent = nullptr);

    MapImportOptions options() const;

    /// 便捷入口：用户取消时返回空 optional
    static std::optional<MapImportOptions> ask(const MapScanResult& scan, QWidget* parent = nullptr);

   private slots:
    void updateEvidence();
    /// 切换世界时同步版本 / 加载器 / 名称（多世界包的每个存档可能是不同版本）
    void worldChanged();
    /// 语言一变，来源（作者/社区/自译）才可用；官中直接锁成"作者发布"
    void languageChanged();

   private:
    void refreshConfirmState();
    /// 当前实际会用的版本：用户改过就看下拉框，没改过就看所选世界的判定结果
    QString effectiveVersion() const;
    /// 把「语言 / 来源」两个下拉拼成变体信息（label 保留文件名的原话）
    variant::Info variantFromUi() const;

    const MapScanResult m_scan;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_worldCombo = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QComboBox* m_loaderCombo = nullptr;
    QLineEdit* m_loaderVersionEdit = nullptr;
    /// 这张图在大厅里是哪一张（slug 作为 item data，空 = 不关联）
    QComboBox* m_hallCombo = nullptr;
    /// 语言：原版 / 简中汉化 / 繁中汉化 / 官中 / 其他语言
    QComboBox* m_languageCombo = nullptr;
    /// 来源：未知 / 作者发布 / 社区汉化 / 自译
    QComboBox* m_sourceCombo = nullptr;
    /// 从文件名推断出来的原始结论（只用来给默认值与 label，不直接落库）
    variant::Info m_inferred;
    /// 实例名的基础部分（世界名或文件名）；语言一变就在后面加「（简中汉化）」这类后缀
    QString m_baseName;
    QListWidget* m_evidenceList = nullptr;
    QLabel* m_confidenceLabel = nullptr;
    /// 实验性设置 / 缺模组这类「导入前必须知道」的警告，单独一行，别和统计数字挤在一起
    QLabel* m_warningLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_okButton = nullptr;
    /// Ok 按钮的默认文案（判定不出时会被换成「请先选择版本」，之后要能换回来）
    QString m_okDefaultText;

    /// 用户是否动过版本下拉框（动过就记 decidedBy=user）
    bool m_versionTouched = false;
    /// 用户是否手改过实例名（手改过就不再跟着世界名走）
    bool m_nameTouched = false;
};

}  // namespace maps
