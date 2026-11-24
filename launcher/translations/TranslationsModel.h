/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <QAbstractListModel>
#include <memory>
#include <optional>

struct Language;

class TranslationsModel : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit TranslationsModel(const QString& path, QObject* parent = nullptr);
    ~TranslationsModel() override;

    // hide copy constructor
    TranslationsModel(const TranslationsModel&) = delete;
    // hide assign op
    TranslationsModel& operator=(const TranslationsModel&) = delete;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;  // NOLINT(*-default-arguments)
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;  // NOLINT(*-default-arguments)
    bool selectLanguage(QString key) const;
    /**
     * 启动时按设置装载界面语言。
     *
     * 上游只在「翻译索引下载成功」的回调里调 `selectLanguage()`；Atlas 11-23 把索引下载
     * 关掉之后，这条路永远走不到 —— 表现是**每次启动都是英文**（`mmc_zh.qm` 明明在包里）。
     * 所以启动时自己装一次：勾了「跟随系统」且系统语言有译文就用系统语言，否则用 `Language`，
     * 两者都没有时落到简体中文（Atlas 是中文优先的产品，不弹语言向导）。
     */
    void applyConfiguredLanguage();
    void updateLanguage(const QString& key);
    QModelIndex selectedIndex() const;
    QString selectedLanguage() const;

    void downloadIndex();
    void setUseSystemLocale(bool useSystemLocale) const;

   private:
    int columnCount(const QModelIndex& parent) const override;

    QList<Language>::Iterator findLanguage(const QString& key) const;
    std::optional<Language> findLanguageAsOptional(const QString& key) const;
    void reloadLocalFiles();
    void downloadTranslation(const QString& key);
    void downloadNext();

   private slots:
    void indexReceived();
    void indexFailed(const QString& reason) const;
    void dlFailed(const QString& reason);
    void dlGood();
    void translationDirChanged(const QString& path);

   private: /* data */
    struct Private;
    std::unique_ptr<Private> d;
};
