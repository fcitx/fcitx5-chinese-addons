#include "customphrase.h"
#include <fcitx-utils/log.h>
#include <fstream>
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
    auto *result = dict.lookup("mmm");
    FCITX_ASSERT(result);
    FCITX_ASSERT(result->size() == 1);

    std::stringstream stream;
    dict.save(stream);
    std::string output = stream.str();
    std::cout << output << std::endl;

    dict.load(stream);

    std::stringstream sout;
    dict.save(sout);
    std::string output2 = sout.str();
    std::cout << output2 << std::endl;
    FCITX_ASSERT(output == output2);

    result = dict.lookup("a");
    FCITX_ASSERT(result);
    FCITX_ASSERT(result->size() == 1);
    FCITX_ASSERT((*result)[0].order() == 1);
    FCITX_ASSERT((*result)[0].value() == "ABC");
}

int main() {
    test_basic();
    return 0;
}
