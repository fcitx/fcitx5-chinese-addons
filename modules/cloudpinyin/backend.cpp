/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "backend.h"
#include <charconv>
#include <curl/curl.h>
#include <exception>
#ifdef FCITX_HAS_LUA
#include <fcitx-config/rawconfig.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <luaaddon_public.h>
#endif
#include <algorithm>
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

    std::optional<HTTPRequest>
    prepareRequest(const CloudPinyinRequestContext &context) override {
        const UniqueCPtr<char, curl_free> escaped(
            curl_escape(context.queryPinyin.c_str(),
                        static_cast<int>(context.queryPinyin.size())));
        if (!escaped) {
            return std::nullopt;
        }
        const std::string url = stringutils::concat(url_, escaped.get());
        return HTTPRequest{.url = url};
    }

    CloudPinyinResult parseResult(const CloudPinyinRequestContext &,
                                  const HTTPResponse &response) override {
        try {
            const auto jv = nlohmann::json::parse(response.body);
            return {.text = jv.at(1).at(0).at(1).at(0).get<std::string>()};
        } catch (const std::exception &) {
            return {};
        }
    }

private:
    const std::string url_;
};

class BaiduBackend : public Backend {
public:
    std::optional<HTTPRequest>
    prepareRequest(const CloudPinyinRequestContext &context) override {
        const UniqueCPtr<char, &curl_free> escaped(
            curl_escape(context.queryPinyin.c_str(),
                        static_cast<int>(context.queryPinyin.size())));
        if (!escaped) {
            return std::nullopt;
        }
        const std::string url =
            std::format("https://olimenew.baidu.com/"
                        "py?input={}&inputtype=py&resultcoding=utf-8",
                        escaped.get());
        return HTTPRequest{.url = url};
    }

    CloudPinyinResult parseResult(const CloudPinyinRequestContext &,
                                  const HTTPResponse &response) override {
        try {
            const auto jv = nlohmann::json::parse(response.body);
            if (jv.at("status") != "T" || jv.at("errno") != "0") {
                return {};
            }
            return {.text =
                        jv.at("result").at(0).at(0).at(0).get<std::string>()};
        } catch (const std::exception &) {
            return {};
        }
    }
};

#ifdef FCITX_HAS_LUA
bool validHTTPToken(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return c > 0x20 && c < 0x7f &&
                      std::string_view("()<>@,;:\\\"/[]?={} \t").find(c) ==
                          std::string_view::npos;
           });
}

bool validHeaderValue(std::string_view value) {
    return value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

class LuaBackend : public Backend {
public:
    LuaBackend(fcitx::AddonManager *manager, std::string provider)
        : manager_(manager), provider_(std::move(provider)) {}

    std::optional<HTTPRequest>
    prepareRequest(const CloudPinyinRequestContext &context) override {
        auto *imeapi = manager_->addon("imeapi", true);
        if (!imeapi) {
            return std::nullopt;
        }
        fcitx::RawConfig config;
        config["provider"].setValue(provider_);
        config["pinyin"].setValue(context.fullPinyin);
        config["input"].setValue(context.input);
        config["selected"].setValue(context.selected);
        config["first"].setValue(context.first);
        if (context.before) {
            config["before"].setValue(*context.before);
        }
        if (context.after) {
            config["after"].setValue(*context.after);
        }
        config["program"].setValue(context.program);
        config["session"].setValue(context.session);
        const auto result = imeapi->call<fcitx::ILuaAddon::invokeLuaFunction>(
            nullptr, "cloudPinyinRequest", config);
        const auto *url = result.valueByPath("url");
        if (!url || url->empty()) {
            return std::nullopt;
        }

        HTTPRequest request;
        request.url = *url;
        if (const auto *method = result.valueByPath("method");
            method && !method->empty()) {
            request.method = *method;
        }
        if (!validHTTPToken(request.method)) {
            return std::nullopt;
        }
        if (const auto *timeout = result.valueByPath("timeout");
            timeout && !timeout->empty()) {
            long value;
            const auto [ptr, error] = std::from_chars(
                timeout->data(), timeout->data() + timeout->size(), value);
            if (error != std::errc{} ||
                ptr != timeout->data() + timeout->size() || value < 1 ||
                value > 60) {
                return std::nullopt;
            }
            request.timeout = value;
        }
        if (const auto *body = result.valueByPath("body")) {
            request.body = *body;
        }
        if (const auto headers = result.get("headers")) {
            for (const auto &name : headers->subItems()) {
                const auto value = headers->get(name);
                if (!value || !validHTTPToken(name) ||
                    !validHeaderValue(value->value())) {
                    return std::nullopt;
                }
                request.headers.emplace_back(name, value->value());
            }
        }
        return request;
    }

    CloudPinyinResult parseResult(const CloudPinyinRequestContext &context,
                                  const HTTPResponse &response) override {
        auto *imeapi = manager_->addon("imeapi", true);
        if (!imeapi) {
            return {};
        }
        fcitx::RawConfig config;
        config["provider"].setValue(provider_);
        config["pinyin"].setValue(context.fullPinyin);
        auto &responseConfig = config["response"];
        responseConfig["status"].setValue(std::to_string(response.status));
        responseConfig["body"].setValue(std::string(response.body));
        for (const auto &[name, value] : response.headers) {
            responseConfig["headers"][name].setValue(value);
        }
        const auto result = imeapi->call<fcitx::ILuaAddon::invokeLuaFunction>(
            nullptr, "cloudPinyinResponse", config);
        const auto *text = result.valueByPath("text");
        if (!text) {
            return {};
        }
        CloudPinyinResult candidate{.text = *text};
        if (const auto *comment = result.valueByPath("comment")) {
            candidate.comment = *comment;
        }
        return candidate;
    }

    std::optional<std::string>
    cacheKey(const CloudPinyinRequestContext &) const override {
        return std::nullopt;
    }

private:
    fcitx::AddonManager *manager_;
    std::string provider_;
};
#endif

} // namespace

std::shared_ptr<Backend> createBackend(CloudPinyinBackend backend,
                                       fcitx::AddonManager *manager,
                                       std::string provider) {
#ifndef FCITX_HAS_LUA
    FCITX_UNUSED(manager);
    FCITX_UNUSED(provider);
#endif
    switch (backend) {
    case CloudPinyinBackend::Google:
        return std::make_shared<GoogleBackend>(
            "https://www.google.com/inputtools/request?ime=pinyin&text=");
    case CloudPinyinBackend::GoogleCN:
        return std::make_shared<GoogleBackend>(
            "https://www.google.cn/inputtools/request?ime=pinyin&text=");
    case CloudPinyinBackend::Baidu:
        return std::make_shared<BaiduBackend>();
#ifdef FCITX_HAS_LUA
    case CloudPinyinBackend::Lua:
        if (manager) {
            return std::make_shared<LuaBackend>(manager, std::move(provider));
        }
        return nullptr;
#endif
    }
    return std::make_shared<GoogleBackend>(
        "https://www.google.cn/inputtools/request?ime=pinyin&text=");
}

} // namespace fcitx::cloudpinyin
