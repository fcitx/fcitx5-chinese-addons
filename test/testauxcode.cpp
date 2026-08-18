/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "auxcode.h"
#include <fcitx-utils/log.h>
#include <sstream>
#include <string>

using namespace fcitx;

// Unloaded table: matchPhrase passes through
static void testEmptyTable() {
    AuxCode aux;
    FCITX_ASSERT(!aux.isLoaded());
    FCITX_ASSERT(aux.matchPhrase("时间", "om"));
}

// Single character: prefix match and boundary
static void testSingleCharMatch() {
    std::istringstream iss(R"(时=oc
魔=gg
)");

    AuxCode aux;
    aux.loadFromStream(iss);
    FCITX_ASSERT(aux.isLoaded());

    // prefix match
    FCITX_ASSERT(aux.matchPhrase("时", "o"));
    FCITX_ASSERT(aux.matchPhrase("时", "oc"));
    FCITX_ASSERT(!aux.matchPhrase("时", "m"));
    FCITX_ASSERT(!aux.matchPhrase("时", "ocd"));

    // boundary
    FCITX_ASSERT(aux.matchPhrase("魔", "g"));
    FCITX_ASSERT(aux.matchPhrase("魔", "gg"));
    FCITX_ASSERT(!aux.matchPhrase("魔", "ggo"));
}

// One character with multiple codes: any code matches
static void testMultipleCodes() {
    std::istringstream iss(R"(厑=ib
厑=ii
)");

    AuxCode aux;
    aux.loadFromStream(iss);
    FCITX_ASSERT(aux.isLoaded());

    // any code matches
    FCITX_ASSERT(aux.matchPhrase("厑", "i"));
    FCITX_ASSERT(aux.matchPhrase("厑", "ib"));
    FCITX_ASSERT(aux.matchPhrase("厑", "ii"));
    FCITX_ASSERT(!aux.matchPhrase("厑", "x"));
}

// Phrase matching: prefix, exact, mismatch, unmatched char, empty input, mixed
// content
static void testMatchPhrase() {
    std::istringstream iss(R"(时=oc
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
    aux.loadFromStream(iss);

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
}

// Loaded table with no valid entries
static void testEmptyContent() {
    std::istringstream iss("# comment\n\n");

    AuxCode aux;
    aux.loadFromStream(iss);
    FCITX_ASSERT(aux.isLoaded());
    // table loaded but no entries -> matching logic runs, characters not found
    FCITX_ASSERT(!aux.matchPhrase("时间", "o"));
    FCITX_ASSERT(aux.matchPhrase("时间", ""));
}

int main() {
    testEmptyTable();
    testSingleCharMatch();
    testMultipleCodes();
    testMatchPhrase();
    testEmptyContent();

    FCITX_INFO() << "All auxcode tests passed!";
    return 0;
}
