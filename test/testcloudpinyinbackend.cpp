/*
 * SPDX-FileCopyrightText: 2026-2026 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "backend.h"
#include <fcitx-utils/log.h>

using namespace fcitx::cloudpinyin;

namespace {

CloudPinyinRequestContext requestContext(std::string queryPinyin,
                                         std::string fullPinyin = {}) {
    return {.queryPinyin = std::move(queryPinyin),
            .fullPinyin = std::move(fullPinyin)};
}

void testGooglePrepareRequest() {
    auto google = createBackend(CloudPinyinBackend::Google);
    auto googleCN = createBackend(CloudPinyinBackend::GoogleCN);

    FCITX_ASSERT(
        google->prepareRequest(requestContext("nihao", "ni'hao"))->url ==
        "https://www.google.com/inputtools/request?ime=pinyin&text=nihao");
    FCITX_ASSERT(
        googleCN->prepareRequest(requestContext("nihao", "ni'hao"))->url ==
        "https://www.google.cn/inputtools/request?ime=pinyin&text=nihao");
}

void testGoogleResult() {
    auto backend = createBackend(CloudPinyinBackend::Google);
    const HTTPHeaders headers;

    const auto normalResult = backend->parseResult(
        requestContext("nihao"),
        {200,
         R"(["SUCCESS",[["nihao",["你好"],[],{"annotation":["ni hao"],"candidate_type":[0],"lc":["16 16"]}]]])",
         headers});
    FCITX_ASSERT(normalResult.text == "你好");

    const auto errorResult = backend->parseResult(
        requestContext("nihao"),
        {200, R"(["FAILED_TO_PARSE_REQUEST_BODY"])", headers});
    FCITX_ASSERT(errorResult.text.empty());

    const auto invalidResult = backend->parseResult(
        requestContext("nihao"), {200, "invalid json", headers});
    FCITX_ASSERT(invalidResult.text.empty());

    const auto invalidResult2 =
        backend->parseResult(requestContext("nihao"), {200, "", headers});
    FCITX_ASSERT(invalidResult2.text.empty());
}

void testBaiduPrepareRequest() {
    auto backend = createBackend(CloudPinyinBackend::Baidu);

    FCITX_ASSERT(
        backend->prepareRequest(requestContext("nihao", "ni'hao"))->url ==
        "https://olimenew.baidu.com/"
        "py?input=nihao&inputtype=py&resultcoding=utf-8");
}

void testBaiduResult() {
    auto backend = createBackend(CloudPinyinBackend::Baidu);
    const HTTPHeaders headers;

    const auto normalResult = backend->parseResult(
        requestContext("jieguo"),
        {200,
         R"({"status":"T","errno":"0","errmsg":"","result":[[["结果",6,{"pinyin":"jie'guo","type":"IMEDICT"}]]]})",
         headers});
    FCITX_ASSERT(normalResult.text == "结果");

    const auto errorResult = backend->parseResult(
        requestContext("jieguo"),
        {200, R"({"status":"F","errno":"3","errmsg":"","result":[]})",
         headers});
    FCITX_ASSERT(errorResult.text.empty());

    const auto invalidResult = backend->parseResult(
        requestContext("jieguo"), {200, "invalid json", headers});
    FCITX_ASSERT(invalidResult.text.empty());

    const auto invalidResult2 =
        backend->parseResult(requestContext("jieguo"), {200, "", headers});
    FCITX_ASSERT(invalidResult2.text.empty());
}

} // namespace

int main() {
    testGooglePrepareRequest();
    testGoogleResult();
    testBaiduPrepareRequest();
    testBaiduResult();
    return 0;
}
