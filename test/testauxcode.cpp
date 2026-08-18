/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "auxcode.h"
#include <fcitx-utils/log.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace fcitx;

static std::string tempPath(const char *suffix) {
    return "/tmp/fcitx_test_aux_" + std::to_string(getpid()) + "_" + suffix;
}

static void writeTestTable(const std::string &path, const std::string &content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

static void testEmptyTable() {
    AuxCode aux;
    FCITX_ASSERT(!aux.isLoaded());
    aux.loadFromFile("/nonexistent/path.txt");
    FCITX_ASSERT(!aux.isLoaded());
    FCITX_ASSERT(aux.getFirstCode("时").empty());
    FCITX_ASSERT(aux.matchPhrase("时间", "om"));
}

static void testSingleCharMatch() {
    auto path = tempPath("single");
    writeTestTable(path, R"(时=oc
魔=gg
厑=ib
厑=ii
)");

    AuxCode aux;
    aux.loadFromFile(path);
    FCITX_ASSERT(aux.isLoaded());

    // single character: prefix match
    FCITX_ASSERT(aux.matchPhrase("时", "o"));
    FCITX_ASSERT(aux.matchPhrase("时", "oc"));
    FCITX_ASSERT(!aux.matchPhrase("时", "m"));
    FCITX_ASSERT(!aux.matchPhrase("时", "ocd"));

    // single character: boundary
    FCITX_ASSERT(aux.matchPhrase("魔", "g"));
    FCITX_ASSERT(aux.matchPhrase("魔", "gg"));
    FCITX_ASSERT(!aux.matchPhrase("魔", "ggo"));

    // multiple codes per character: any code matches
    FCITX_ASSERT(aux.matchPhrase("厑", "i"));
    FCITX_ASSERT(aux.matchPhrase("厑", "ib"));
    FCITX_ASSERT(aux.matchPhrase("厑", "ii"));
    FCITX_ASSERT(!aux.matchPhrase("厑", "x"));

    std::remove(path.c_str());
}

static void testGetFirstCode() {
    auto path = tempPath("first");
    writeTestTable(path, R"(时=oc
间=mo
)");

    AuxCode aux;
    aux.loadFromFile(path);
    FCITX_ASSERT(aux.getFirstCode("时") == "oc") << aux.getFirstCode("时");
    FCITX_ASSERT(aux.getFirstCode("间") == "mo") << aux.getFirstCode("间");
    FCITX_ASSERT(aux.getFirstCode("不").empty());

    std::remove(path.c_str());
}

static void testMatchPhrase() {
    auto path = tempPath("phrase");
    writeTestTable(path, R"(时=oc
间=mo
实=bd
践=jc
魔=gg
法=ds
少=xp
女=va
人=pn
体=rb
多=dk
啦=kl
A=a
梦=mx
)");

    AuxCode aux;
    aux.loadFromFile(path);

    // prefix match
    FCITX_ASSERT(aux.matchPhrase("魔法少女", "g"));
    FCITX_ASSERT(aux.matchPhrase("魔法少女", "gd"));
    FCITX_ASSERT(aux.matchPhrase("魔法少女", "gdx"));

    // exact match
    FCITX_ASSERT(aux.matchPhrase("魔法少女", "gdxv"));
    FCITX_ASSERT(aux.matchPhrase("人间体", "pmr"));

    // mismatch
    FCITX_ASSERT(!aux.matchPhrase("魔法少女", "gdxvy"));
    FCITX_ASSERT(!aux.matchPhrase("人间体", "pmrx"));

    // unmatched character -> filter entire phrase
    FCITX_ASSERT(!aux.matchPhrase("没时间", "o"));

    // empty input -> keep
    FCITX_ASSERT(aux.matchPhrase("魔法少女", ""));

    // multi-char codes with English
    FCITX_ASSERT(aux.matchPhrase("多啦A梦", "d"));
    FCITX_ASSERT(aux.matchPhrase("多啦A梦", "dk"));
    FCITX_ASSERT(aux.matchPhrase("多啦A梦", "dka"));
    FCITX_ASSERT(aux.matchPhrase("多啦A梦", "dkam"));
    FCITX_ASSERT(!aux.matchPhrase("多啦A梦", "dkamx"));

    std::remove(path.c_str());
}

static void testEmptyContent() {
    auto path = tempPath("empty");
    writeTestTable(path, "# comment\n\n");

    AuxCode aux;
    aux.loadFromFile(path);
    FCITX_ASSERT(aux.isLoaded());
    // file opened but no entries -> matching logic runs, characters not found
    FCITX_ASSERT(!aux.matchPhrase("时间", "o"));
    FCITX_ASSERT(aux.matchPhrase("时间", ""));

    std::remove(path.c_str());
}

int main() {
    testEmptyTable();
    testSingleCharMatch();
    testGetFirstCode();
    testMatchPhrase();
    testEmptyContent();

    FCITX_INFO() << "All auxcode tests passed!";
    return 0;
}
