/*
 * SPDX-FileCopyrightText: 2024
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
    assert(!aux.anyCodeStartsWith("时", "o"));
    assert(aux.getFirstCode("时").empty());
    assert(aux.matchPhrase("时间", "om"));
}

static void testLoadAndSingleCharMatch() {
    const std::string path = "/tmp/test_aux_code.txt";
    writeTestTable(path, R"(# comment line
时=oc
间=mo
实=bd
践=jc
魔=gg
法=ds
少=xp
女=va
人=pn
体=rb
)");

    AuxCode aux;
    aux.loadFromFile(path);
    assert(aux.isLoaded());

    assert(aux.anyCodeStartsWith("时", "o"));
    assert(aux.anyCodeStartsWith("时", "oc"));
    assert(!aux.anyCodeStartsWith("时", "m"));
    assert(!aux.anyCodeStartsWith("时", "ocd"));

    assert(aux.anyCodeStartsWith("魔", "g"));
    assert(aux.anyCodeStartsWith("魔", "gg"));
    assert(!aux.anyCodeStartsWith("魔", "ggo"));

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

static void testMultiCode() {
    const std::string path = "/tmp/test_aux_code_multi.txt";
    writeTestTable(path, R"(厑=ib
厑=ii
魔=gg
)");

    AuxCode aux;
    aux.loadFromFile(path);

    assert(aux.anyCodeStartsWith("厑", "i"));
    assert(aux.anyCodeStartsWith("厑", "ib"));
    assert(aux.anyCodeStartsWith("厑", "ii"));
    assert(!aux.anyCodeStartsWith("厑", "x"));

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
)");

    AuxCode aux;
    aux.loadFromFile(path);

    // input length < char count -> prefix match -> keep
    assert(aux.matchPhrase("时间", "o"));
    assert(aux.matchPhrase("时间", "om"));
    assert(aux.matchPhrase("魔法少女", "g"));
    assert(aux.matchPhrase("魔法少女", "gd"));
    assert(aux.matchPhrase("魔法少女", "gdx"));

    // input length == char count, all match -> keep
    assert(aux.matchPhrase("时间", "om"));
    assert(aux.matchPhrase("魔法少女", "gdxv"));

    // input length == char count, mismatch -> filter
    assert(!aux.matchPhrase("时间", "og"));
    assert(!aux.matchPhrase("时间", "xm"));

    // input length > char count -> filter
    assert(!aux.matchPhrase("时间", "omx"));
    assert(!aux.matchPhrase("魔法少女", "gdxvy"));

    // unmatched character -> filter entire phrase
    assert(!aux.matchPhrase("没时间", "o"));

    // empty input -> keep
    assert(aux.matchPhrase("时间", ""));

    std::remove(path.c_str());
}

static void testStrokeKeyChars() {
    const std::string path = "/tmp/test_aux_code_stroke.txt";
    writeTestTable(path, R"(一=h
丨=s
丿=p
㇏=n
𠃍=z
)");

    AuxCode aux;
    aux.loadFromFile(path);
    assert(aux.anyCodeStartsWith("一", "h"));
    assert(aux.anyCodeStartsWith("丨", "s"));
    assert(aux.anyCodeStartsWith("丿", "p"));
    assert(aux.anyCodeStartsWith("㇏", "n"));
    assert(aux.anyCodeStartsWith("𠃍", "z"));

    std::remove(path.c_str());
}

static void testBehaviorExamples() {
    const std::string path = "/tmp/test_aux_code_examples.txt";
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

    // Example 1: shijian + "o"
    assert(aux.matchPhrase("时间", "o"));
    assert(!aux.matchPhrase("实践", "o"));

    // Example 1: shijian + "om"
    assert(aux.matchPhrase("时间", "om"));

    // Example 2: mofaxxx + "g"
    assert(aux.matchPhrase("魔法少女", "g"));
    assert(aux.matchPhrase("魔法少女", "gd"));
    assert(aux.matchPhrase("魔法少女", "gdx"));
    assert(aux.matchPhrase("魔法少女", "gdxv"));
    assert(!aux.matchPhrase("魔法少女", "gdxvy"));

    // Example 3: renjianti
    assert(aux.matchPhrase("人间体", "p"));
    assert(aux.matchPhrase("人间体", "pm"));
    assert(aux.matchPhrase("人间体", "pmr"));
    assert(!aux.matchPhrase("人间体", "pmrx"));

    // Multi-char codes
    assert(aux.matchPhrase("多啦A梦", "d"));
    assert(aux.matchPhrase("多啦A梦", "dk"));
    assert(aux.matchPhrase("多啦A梦", "dka"));
    assert(aux.matchPhrase("多啦A梦", "dkam"));
    assert(!aux.matchPhrase("多啦A梦", "dkamx"));

    std::remove(path.c_str());
}

int main() {
    testEmptyTable();
    testLoadAndSingleCharMatch();
    testGetFirstCode();
    testMultiCode();
    testMatchPhrase();
    testStrokeKeyChars();
    testBehaviorExamples();

    std::cout << "All auxcode tests passed!" << std::endl;
    return 0;
}
