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

void testGooglePrepareRequest() {
    auto google = createBackend(CloudPinyinBackend::Google);
    auto googleCN = createBackend(CloudPinyinBackend::GoogleCN);

    FCITX_ASSERT(
        google->prepareRequest("nihao") ==
        "https://www.google.com/inputtools/request?ime=pinyin&text=nihao");
    FCITX_ASSERT(
        googleCN->prepareRequest("nihao") ==
        "https://www.google.cn/inputtools/request?ime=pinyin&text=nihao");
}

void testGoogleResult() {
    auto backend = createBackend(CloudPinyinBackend::Google);

    const auto normalResult = backend->parseResult(
        R"(["SUCCESS",[["nihao",["你好"],[],{"annotation":["ni hao"],"candidate_type":[0],"lc":["16 16"]}]]])");
    FCITX_ASSERT(normalResult == "你好");

    const auto errorResult =
        backend->parseResult(R"(["FAILED_TO_PARSE_REQUEST_BODY"])");
    FCITX_ASSERT(errorResult.empty());

    const auto invalidResult = backend->parseResult("invalid json");
    FCITX_ASSERT(invalidResult.empty());

    const auto invalidResult2 = backend->parseResult("");
    FCITX_ASSERT(invalidResult2.empty());
}

void testBaiduPrepareRequest() {
    auto backend = createBackend(CloudPinyinBackend::Baidu);

    FCITX_ASSERT(backend->prepareRequest("nihao") ==
                 "https://olimenew.baidu.com/"
                 "py?input=nihao&inputtype=py&resultcoding=utf-8");
}

void testBaiduResult() {
    auto backend = createBackend(CloudPinyinBackend::Baidu);

    const auto normalResult = backend->parseResult(
        R"({"status":"T","errno":"0","errmsg":"","result":[[["结果",6,{"pinyin":"jie'guo","type":"IMEDICT"}]]]})");
    FCITX_ASSERT(normalResult == "结果");

    const auto errorResult = backend->parseResult(
        R"({"status":"F","errno":"3","errmsg":"","result":[]})");
    FCITX_ASSERT(errorResult.empty());

    const auto invalidResult = backend->parseResult("invalid json");
    FCITX_ASSERT(invalidResult.empty());

    const auto invalidResult2 = backend->parseResult("");
    FCITX_ASSERT(invalidResult2.empty());
}

} // namespace

int main() {
    testGooglePrepareRequest();
    testGoogleResult();
    testBaiduPrepareRequest();
    testBaiduResult();
    return 0;
}
