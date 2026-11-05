// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
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

#pragma once

#include <expected>
#include "Validator.h"

namespace Net {
class Sink {
   public:
    Sink() = default;
    virtual ~Sink() = default;

    enum class InitType : std::uint8_t { Ok, CacheHit };
    using InitResult = Result<InitType>;

   public:
    virtual InitResult init(QNetworkRequest& request) = 0;
    virtual Result<> write(const QByteArray& data) = 0;
    virtual Result<> finalize(QNetworkReply& reply) = 0;
    virtual void abort() = 0;

    virtual auto hasLocalData() -> bool = 0;

    /**
     * 这个 sink 支不支持"断点续传"。
     *
     * 返回 true 表示：失败时**不要把半截数据删掉**，下一轮 init() 会带着 `Range` 头
     * 从断点继续。只有写文件的 sink 能这么干（ByteArraySink 一删就全没了）。
     */
    virtual auto supportsResume() const -> bool { return false; }
    /// 已经落盘的字节数（顺序模式 = 当前文件长度；否则 0）
    virtual auto bytesWritten() const -> qint64 { return 0; }
    /// 这次请求有没有带 Range（服务端返回 200 就说明它没理这个头）
    virtual auto requestedRange() const -> bool { return false; }
    /// 服务端无视 Range 时，把半截数据丢掉、从头写
    virtual void restartFromZero() {}

    void addValidator(Validator* validator)
    {
        if (validator) {
            m_validators.push_back(std::shared_ptr<Validator>(validator));
        }
    }

   protected:
    void initAllValidators()
    {
        for (auto& validator : m_validators) {
            validator->init();
        }
    }
    void writeAllValidators(const QByteArray& data)
    {
        for (auto& validator : m_validators) {
            validator->write(data);
        }
    }
    void failAllValidators()
    {
        for (auto& validator : m_validators) {
            validator->abort();
        }
    }
    Result<> finalizeAllValidators()
    {
        for (auto& validator : m_validators) {
            auto result = validator->validate();
            if (!result) {
                return result;
            }
        }
        return {};
    }

   protected:
    std::vector<std::shared_ptr<Validator>> m_validators;
};
}  // namespace Net
