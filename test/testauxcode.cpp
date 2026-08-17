/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "auxcode.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace fcitx;

static void writeTestTable(const std::string &path, const std::string &content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

static void testEmptyTable() {
    AuxCode aux;
    assert(!aux.isLoaded());
    aux.loadFromFile("/nonexistent/path.txt");
    assert(!aux.isLoaded());
    assert(aux.getFirstCode("时").empty());
    assert(aux.matchPhrase("时间", "om"));
}

static void testSingleCharMatch() {
    const std::string path = "/tmp/test_aux_code_single.txt";
    writeTestTable(path, R"(时=oc
魔=gg
厑=ib
厑=ii
)");

    AuxCode aux;
    aux.loadFromFile(path);
    assert(aux.isLoaded());

    // single code: prefix match
    assert(aux.matchPhrase("时", "o"));
    assert(aux.matchPhrase("时", "oc"));
    assert(!aux.matchPhrase("时", "m"));
    assert(!aux.matchPhrase("时", "ocd"));

    // single code: exact match
    assert(aux.matchPhrase("魔", "g"));
    assert(aux.matchPhrase("魔", "gg"));
    assert(!aux.matchPhrase("魔", "ggo"));

    // multiple codes: any code starts with input
    assert(aux.matchPhrase("厑", "i"));
    assert(aux.matchPhrase("厑", "ib"));
    assert(aux.matchPhrase("厑", "ii"));
    assert(!aux.matchPhrase("厑", "x"));

    std::remove(path.c_str());
}

static void testGetFirstCode() {
    const std::string path = "/tmp/test_aux_code_first.txt";
    writeTestTable(path, R"(时=oc
间=mo
)");

    AuxCode aux;
    aux.loadFromFile(path);
    assert(aux.getFirstCode("时") == "oc");
    assert(aux.getFirstCode("间") == "mo");
    assert(aux.getFirstCode("不").empty());

    std::remove(path.c_str());
}

static void testMatchPhrase() {
    const std::string path = "/tmp/test_aux_code_phrase.txt";
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
    assert(aux.matchPhrase("魔法少女", "g"));
    assert(aux.matchPhrase("魔法少女", "gd"));
    assert(aux.matchPhrase("魔法少女", "gdx"));

    // exact match
    assert(aux.matchPhrase("魔法少女", "gdxv"));
    assert(aux.matchPhrase("人间体", "pmr"));

    // mismatch
    assert(!aux.matchPhrase("魔法少女", "gdxvy"));
    assert(!aux.matchPhrase("人间体", "pmrx"));

    // unmatched character -> filter entire phrase
    assert(!aux.matchPhrase("没时间", "o"));

    // empty input -> keep
    assert(aux.matchPhrase("魔法少女", ""));

    // multi-char codes with English
    assert(aux.matchPhrase("多啦A梦", "d"));
    assert(aux.matchPhrase("多啦A梦", "dk"));
    assert(aux.matchPhrase("多啦A梦", "dka"));
    assert(aux.matchPhrase("多啦A梦", "dkam"));
    assert(!aux.matchPhrase("多啦A梦", "dkamx"));

    std::remove(path.c_str());
}

int main() {
    testEmptyTable();
    testSingleCharMatch();
    testGetFirstCode();
    testMatchPhrase();

    std::cout << "All auxcode tests passed!" << std::endl;
    return 0;
}
