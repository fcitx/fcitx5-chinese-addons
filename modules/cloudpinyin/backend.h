/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CLOUDPINYIN_BACKEND_H_
#define _CLOUDPINYIN_BACKEND_H_

#include <fcitx-config/enum.h>
#include <fcitx-utils/macros.h>
#include <memory>
#include <string>
#include <string_view>

namespace fcitx::cloudpinyin {

FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, GoogleCN, Baidu);

class Backend {
public:
    FCITX_NODISCARD virtual std::string
    prepareRequest(const std::string &pinyin) = 0;
    virtual std::string parseResult(std::string_view result) = 0;
    virtual ~Backend() = default;
};

std::unique_ptr<Backend> createBackend(CloudPinyinBackend backend);

} // namespace fcitx::cloudpinyin

#endif // _CLOUDPINYIN_BACKEND_H_
