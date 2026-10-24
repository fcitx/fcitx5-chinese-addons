/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "cloudpinyin.h"
#include "backend.h"
#include "cloudpinyin_public.h"
#include "fetch.h"
#include <cstdint>
#include <curl/curl.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>

namespace fcitx::cloudpinyin {

namespace {

#define CLOUDPINYIN_DEBUG() FCITX_LOGC(cloudpinyin, Debug)
FCITX_DEFINE_LOG_CATEGORY(cloudpinyin, "cloudpinyin");

constexpr int MAX_ERROR = 10;
constexpr uint64_t minInUs = 60000000;

std::string sessionID(const fcitx::ICUUID &uuid) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(uuid.size() * 2);
    for (const auto byte : uuid) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0xf]);
    }
    return result;
}

} // namespace

CloudPinyin::CloudPinyin(fcitx::AddonManager *manager)
    : eventLoop_(manager->eventLoop()),
      dispatcher_(manager->instance()->eventDispatcher()), manager_(manager) {
    curl_global_init(CURL_GLOBAL_ALL);

    backends_.emplace(CloudPinyinBackend::Google,
                      createBackend(CloudPinyinBackend::Google));
    backends_.emplace(CloudPinyinBackend::GoogleCN,
                      createBackend(CloudPinyinBackend::GoogleCN));
    backends_.emplace(CloudPinyinBackend::Baidu,
                      createBackend(CloudPinyinBackend::Baidu));

    resetError_ =
        eventLoop_->addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), minInUs,
                                 [this](EventSourceTime *, uint64_t) {
                                     resetError();
                                     return true;
                                 });
    if (resetError_) {
        resetError_->setEnabled(false);
    }
    thread_ = std::make_unique<FetchThread>(this);

    reloadConfig();
}

CloudPinyin::~CloudPinyin() {}

void CloudPinyin::reloadConfig() {
    readAsIni(config_, "conf/cloudpinyin.conf");
#ifdef FCITX_HAS_LUA
    updateLuaBackend();
#endif
}

void CloudPinyin::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    fcitx::safeSaveAsIni(config_, "conf/cloudpinyin.conf");
#ifdef FCITX_HAS_LUA
    updateLuaBackend();
#endif
}

#ifdef FCITX_HAS_LUA
void CloudPinyin::updateLuaBackend() {
    backends_[CloudPinyinBackend::Lua] = createBackend(
        CloudPinyinBackend::Lua, manager_, config_.luaProvider.value());
}
#endif

void CloudPinyin::request(const std::string &pinyin,
                          CloudPinyinCallback callback) {
    requestWithContext(
        nullptr, pinyin, pinyin, "", "", "",
        [callback = std::move(callback)](const std::string &requestPinyin,
                                         const CloudPinyinResult &result) {
            callback(requestPinyin, result.text);
        });
}

void CloudPinyin::requestWithContext(fcitx::InputContext *inputContext,
                                     const std::string &queryPinyin,
                                     const std::string &fullPinyin,
                                     const std::string &input,
                                     const std::string &selected,
                                     const std::string &first,
                                     CloudPinyinResultCallback callback) {
    CloudPinyinRequestContext context{.queryPinyin = queryPinyin,
                                      .fullPinyin = fullPinyin,
                                      .input = input,
                                      .selected = selected,
                                      .first = first};
    if (inputContext) {
        context.program = inputContext->program();
        context.session = sessionID(inputContext->uuid());
        const auto &surrounding = inputContext->surroundingText();
        if (surrounding.isValid()) {
            const auto &text = surrounding.text();
            const auto cursor =
                utf8::ncharByteLength(text.begin(), surrounding.cursor());
            context.before = text.substr(0, cursor);
            context.after = text.substr(cursor);
        }
    }
    requestImpl(context, std::move(callback));
}

void CloudPinyin::requestImpl(const CloudPinyinRequestContext &context,
                              CloudPinyinResultCallback callback) {
    if (static_cast<int>(context.queryPinyin.size()) <
        config_.minimumLength.value()) {
        callback(context.queryPinyin, {});
        return;
    }
    auto backend = config_.backend.value();
    auto iter = backends_.find(backend);
    if (iter == backends_.end() || !iter->second || errorCount_ >= MAX_ERROR) {
        callback(context.queryPinyin, {});
        return;
    }
    auto b = iter->second;
    if (const auto cacheKey = b->cacheKey(context)) {
        if (auto *value = cache_.find(*cacheKey)) {
            callback(context.queryPinyin, *value);
            return;
        }
    }
    if (!thread_->addRequest(
            [proxy = *config_.proxy, b, &context, &callback](CurlQueue *queue) {
                const auto request = b->prepareRequest(context);
                if (!request || !queue->setupRequest(*request, proxy)) {
                    queue->release();
                    return false;
                }
                queue->setContext(context);
                queue->setBackend(b);
                queue->setBusy();
                queue->setCallback(callback);
                return true;
            })) {
        callback(context.queryPinyin, {});
    }
}

void CloudPinyin::notifyFinished() {
    dispatcher_.scheduleWithContext(this->watch(), [this]() {
        CurlQueue *item;

        while ((item = thread_->popFinished())) {
            if (!item->succeeded() || item->httpCode() != 200) {
                errorCount_ += 1;

                if (errorCount_ == MAX_ERROR && resetError_) {
                    FCITX_ERROR() << "Cloud pinyin reaches max error. "
                                     "Retry in 5 minutes.";
                    resetError_->setNextInterval(minInUs * 5);
                    resetError_->setOneShot();
                }
            }

            CloudPinyinResult result;
            const auto backend = item->backend();
            if (backend) {
                const HTTPResponse response{
                    item->httpCode(),
                    std::string_view(item->result().data(),
                                     item->result().size()),
                    item->headers()};
                CLOUDPINYIN_DEBUG()
                    << "Request response status: " << response.status;
                result = backend->parseResult(item->context(), response);
            }
            item->callback()(item->context().queryPinyin, result);
            if (!result.text.empty()) {
                if (const auto cacheKey = backend->cacheKey(item->context())) {
                    cache_.insert(*cacheKey, result);
                }
            }
            item->release();
        }
        return true;
    });
}

FCITX_ADDON_FACTORY_V2(cloudpinyin, CloudPinyinFactory);

} // namespace fcitx::cloudpinyin
