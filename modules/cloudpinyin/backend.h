/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CLOUDPINYIN_BACKEND_H_
#define _CLOUDPINYIN_BACKEND_H_

#include "cloudpinyin_public.h"
#include <fcitx-config/enum.h>
#include <fcitx-utils/macros.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx {
class AddonManager;
}
namespace fcitx::cloudpinyin {

#ifdef FCITX_HAS_LUA
FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, GoogleCN, Baidu, Lua);
#else
FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, GoogleCN, Baidu);
#endif

using HTTPHeaders = std::vector<std::pair<std::string, std::string>>;

struct CloudPinyinRequestContext {
    std::string queryPinyin{};
    std::string fullPinyin{};
    std::string input{};
    std::string selected{};
    std::string first{};
    std::optional<std::string> before{};
    std::optional<std::string> after{};
    std::string program{};
    std::string session{};
};

struct HTTPRequest {
    std::string url{};
    std::string method = "GET";
    HTTPHeaders headers{};
    std::string body{};
    long timeout = 10;
};

struct HTTPResponse {
    long status;
    std::string_view body;
    const HTTPHeaders &headers;
};

class Backend {
public:
    FCITX_NODISCARD virtual std::optional<HTTPRequest>
    prepareRequest(const CloudPinyinRequestContext &context) = 0;
    virtual CloudPinyinResult
    parseResult(const CloudPinyinRequestContext &context,
                const HTTPResponse &response) = 0;
    virtual std::optional<std::string>
    cacheKey(const CloudPinyinRequestContext &context) const {
        return context.queryPinyin;
    }
    virtual ~Backend() = default;
};

std::shared_ptr<Backend> createBackend(CloudPinyinBackend backend,
                                       fcitx::AddonManager *manager = nullptr,
                                       std::string provider = {});

} // namespace fcitx::cloudpinyin

#endif // _CLOUDPINYIN_BACKEND_H_
