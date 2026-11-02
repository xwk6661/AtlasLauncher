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
#include "MapTextUtils.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>

namespace maps::text {

QString stripFormattingCodes(const QString& input)
{
    static const QRegularExpression formatCode(QStringLiteral("\u00a7[0-9a-fk-orx]"),
                                               QRegularExpression::CaseInsensitiveOption);
    QString out = input;
    out.remove(formatCode);
    return out;
}

QString cleanDisplayName(const QString& input)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString out = stripFormattingCodes(input);
    out.replace(whitespace, QStringLiteral(" "));
    return out.trimmed();
}

QString decodeText(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return {};

    // 严格 UTF-8：非法字节会被替换成 U+0000，据此判断是否要换编码
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInvalidToNull);
    const QString decoded = utf8.decode(bytes);
    if (!utf8.hasError() && !decoded.contains(QChar(0)))
        return decoded;

    // 中文 Windows 的本地编码就是 GBK，正是国内地图 README 的常见编码
    return QString::fromLocal8Bit(bytes);
}

QStringList extractVersionMentions(const QString& text)
{
    static const QRegularExpression release(QStringLiteral("\\b(1\\.\\d{1,2}(?:\\.\\d{1,2})?)\\b"));
    QStringList out;
    QSet<QString> seen;
    auto it = release.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString found = match.captured(1);
        if (seen.contains(found))
            continue;
        seen.insert(found);
        out.append(found);
    }
    return out;
}

bool looksLikeText(const QByteArray& bytes)
{
    const int n = std::min<qsizetype>(bytes.size(), 4096);
    if (n <= 0)
        return false;

    int printable = 0;
    for (int i = 0; i < n; ++i) {
        const auto b = static_cast<unsigned char>(bytes.at(i));
        if (b == 0)
            return false;
        if ((b >= 0x20 && b < 0x7f) || b == 0x0a || b == 0x0d || b == 0x09 || b >= 0x80)
            ++printable;
    }
    return static_cast<double>(printable) / n > 0.9;
}

}  // namespace maps::text
