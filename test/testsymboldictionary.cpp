#include "../im/pinyin/symboldictionary.h"
#include <fcitx-utils/log.h>
#include <fstream>
#include <sstream>
#include <string_view>

using namespace fcitx;

constexpr std::string_view testInput = R"TEST(
AAA BBB
"CDEF G" H
X "Y Z"
"Y""12"
"Y" "34"
"Y" "56"
)TEST";

void test_basic() {
    std::stringstream ss;

    ss << testInput;

    SymbolDict dict;
    dict.load(ss);
    auto *result = dict.lookup("P");
    FCITX_ASSERT(!result);

    result = dict.lookup("AAA");
    FCITX_ASSERT(*result == std::vector<std::string>{"BBB"});

    result = dict.lookup("CDEF G");
    FCITX_ASSERT(*result == std::vector<std::string>{"H"});

    result = dict.lookup("Y");
    FCITX_ASSERT(*result == std::vector<std::string>{"34", "56"});

    result = dict.lookup("X");
    FCITX_ASSERT(*result == std::vector<std::string>{"Y Z"});
}

int main() {
    test_basic();
    return 0;
}
