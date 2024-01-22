#include "../im/pinyin/customphrase.h"
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

void test_evaluator() {
    CustomPhrase phrase(0, "a");
    auto evaluator = [](std::string_view name) -> std::string {
        if (name == "a") {
            return "xx";
        } else if (name == "b") {
            return "yy";
        }
        return "";
    };

    phrase.mutableValue() = "$a $b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "$a $b");

    phrase.mutableValue() = "#$a $b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx yy")
        << phrase.evaluate(evaluator);

    phrase.mutableValue() = "#$a$b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xxyy");

    phrase.mutableValue() = "#$a*$b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx*yy");

    phrase.mutableValue() = "#$a_$b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "yy");

    phrase.mutableValue() = "#$a$$b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx$b");

    phrase.mutableValue() = "#$a$$";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx$");

    phrase.mutableValue() = "#${a} $b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx yy");

    phrase.mutableValue() = "#${a}${b}";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xxyy");

    phrase.mutableValue() = "#$}${b}";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "$}yy");

    phrase.mutableValue() = "#$ ${b}";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "$ yy");

    phrase.mutableValue() = "#$a$";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx$")
        << phrase.evaluate(evaluator);

    phrase.mutableValue() = "#$a${b";
    FCITX_ASSERT(phrase.evaluate(evaluator) == "xx${b");
}

void test_builtin_evaluator() {
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("year") == "2023");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("year_yy") == "23");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("month") == "7");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("month_mm") == "07");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("day") == "11");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("day_dd") == "11");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("weekday") == "2");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("fullhour") == "23");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("halfhour") == "11")
        << CustomPhrase::builtinEvaluator("halfhour");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("ampm") == "PM");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("minute") == "16");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("second") == "06")
        << CustomPhrase::builtinEvaluator("second");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("year_cn") == "二〇二三");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("year_yy_cn") == "二三");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("month_cn") == "七")
        << CustomPhrase::builtinEvaluator("month_cn");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("day_cn") == "十一");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("weekday_cn") == "二");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("fullhour_cn") == "二十三");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("halfhour_cn") == "十一");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("ampm_cn") == "下午");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("minute_cn") == "十六");
    FCITX_ASSERT(CustomPhrase::builtinEvaluator("second_cn") == "零六");
}

int main() {
    test_basic();
    test_evaluator();
    test_builtin_evaluator();
    return 0;
}
