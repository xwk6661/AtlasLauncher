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
#include "ResourcePaths.h"

#include <QDir>

#include "Application.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

namespace resource {

QString sharedRoot()
{
    QString path = APPLICATION->settings()->get("SharedResourceDir").toString().trimmed();
    if (path.isEmpty())
        return {};

    path = QDir::fromNativeSeparators(path);
    // 允许直接从资源管理器粘贴带引号的路径
    if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2);
    path = QDir::cleanPath(path);
    while (path.size() > 3 && path.endsWith(QLatin1Char('/')))
        path.chop(1);

    // 指到数据目录本身 = 没改
    if (path == QDir::cleanPath(APPLICATION->dataRoot()))
        return {};
    return path;
}

QString dirFor(const QString& name)
{
    const QString root = sharedRoot();
    if (!root.isEmpty())
        return FS::PathCombine(root, name);
    // 没设置时保持原来的行为：相对当前工作目录（启动器启动时切到数据目录）
    return QDir::current().absoluteFilePath(name);
}

QString assetsDir()
{
    return dirFor(QStringLiteral("assets"));
}

QString librariesDir()
{
    return dirFor(QStringLiteral("libraries"));
}

QString javaDir()
{
    const QString root = sharedRoot();
    if (!root.isEmpty())
        return FS::PathCombine(root, "java");
    // JavaDir 是既有设置项（默认相对路径 "java"），没设共享目录时照旧用它
    const QString configured = APPLICATION->settings()->get("JavaDir").toString();
    if (QDir::isAbsolutePath(configured))
        return QDir::cleanPath(configured);
    return QDir::current().absoluteFilePath(configured);
}

}  // namespace resource
