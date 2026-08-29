/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "backend.h"
#include <curl/curl.h>
#include <exception>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/stringutils.h>
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace fcitx::cloudpinyin {

namespace {

class GoogleBackend : public Backend {
public:
    explicit GoogleBackend(std::string url) : url_(std::move(url)) {}

    std::string prepareRequest(const std::string &pinyin) override {
        const UniqueCPtr<char, curl_free> escaped(
            curl_escape(pinyin.c_str(), static_cast<int>(pinyin.size())));
        if (!escaped) {
            return {};
        }
        const std::string url = stringutils::concat(url_, escaped.get());
        return url;
    }

    std::string parseResult(std::string_view result) override {
        try {
            const auto jv = nlohmann::json::parse(result);
            return jv.at(1).at(0).at(1).at(0).get<std::string>();
        } catch (const std::exception &) {
            return {};
        }
    }

private:
    const std::string url_;
};

class BaiduBackend : public Backend {
public:
    std::string prepareRequest(const std::string &pinyin) override {
        const UniqueCPtr<char, &curl_free> escaped(
            curl_escape(pinyin.c_str(), static_cast<int>(pinyin.size())));
        if (!escaped) {
            return {};
        }
        const std::string url =
            std::format("https://olimenew.baidu.com/"
                        "py?input={}&inputtype=py&resultcoding=utf-8",
                        escaped.get());
        return url;
    }

    std::string parseResult(std::string_view result) override {
        try {
            const auto jv = nlohmann::json::parse(result);
            if (jv.at("status") != "T" || jv.at("errno") != "0") {
                return {};
            }
            return jv.at("result").at(0).at(0).at(0).get<std::string>();
        } catch (const std::exception &) {
            return {};
        }
    }
};

} // namespace

std::unique_ptr<Backend> createBackend(CloudPinyinBackend backend) {
    switch (backend) {
    case CloudPinyinBackend::Google:
        return std::make_unique<GoogleBackend>(
            "https://www.google.com/inputtools/request?ime=pinyin&text=");
    case CloudPinyinBackend::GoogleCN:
        return std::make_unique<GoogleBackend>(
            "https://www.google.cn/inputtools/request?ime=pinyin&text=");
    case CloudPinyinBackend::Baidu:
        return std::make_unique<BaiduBackend>();
    }
    return std::make_unique<GoogleBackend>(
        "https://www.google.cn/inputtools/request?ime=pinyin&text=");
}

} // namespace fcitx::cloudpinyin
