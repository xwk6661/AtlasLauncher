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
#include "DataVersionTable.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// 静态库里的 qrc 不会自动注册（Qt 只在可执行目标里生成初始化代码）。
// Q_INIT_RESOURCE 必须在全局作用域展开，所以放在 namespace maps 外面。
static void ensureMapsResources()
{
    Q_INIT_RESOURCE(maps);
}

namespace maps {

const DataVersionTable& DataVersionTable::instance()
{
    static const DataVersionTable table = []() {
        ensureMapsResources();
        DataVersionTable loaded;
        QString error;
        if (!loaded.loadFromFile(QStringLiteral(":/maps/dataversion.json"), &error)) {
            qWarning() << "Failed to load dataversion table:" << error;
        }
        return loaded;
    }();
    return table;
}

bool DataVersionTable::loadFromFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("无法打开 %1：%2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr)
            *error = QStringLiteral("%1 不是合法的 JSON：%2").arg(path, parseError.errorString());
        return false;
    }

    const auto root = document.object();
    m_generatedAt = root.value(QStringLiteral("generatedAt")).toString();

    m_byVersion.clear();
    m_byDataVersion.clear();
    m_releases.clear();

    const auto byVersion = root.value(QStringLiteral("byVersion")).toObject();
    for (auto it = byVersion.constBegin(); it != byVersion.constEnd(); ++it) {
        const auto item = it.value().toObject();
        DataVersionEntry entry;
        entry.dataVersion = item.value(QStringLiteral("dv")).toInt(-1);
        entry.packFormat = item.value(QStringLiteral("pack")).toInt(-1);
        entry.type = item.value(QStringLiteral("type")).toString();
        entry.time = item.value(QStringLiteral("time")).toString();
        entry.source = item.value(QStringLiteral("src")).toString();
        m_byVersion.insert(it.key(), entry);
    }

    const auto byDataVersion = root.value(QStringLiteral("byDataVersion")).toObject();
    for (auto it = byDataVersion.constBegin(); it != byDataVersion.constEnd(); ++it) {
        bool ok = false;
        const int dataVersion = it.key().toInt(&ok);
        if (ok)
            m_byDataVersion.insert(dataVersion, it.value().toString());
    }

    const auto releases = root.value(QStringLiteral("releases")).toArray();
    m_releases.reserve(releases.size());
    for (const auto& value : releases)
        m_releases.append(value.toString());

    m_loaded = !m_byVersion.isEmpty();
    if (!m_loaded && error != nullptr)
        *error = QStringLiteral("%1 里没有 byVersion 数据").arg(path);
    return m_loaded;
}

std::optional<DataVersionEntry> DataVersionTable::entry(const QString& id) const
{
    const auto it = m_byVersion.constFind(id);
    if (it == m_byVersion.constEnd())
        return std::nullopt;
    return *it;
}

QString DataVersionTable::versionForDataVersion(int dataVersion) const
{
    return m_byDataVersion.value(dataVersion);
}

std::optional<std::array<int, 3>> DataVersionTable::parseReleaseKey(const QString& id)
{
    static const QRegularExpression release(QStringLiteral("^(\\d{1,2})\\.(\\d{1,2})(?:\\.(\\d{1,2}))?$"));
    const auto match = release.match(id);
    if (!match.hasMatch())
        return std::nullopt;
    return std::array<int, 3>{ match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt() };
}

int DataVersionTable::compareRelease(const QString& a, const QString& b)
{
    const auto left = parseReleaseKey(a);
    const auto right = parseReleaseKey(b);
    if (!left || !right)
        return QString::compare(a, b);
    for (size_t i = 0; i < left->size(); ++i) {
        if ((*left)[i] != (*right)[i])
            return (*left)[i] - (*right)[i];
    }
    return 0;
}

int DataVersionTable::javaMajorFor(const QString& mcVersion)
{
    if (!parseReleaseKey(mcVersion))
        return -1;
    // 官方口径：1.17 仍是 Java 16（1.18 才升到 17），别按文档里那条"1.17 -> 17"写
    if (compareRelease(mcVersion, QStringLiteral("1.16.5")) <= 0)
        return 8;
    if (compareRelease(mcVersion, QStringLiteral("1.17.1")) <= 0)
        return 16;
    if (compareRelease(mcVersion, QStringLiteral("1.20.4")) <= 0)
        return 17;
    return 21;
}

bool DataVersionTable::supportsQuickPlay(const QString& mcVersion)
{
    if (!parseReleaseKey(mcVersion))
        return false;
    return compareRelease(mcVersion, QStringLiteral("1.20")) >= 0;
}

QString DataVersionTable::nearestReleaseAtOrBelow(const QString& id) const
{
    if (m_releases.contains(id))
        return id;

    QString best;
    for (const auto& release : m_releases) {
        if (compareRelease(release, id) > 0)
            continue;
        if (best.isEmpty() || compareRelease(release, best) > 0)
            best = release;
    }
    return best;
}

}  // namespace maps
