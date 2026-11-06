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
#include <QString>

class QLabel;
class QFrame;
class QPushButton;

class ImportChoiceDialog : public QDialog {
    Q_OBJECT

   public:
    enum class Choice {
        Map,      ///< 作为地图存档导入 (Atlas Map Import)
        Modpack,  ///< 作为整合导入 (Prism Modpack Import)
        Cancel    ///< 取消导入
    };

    explicit ImportChoiceDialog(const QString& filePath, QWidget* parent = nullptr);
    ~ImportChoiceDialog() override = default;

    Choice choice() const { return m_choice; }

    /**
     * 弹出导入方式抉择对话框：
     * 返回用户选择的导入模式（Map / Modpack / Cancel）。
     */
    static Choice ask(const QString& filePath, QWidget* parent = nullptr);

   private slots:
    void onMapClicked();
    void onModpackClicked();

   private:
    void inspectFile();

   private:
    QString m_filePath;
    Choice m_choice = Choice::Cancel;
    QLabel* m_recTitleLabel = nullptr;
    QLabel* m_recDetailLabel = nullptr;
    QFrame* m_hintBox = nullptr;
    QPushButton* m_mapBtn = nullptr;
    QPushButton* m_modpackBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};
