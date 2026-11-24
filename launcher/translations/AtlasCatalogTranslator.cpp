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
#include "AtlasCatalogTranslator.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * 静态库里的 qrc 不会自动注册（与 `maps.qrc` / `agents.qrc` 同理）。
 *
 * 而且这个翻译器比 `main()` 里的那批 `Q_INIT_RESOURCE` **更早**被构造：
 * `TranslationsModel` 在 `Application` 构造函数里就选语言，那时还没走到 main 的初始化。
 * 所以必须在这里自己注册一次。`Q_INIT_RESOURCE` 要在全局作用域展开，
 * 否则会声明成别的命名空间里的符号（见 `AgentBootstrap.cpp` 的同类注释）。
 */
static void ensureI18nResources()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    Q_INIT_RESOURCE(i18n);
}

AtlasCatalogTranslator::AtlasCatalogTranslator(QObject* parent) : QTranslator(parent)
{
    ensureI18nResources();
}

bool AtlasCatalogTranslator::loadCatalog(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Atlas translations: 词表不是合法 JSON：" << path << error.errorString();
        return false;
    }

    const auto object = document.object();
    m_entries.clear();
    m_entries.reserve(object.size());
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString translated = it.value().toString();
        if (!it.key().isEmpty() && !translated.isEmpty())
            m_entries.insert(it.key(), translated);
    }
    return !m_entries.isEmpty();
}

bool AtlasCatalogTranslator::loadForLanguage(const QString& langCode)
{
    m_entries.clear();

    const QString code = langCode.trimmed();
    if (code.isEmpty())
        return false;

    const QString language = code.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('@'), 0, 0).section(QLatin1Char('-'), 0, 0);
    // 中文界面不装：源串就是中文，硬套一份英文回译只会把术语改坏
    if (language.compare(QLatin1String("zh"), Qt::CaseInsensitive) == 0)
        return false;

    const QStringList candidates{
        QStringLiteral(":/i18n/atlas_%1.json").arg(code),
        QStringLiteral(":/i18n/atlas_%1.json").arg(language),
        QStringLiteral(":/i18n/atlas_en.json"),
    };

    QStringList tried;
    for (const auto& path : candidates) {
        if (tried.contains(path))
            continue;
        tried.append(path);
        if (loadCatalog(path)) {
            qDebug() << "Atlas translations: 已加载补充词表" << path << "（" << m_entries.size() << "条）";
            return true;
        }
    }

    qDebug() << "Atlas translations: 没有可用于" << code << "的补充词表";
    return false;
}

QString AtlasCatalogTranslator::translate(const char* context, const char* sourceText, const char* disambiguation, int n) const
{
    Q_UNUSED(context)
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)

    if (sourceText == nullptr || m_entries.isEmpty())
        return {};

    // 源串在代码里是 UTF-8；词表键也是 UTF-8。按 QString 查，避开编码假设
    return m_entries.value(QString::fromUtf8(sourceText));
}

bool AtlasCatalogTranslator::isEmpty() const
{
    return m_entries.isEmpty();
}
