#include "customphrase.h"
#include <fcitx-utils/log.h>
#include <sstream>
#include <string_view>

using namespace fcitx;

constexpr std::string_view testInput = R"TEST(
; semicolon style comment
# hash style comment
  ;random,1=a
a,1=ABC
bcd,-1=EFG
zzz,1=
LINE1
LINE2
; line3
mmm,=sdf
mmm,adf=df
mmm,4="a\nb"
)TEST";

void test_basic() {
    std::stringstream ss;

    ss << testInput;

    CustomPhraseDict dict;
    dict.load(ss);
    auto result = dict.lookup("mmm");
    FCITX_ASSERT(result);
    FCITX_ASSERT(result->size() == 1);

    std::stringstream sout;
    dict.save(sout);
    std::cout << sout.str() << std::endl;
}

int main() { test_basic(); }
