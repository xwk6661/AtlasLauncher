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
#include "MapVariant.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace maps::variant {

namespace {

constexpr const char* kKindOriginal = "original";
constexpr const char* kKindLocalized = "localized";

constexpr const char* kLanguageUnknown = "unknown";
constexpr const char* kLanguageEnglish = "en";
constexpr const char* kLanguageSimplified = "zh_cn";
constexpr const char* kLanguageTraditional = "zh_tw";
constexpr const char* kLanguageOther = "other";

constexpr const char* kDeliveryArchive = "archive";
constexpr const char* kDeliveryResourcePack = "resourcepack";

constexpr const char* kSourceUnknown = "unknown";
constexpr const char* kSourceAuthor = "author";
constexpr const char* kSourceCommunity = "community";
constexpr const char* kSourceSelf = "self";

/// 所有括号里的内容（全角/半角都认）
QStringList parentheticalMarks(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("[（(]([^）)]*)[）)]"));
    QStringList marks;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        marks << it.next().captured(1).trimmed();
    return marks;
}

bool containsAny(const QString& haystack, const QStringList& needles)
{
    for (const auto& needle : needles) {
        if (haystack.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

}  // namespace

QString kindId(Kind kind)
{
    return kind == Kind::Localized ? QString::fromLatin1(kKindLocalized) : QString::fromLatin1(kKindOriginal);
}

Kind kindFromId(const QString& id)
{
    return id == QLatin1String(kKindLocalized) ? Kind::Localized : Kind::Original;
}

QString languageId(Language language)
{
    switch (language) {
        case Language::English:
            return QString::fromLatin1(kLanguageEnglish);
        case Language::SimplifiedChinese:
            return QString::fromLatin1(kLanguageSimplified);
        case Language::TraditionalChinese:
            return QString::fromLatin1(kLanguageTraditional);
        case Language::Other:
            return QString::fromLatin1(kLanguageOther);
        case Language::Unknown:
            break;
    }
    return QString::fromLatin1(kLanguageUnknown);
}

Language languageFromId(const QString& id)
{
    if (id == QLatin1String(kLanguageEnglish))
        return Language::English;
    if (id == QLatin1String(kLanguageSimplified))
        return Language::SimplifiedChinese;
    if (id == QLatin1String(kLanguageTraditional))
        return Language::TraditionalChinese;
    if (id == QLatin1String(kLanguageOther))
        return Language::Other;
    return Language::Unknown;
}

QString deliveryId(Delivery delivery)
{
    return delivery == Delivery::ResourcePack ? QString::fromLatin1(kDeliveryResourcePack)
                                              : QString::fromLatin1(kDeliveryArchive);
}

Delivery deliveryFromId(const QString& id)
{
    return id == QLatin1String(kDeliveryResourcePack) ? Delivery::ResourcePack : Delivery::Archive;
}

QString sourceId(Source source)
{
    switch (source) {
        case Source::Author:
            return QString::fromLatin1(kSourceAuthor);
        case Source::Community:
            return QString::fromLatin1(kSourceCommunity);
        case Source::Self:
            return QString::fromLatin1(kSourceSelf);
        case Source::Unknown:
            break;
    }
    return QString::fromLatin1(kSourceUnknown);
}

Source sourceFromId(const QString& id)
{
    if (id == QLatin1String(kSourceAuthor))
        return Source::Author;
    if (id == QLatin1String(kSourceCommunity))
        return Source::Community;
    if (id == QLatin1String(kSourceSelf))
        return Source::Self;
    return Source::Unknown;
}

QString languageLabel(Language language)
{
    switch (language) {
        case Language::English:
            return QStringLiteral("英文");
        case Language::SimplifiedChinese:
            return QStringLiteral("简中");
        case Language::TraditionalChinese:
            return QStringLiteral("繁中");
        case Language::Other:
            return QStringLiteral("其他语言");
        case Language::Unknown:
            break;
    }
    return QStringLiteral("未标注");
}

QString deliveryLabel(Delivery delivery)
{
    return delivery == Delivery::ResourcePack ? QStringLiteral("资源包") : QStringLiteral("整包");
}

QString sourceLabel(Source source)
{
    switch (source) {
        case Source::Author:
            return QStringLiteral("作者发布");
        case Source::Community:
            return QStringLiteral("社区汉化");
        case Source::Self:
            return QStringLiteral("自译");
        case Source::Unknown:
            break;
    }
    return QStringLiteral("来源未知");
}

QJsonObject Info::toJson() const
{
    QJsonObject object;
    object.insert("kind", kindId(kind));
    object.insert("language", languageId(language));
    object.insert("delivery", deliveryId(delivery));
    object.insert("source", sourceId(source));
    if (!label.isEmpty())
        object.insert("label", label);
    if (!basedOn.isEmpty())
        object.insert("basedOn", basedOn);
    if (isDefault)
        object.insert("default", true);
    return object;
}

Info Info::fromJson(const QJsonObject& object)
{
    Info info;
    info.kind = kindFromId(object.value("kind").toString());
    info.language = languageFromId(object.value("language").toString());
    info.delivery = deliveryFromId(object.value("delivery").toString());
    info.source = sourceFromId(object.value("source").toString());
    info.label = object.value("label").toString();
    info.basedOn = object.value("basedOn").toString();
    info.isDefault = object.value("default").toBool();
    return info;
}

QString Info::summary() const
{
    if (kind == Kind::Original)
        return language == Language::Unknown ? QStringLiteral("原版") : QStringLiteral("原版（%1）").arg(languageLabel(language));

    QString text = languageLabel(language);
    if (source == Source::Author)
        return QStringLiteral("官中");
    if (!label.isEmpty())
        text = label;
    if (delivery == Delivery::ResourcePack)
        text += QStringLiteral(" · 资源包");
    return text;
}

bool Info::isEmpty() const
{
    return kind == Kind::Original && language == Language::Unknown && delivery == Delivery::Archive &&
           source == Source::Unknown && label.isEmpty();
}

Info inferFromFileName(const QString& fileName, const QString& note)
{
    Info info;

    // 语言标记**只看文件名**。备注里会出现「与繁中版同图不同语言」这种描述，
    // 拿备注当证据会把英文原版判成繁中（Ragecraft III 就是这么被误判过）。
    const QString base = QFileInfo(fileName).completeBaseName();
    const QStringList marks = parentheticalMarks(base);

    static const QStringList traditional = { QStringLiteral("繁中"), QStringLiteral("繁體"), QStringLiteral("繁体"),
                                             QStringLiteral("正體"), QStringLiteral("正体") };
    static const QStringList simplified = { QStringLiteral("简中"), QStringLiteral("簡中"),
                                            QStringLiteral("简體"), QStringLiteral("简体") };
    static const QStringList official = { QStringLiteral("官中"), QStringLiteral("官方中文"),
                                          QStringLiteral("官方汉化"), QStringLiteral("官方漢化") };
    static const QStringList localized = { QStringLiteral("汉化"), QStringLiteral("漢化"),
                                           QStringLiteral("中文化"), QStringLiteral("中文版") };

    // 顺序要紧：「简中汉化」同时命中 simplified 与 localized；「官方汉化」要先判官中
    if (containsAny(base, traditional)) {
        info.kind = Kind::Localized;
        info.language = Language::TraditionalChinese;
    } else if (containsAny(base, official)) {
        info.kind = Kind::Localized;
        info.language = Language::SimplifiedChinese;
        info.source = Source::Author;
    } else if (containsAny(base, simplified) || containsAny(base, localized)) {
        info.kind = Kind::Localized;
        info.language = Language::SimplifiedChinese;
        if (containsAny(base, localized))
            info.source = Source::Community;
    }

    // 备注只用来补来源，不改语言判定
    if (info.kind == Kind::Localized) {
        if (containsAny(note, { QStringLiteral("自制"), QStringLiteral("自译"), QStringLiteral("自己翻") }))
            info.source = Source::Self;
        else if (containsAny(note, { QStringLiteral("官方") }))
            info.source = Source::Author;
    }

    // 徽章文案保留括号原话，别把「简中汉化v1.0」改写成「简中」
    for (const auto& mark : marks) {
        if (containsAny(mark, traditional) || containsAny(mark, simplified) || containsAny(mark, official) ||
            containsAny(mark, localized)) {
            info.label = mark;
            break;
        }
    }
    if (info.label.isEmpty() && info.kind == Kind::Localized)
        info.label = languageLabel(info.language);

    return info;
}

QString normalizeName(const QString& text)
{
    QString name = text;

    // 扩展名
    static const QRegularExpression suffix(QStringLiteral("\\.(zip|rar|7z|tar|gz|mcworld)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    name.remove(suffix);

    // 开头的 [1.16.5]（可能有多个）
    static const QRegularExpression leadingTag(QStringLiteral("^\\s*\\[[^\\]]*\\]\\s*"));
    while (name.contains(leadingTag))
        name.remove(leadingTag);

    // 括号内容：语言标记、作者署名、备注都在这
    static const QRegularExpression paren(QStringLiteral("[（(][^）)]*[）)]"));
    name.remove(paren);

    // 版本 token：v1.2 / 1.16.5 / MC1.21.9 / V1.0.5
    static const QRegularExpression versionToken(QStringLiteral("\\b(?:v|mc)?\\d+(?:\\.\\d+)+\\b"),
                                                 QRegularExpression::CaseInsensitiveOption);

    name = name.toLower();
    for (const QChar ch : QStringLiteral("-_:：·—–&/\\")) {
        name.replace(ch, QLatin1Char(' '));
    }

    static const QStringList stopWords = { QStringLiteral("by"), QStringLiteral("the"), QStringLiteral("a"),
                                           QStringLiteral("an"), QStringLiteral("and"), QStringLiteral("of") };

    QStringList kept;
    for (const QString& token : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (versionToken.match(token).hasMatch())
            continue;
        if (stopWords.contains(token))
            continue;
        bool hasLetterOrNumber = false;
        for (const QChar ch : token) {
            if (ch.isLetterOrNumber()) {
                hasLetterOrNumber = true;
                break;
            }
        }
        if (!hasLetterOrNumber)
            continue;
        kept << token;
    }
    return kept.join(QLatin1Char(' ')).trimmed();
}

}  // namespace maps::variant
