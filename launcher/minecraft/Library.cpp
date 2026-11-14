// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "Library.h"
#include "MinecraftInstance.h"
#include "net/Request.h"

#include <BuildConfig.h>
#include <FileSystem.h>
#include <ResourcePaths.h>
#include <net/ApiRequest.h>
#include <net/ChecksumValidator.h>
#include <net/HttpMetaCache.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

namespace {

/**
 * @brief 认领目标位置已经存在的库文件：核对 sha1，然后把 md5 与时间戳补进缓存条目。
 *
 * 账本是按**数据目录**存的（`metacache`），文件却在共享资源目录里。把 libraries
 * 指到另一个启动器的目录、或者换机器时账本不会跟着走，于是「文件明明在、缓存认为
 * 没下载过」，整份重下（本机实测 2.1 GB）。这里只认内容不认来源：
 * sha1 与元数据一致就直接记账，不一致才让调用方去下载。
 */
bool adoptExistingFile(HttpMetaCache* cache, const MetaEntryPtr& entry, const QString& expectedSha1)
{
    const QString path = entry->getFullPath();
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // 一次读完，同时算 sha1（与元数据核对）和 md5（账本里存的是它）
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QCryptographicHash md5(QCryptographicHash::Md5);
    constexpr qint64 kChunk = 256 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunk);
        if (chunk.isEmpty())
            break;
        sha1.addData(chunk);
        md5.addData(chunk);
    }
    file.close();

    const QString actual = QString::fromLatin1(sha1.result().toHex());
    if (actual.compare(expectedSha1, Qt::CaseInsensitive) != 0) {
        qDebug() << "Library file does not match its sha1, re-downloading:" << path;
        return false;
    }

    entry->setMD5Sum(QString::fromLatin1(md5.result().toHex()));
    entry->setLocalChangedTimestamp(info.lastModified().toUTC().toMSecsSinceEpoch());
    entry->makeEternal(true);
    entry->setStale(false);
    if (!cache->updateEntry(entry)) {
        entry->setStale(true);
        return false;
    }

    qInfo() << "Adopted existing library file:" << path;
    return true;
}

}  // namespace

/**
 * @brief Collect applicable files for the library.
 *
 * Depending on whether the library is native or not, it adds paths to the
 * appropriate lists for jar files, native libraries for 32-bit, and native
 * libraries for 64-bit.
 *
 * @param runtimeContext The current runtime context.
 * @param jar List to store paths for jar files.
 * @param native List to store paths for native libraries.
 * @param native32 List to store paths for 32-bit native libraries.
 * @param native64 List to store paths for 64-bit native libraries.
 * @param overridePath Optional path to override the default storage path.
 */
void Library::getApplicableFiles(const RuntimeContext& runtimeContext,
                                 QStringList& jar,
                                 QStringList& native,
                                 QStringList& native32,
                                 QStringList& native64,
                                 const QString& overridePath) const
{
    bool local = isLocal();
    // Lambda function to get the absolute file path
    auto actualPath = [this, local, overridePath](QString relPath) {
        relPath = FS::RemoveInvalidPathChars(relPath);
        QFileInfo out(FS::PathCombine(storagePrefix(), relPath));
        if (local && !overridePath.isEmpty()) {
            QString fileName = out.fileName();
            return QFileInfo(FS::PathCombine(overridePath, fileName)).absoluteFilePath();
        }
        return out.absoluteFilePath();
    };

    QString raw_storage = storageSuffix(runtimeContext);
    if (isNative()) {
        if (raw_storage.contains("${arch}")) {
            auto nat32Storage = raw_storage;
            nat32Storage.replace("${arch}", "32");
            auto nat64Storage = raw_storage;
            nat64Storage.replace("${arch}", "64");
            native32 += actualPath(nat32Storage);
            native64 += actualPath(nat64Storage);
        } else {
            native += actualPath(raw_storage);
        }
    } else {
        jar += actualPath(raw_storage);
    }
}

/**
 * @brief Get download requests for the library files.
 *
 * Depending on whether the library is native or not, and the current runtime context,
 * this function prepares download requests for the necessary files. It handles both local
 * and remote files, checks for stale cache entries, and adds checksummed downloads.
 *
 * @param runtimeContext The current runtime context.
 * @param cache Pointer to the HTTP meta cache.
 * @param failedLocalFiles List to store paths for failed local files.
 * @param overridePath Optional path to override the default storage path.
 * @return QList<Net::Request::Ptr> List of download requests.
 */
QList<Net::Request::Ptr> Library::getDownloads(const RuntimeContext& runtimeContext,
                                                  class HttpMetaCache* cache,
                                                  QStringList& failedLocalFiles,
                                                  const QString& overridePath) const
{
    QList<Net::Request::Ptr> out;
    bool stale = isAlwaysStale();
    bool local = isLocal();

    // Lambda function to check if a local file exists
    auto check_local_file = [overridePath, &failedLocalFiles](QString storage) {
        QFileInfo fileinfo(storage);
        QString fileName = fileinfo.fileName();
        auto fullPath = FS::PathCombine(overridePath, fileName);
        QFileInfo localFileInfo(fullPath);
        if (!localFileInfo.exists()) {
            failedLocalFiles.append(localFileInfo.filePath());
            return false;
        }
        return true;
    };

    // Lambda function to add a download request
    auto add_download = [this, local, check_local_file, cache, stale, &out](QString storage, QString url, QString sha1) {
        if (local) {
            return check_local_file(storage);
        }
        auto entry = cache->resolveEntry("libraries", storage);
        if (stale) {
            entry->setStale(true);
        }
        if (!entry->isStale())
            return true;
        // 账本里没有、但目标位置已经有这个文件：先核对 sha1，对得上就直接记账，不再重下
        if (!sha1.isEmpty() && adoptExistingFile(cache, entry, sha1))
            return true;
        Net::Request::Options options;
        if (stale) {
            options |= Net::Request::Option::AcceptLocalFiles;
        }

        // Don't add a time limit for the libraries cache entry validity
        options |= Net::Request::Option::MakeEternal;

        if (sha1.size()) {
            auto dl = Net::ApiRequest::makeCached(url, entry, options);
            dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, sha1));
            qDebug() << "Checksummed Download for:" << rawName().serialize() << "storage:" << storage << "url:" << url << "expected sha1:" << sha1;
            out.append(dl);
        } else {
            out.append(Net::ApiRequest::makeCached(url, entry, options));
            qDebug() << "Download for:" << rawName().serialize() << "storage:" << storage << "url:" << url;
        }
        return true;
    };

    QString raw_storage = storageSuffix(runtimeContext);
    if (m_mojangDownloads) {
        if (isNative()) {
            auto nativeClassifier = getCompatibleNative(runtimeContext);
            if (!nativeClassifier.isNull()) {
                if (nativeClassifier.contains("${arch}")) {
                    auto nat32Classifier = nativeClassifier;
                    nat32Classifier.replace("${arch}", "32");
                    auto nat64Classifier = nativeClassifier;
                    nat64Classifier.replace("${arch}", "64");
                    auto nat32info = m_mojangDownloads->getDownloadInfo(nat32Classifier);
                    if (nat32info) {
                        auto cooked_storage = raw_storage;
                        cooked_storage.replace("${arch}", "32");
                        add_download(cooked_storage, nat32info->url, nat32info->sha1);
                    }
                    auto nat64info = m_mojangDownloads->getDownloadInfo(nat64Classifier);
                    if (nat64info) {
                        auto cooked_storage = raw_storage;
                        cooked_storage.replace("${arch}", "64");
                        add_download(cooked_storage, nat64info->url, nat64info->sha1);
                    }
                } else {
                    auto info = m_mojangDownloads->getDownloadInfo(nativeClassifier);
                    if (info) {
                        add_download(raw_storage, info->url, info->sha1);
                    }
                }
            } else {
                qDebug() << "Ignoring native library" << m_name.serialize() << "because it has no classifier for current OS";
            }
        } else {
            if (m_mojangDownloads->artifact) {
                auto artifact = m_mojangDownloads->artifact;
                add_download(raw_storage, artifact->url, artifact->sha1);
            } else {
                qDebug() << "Ignoring java library" << m_name.serialize() << "because it has no artifact";
            }
        }
    } else {
        auto raw_dl = [this, raw_storage]() {
            if (!m_absoluteURL.isEmpty()) {
                return m_absoluteURL;
            }

            if (m_repositoryURL.isEmpty()) {
                return BuildConfig.LIBRARY_BASE + raw_storage;
            }

            if (m_repositoryURL.endsWith('/')) {
                return m_repositoryURL + raw_storage;
            } else {
                return m_repositoryURL + QChar('/') + raw_storage;
            }
        }();
        if (raw_storage.contains("${arch}")) {
            QString cooked_storage = raw_storage;
            QString cooked_dl = raw_dl;
            add_download(cooked_storage.replace("${arch}", "32"), cooked_dl.replace("${arch}", "32"), QString());
            cooked_storage = raw_storage;
            cooked_dl = raw_dl;
            add_download(cooked_storage.replace("${arch}", "64"), cooked_dl.replace("${arch}", "64"), QString());
        } else {
            add_download(raw_storage, raw_dl, QString());
        }
    }
    return out;
}

/**
 * @brief Check if the library is active in the given runtime context.
 *
 * This function evaluates rules to determine if the library should be active,
 * considering both general rules and native compatibility.
 *
 * @param runtimeContext The current runtime context.
 * @return bool True if the library is active, false otherwise.
 */
bool Library::isActive(const RuntimeContext& runtimeContext) const
{
    bool result = true;
    if (m_rules.empty()) {
        result = true;
    } else {
        Rule::Action ruleResult = Rule::Disallow;
        for (auto rule : m_rules) {
            Rule::Action temp = rule.apply(runtimeContext);
            if (temp != Rule::Defer)
                ruleResult = temp;
        }
        result = result && (ruleResult == Rule::Allow);
    }
    if (isNative()) {
        result = result && !getCompatibleNative(runtimeContext).isNull();
    }
    return result;
}

/**
 * @brief Check if the library is considered local.
 *
 * @return bool True if the library is local, false otherwise.
 */
bool Library::isLocal() const
{
    return m_hint == "local";
}

/**
 * @brief Check if the library is always considered stale.
 *
 * @return bool True if the library is always stale, false otherwise.
 */
bool Library::isAlwaysStale() const
{
    return m_hint == "always-stale";
}

/**
 * @brief Get the compatible native classifier for the current runtime context.
 *
 * This function attempts to match the current runtime context with the appropriate
 * native classifier.
 *
 * @param runtimeContext The current runtime context.
 * @return QString The compatible native classifier, or an empty string if none is found.
 */
QString Library::getCompatibleNative(const RuntimeContext& runtimeContext) const
{
    // try to match precise classifier "[os]-[arch]"
    auto entry = m_nativeClassifiers.constFind(runtimeContext.getClassifier());
    // try to match imprecise classifier on legacy architectures "[os]"
    if (entry == m_nativeClassifiers.constEnd() && runtimeContext.isLegacyArch())
        entry = m_nativeClassifiers.constFind(runtimeContext.system);

    if (entry == m_nativeClassifiers.constEnd())
        return QString();

    return entry.value();
}

/**
 * @brief Set the storage prefix for the library.
 *
 * @param prefix The storage prefix to set.
 */
void Library::setStoragePrefix(QString prefix)
{
    m_storagePrefix = prefix;
}

/**
 * @brief Get the default storage prefix for libraries.
 *
 * 必须走 `resource::librariesDir()`，不能返回相对的 "libraries/"。
 * 这个前缀被 getApplicableFiles() 拼成 classpath 里的 jar 路径，而相对路径是按
 * **进程当前工作目录**解析的 —— 一旦设了共享资源目录，库文件已经搬到别处，
 * classpath 却仍然指向原地，客户端启动时就是
 * `java.lang.ClassNotFoundException: net.minecraft.client.main.Main`。
 * 没设共享目录时 librariesDir() 就等于「当前目录/libraries」，与旧行为逐字节一致。
 *
 * @return QString The default storage prefix（绝对路径）。
 */
QString Library::defaultStoragePrefix()
{
    return resource::librariesDir();
}

/**
 * @brief Get the current storage prefix for the library.
 *
 * @return QString The current storage prefix.
 */
QString Library::storagePrefix() const
{
    if (m_storagePrefix.isEmpty()) {
        return defaultStoragePrefix();
    }
    return m_storagePrefix;
}

/**
 * @brief Get the filename for the library in the current runtime context.
 *
 * This function determines the appropriate filename for the library, taking into
 * account native classifiers if applicable.
 *
 * @param runtimeContext The current runtime context.
 * @return QString The filename of the library.
 */
QString Library::filename(const RuntimeContext& runtimeContext) const
{
    if (!m_filename.isEmpty()) {
        return m_filename;
    }
    // non-native? use only the gradle specifier
    if (!isNative()) {
        return m_name.getFileName();
    }

    // otherwise native, override classifiers. Mojang HACK!
    GradleSpecifier nativeSpec = m_name;
    QString nativeClassifier = getCompatibleNative(runtimeContext);
    if (!nativeClassifier.isNull()) {
        nativeSpec.setClassifier(nativeClassifier);
    } else {
        nativeSpec.setClassifier("INVALID");
    }
    return nativeSpec.getFileName();
}

/**
 * @brief Get the display name for the library in the current runtime context.
 *
 * This function returns the display name for the library, defaulting to the filename
 * if no display name is set.
 *
 * @param runtimeContext The current runtime context.
 * @return QString The display name of the library.
 */
QString Library::displayName(const RuntimeContext& runtimeContext) const
{
    if (!m_displayname.isEmpty())
        return m_displayname;
    return filename(runtimeContext);
}

/**
 * @brief Get the storage suffix for the library in the current runtime context.
 *
 * This function determines the appropriate storage suffix for the library, taking into
 * account native classifiers if applicable.
 *
 * @param runtimeContext The current runtime context.
 * @return QString The storage suffix of the library.
 */
QString Library::storageSuffix(const RuntimeContext& runtimeContext) const
{
    // non-native? use only the gradle specifier
    if (!isNative()) {
        return m_name.toPath(m_filename);
    }

    // otherwise native, override classifiers. Mojang HACK!
    GradleSpecifier nativeSpec = m_name;
    QString nativeClassifier = getCompatibleNative(runtimeContext);
    if (!nativeClassifier.isNull()) {
        nativeSpec.setClassifier(nativeClassifier);
    } else {
        nativeSpec.setClassifier("INVALID");
    }
    return nativeSpec.toPath(m_filename);
}
