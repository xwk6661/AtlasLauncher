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

   private:
    void refreshConfirmState();
    /// 当前实际会用的版本：用户改过就看下拉框，没改过就看所选世界的判定结果
    QString effectiveVersion() const;

    const MapScanResult m_scan;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_worldCombo = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QComboBox* m_loaderCombo = nullptr;
    QLineEdit* m_loaderVersionEdit = nullptr;
    QListWidget* m_evidenceList = nullptr;
    QLabel* m_confidenceLabel = nullptr;
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
