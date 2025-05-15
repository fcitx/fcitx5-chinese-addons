/*
 * SPDX-FileCopyrightText: 2010-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <algorithm>
#include <array>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/unixfd.h>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <istream>
#include <locale>
#include <optional>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__GLIBC__)
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#include <sys/endian.h>
#endif

using namespace fcitx;

// SCEL file format
//
// Common data structure
// 2 byte of bytes length, and data, we will refer this as bytearray.
//
// 12bytes Header
// 0x5C 4byte Num Phrase
// 0x60 4byte Phrase Offset
// 0x74 4byte num del table
// 0x78 4byte del table offset
// 0x120 num entries
// 0x130-0x338 description
// 0x338-0x540 source
// 0x540-0xD40 long description
// 0xD40-0x1540 example
// 0x1540 Pinyin index table, may be empty, but still has pinyin size.
//  4 bytes num pinyin
//  2 byte index
//  bytearray pinyin string
// Regular data
// num entries
// 2 byte num words
// bytearray pinyin index
// [
//   bytearray word
//   bytearray unused data
// ]
// Phase Offset
// [
//    17 bytes of data
//    bytearray of pinyin / string
//    bytearray of word
// ]
// DEL table (uncommon)
// At del table offset, or starts with "DELTBL" in utf16
// num of entry
// [word length in character, word]

namespace {

constexpr std::array header = {
    std::to_array<uint8_t>({0x40, 0x15, 0x00, 0x00, 0x44, 0x43, 0x53, 0x01,
                            0x01, 0x00, 0x00, 0x00}),
    std::to_array<uint8_t>({0x40, 0x15, 0x00, 0x00, 0x45, 0x43, 0x53, 0x01,
                            0x01, 0x00, 0x00, 0x00}),
    std::to_array<uint8_t>({0x40, 0x15, 0x00, 0x00, 0xd2, 0x6d, 0x53, 0x01,
                            0x01, 0x00, 0x00, 0x00})};
constexpr std::array deltbl = std::to_array<uint8_t>(
    {0x44, 0x00, 0x45, 0x00, 0x4c, 0x00, 0x54, 0x00, 0x42, 0x00, 0x4c, 0x00});

constexpr size_t PHRASE_OFFSET = 0x5C;
constexpr size_t DELTBL_OFFSET = 0x74;
constexpr size_t ENTRY_OFFSET = 0x120;
constexpr size_t DESC_OFFSET = 0x130;
constexpr size_t SOURCE_OFFSET = 0x338;
constexpr size_t LONG_DESC_OFFSET = 0x540;
constexpr size_t EXAMPLE_OFFSET = 0xd40;
constexpr size_t PINYIN_OFFSET = 0x1540;

template <typename T>
void readOrAbort(std::istream &in, T *value, int n, const char *error) {
    if (!in.read(reinterpret_cast<char *>(value), n * sizeof(T))) {
        throw std::runtime_error(
            std::format("Read error: {}, current offset: {}", error,
                        std::streamoff(in.tellg())));
    }
}
template <typename T>
void readOrAbort(std::istream &in, T *value, const char *error) {
    readOrAbort(in, value, 1, error);
}

template <typename T>
void readFixedBuffer(std::istream &in, T *value, const char *error) {
    readOrAbort(in, value->data(), value->size(), error);
}

void readUInt16(std::istream &in, uint16_t *value, const char *error) {
    readOrAbort(in, value, error);
    *value = le16toh(*value);
}

void readUInt32(std::istream &in, uint32_t *value, const char *error) {
    readOrAbort(in, value, error);
    *value = le32toh(*value);
}

template <typename T>
    requires(sizeof(typename T::value_type) == 2)
void readByteArray(std::istream &in, T *value, const char *error) {
    uint16_t size;
    readUInt16(in, &size, error);
    if (size % 2 != 0) {
        throw std::runtime_error(
            std::format("Invalid size of byte array {}: {}", size, error));
    }
    for (size_t i = 0; i < size; i += 2) {
        uint16_t data;
        readUInt16(in, &data, error);
        value->push_back(le16toh(data));
    }
}

std::string unicodeToUTF8(const char16_t *value, size_t size) {
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
        .to_bytes(value, value + size);
}

std::string unicodeToUTF8(const uint8_t *value, size_t size) {
    if (size % 2 != 0) {
        throw std::runtime_error(
            std::format("Invalid size of string {}", size));
    }
    const auto *ustr = reinterpret_cast<const uint16_t *>(value);
    std::u16string str;
    str.reserve(size / 2);
    for (size_t i = 0; i < size / 2; i++) {
        // either le or be will be 0
        if (ustr[i] == 0) {
            break;
        }
        str.push_back(le16toh(ustr[i]));
    }
    return unicodeToUTF8(str.data(), str.size());
}

template <typename T>
    requires std::is_same_v<typename T::value_type, uint8_t>
std::string unicodeToUTF8(const T &value) {
    return unicodeToUTF8(value.data(), value.size());
}

void readString(std::istream &in, std::string *out, const char *error) {
    std::u16string ustr;
    readByteArray(in, &ustr, error);
    *out = unicodeToUTF8(ustr.data(), ustr.size());
}

std::string indexPinyin(uint32_t index, const std::vector<std::string> &vec) {
    if (index < vec.size()) {
        return vec[index];
    }

    throw std::runtime_error(std::format("Invalid pinyin index {}", index));
}

// There is a special 482 index equals "#", but we don't support "#" anyway.
// And we only want to guess how 482 is mapped to "#".
constexpr std::array<std::string_view, 449> defaultPinyins = {
    "a",     "ai",     "an",     "ang",   "ao",     "ba",    "bai",   "ban",
    "bang",  "bao",    "bei",    "ben",   "beng",   "bi",    "bian",  "biao",
    "bie",   "bin",    "bing",   "bo",    "bu",     "ca",    "cai",   "can",
    "cang",  "cao",    "ce",     "cen",   "ceng",   "cha",   "chai",  "chan",
    "chang", "chao",   "che",    "chen",  "cheng",  "chi",   "chong", "chou",
    "chu",   "chua",   "chuai",  "chuan", "chuang", "chui",  "chun",  "chuo",
    "ci",    "cong",   "cou",    "cu",    "cuan",   "cui",   "cun",   "cuo",
    "da",    "dai",    "dan",    "dang",  "dao",    "de",    "dei",   "den",
    "deng",  "di",     "dia",    "dian",  "diao",   "die",   "ding",  "diu",
    "dong",  "dou",    "du",     "duan",  "dui",    "dun",   "duo",   "e",
    "ei",    "en",     "eng",    "er",    "fa",     "fan",   "fang",  "fei",
    "fen",   "feng",   "fiao",   "fo",    "fou",    "fu",    "ga",    "gai",
    "gan",   "gang",   "gao",    "ge",    "gei",    "gen",   "geng",  "gong",
    "gou",   "gu",     "gua",    "guai",  "guan",   "guang", "gui",   "gun",
    "guo",   "ha",     "hai",    "han",   "hang",   "hao",   "he",    "hei",
    "hen",   "heng",   "hong",   "hou",   "hu",     "hua",   "huai",  "huan",
    "huang", "hui",    "hun",    "huo",   "ji",     "jia",   "jian",  "jiang",
    "jiao",  "jie",    "jin",    "jing",  "jiong",  "jiu",   "ju",    "juan",
    "jue",   "jun",    "ka",     "kai",   "kan",    "kang",  "kao",   "ke",
    "kei",   "ken",    "keng",   "kong",  "kou",    "ku",    "kua",   "kuai",
    "kuan",  "kuang",  "kui",    "kun",   "kuo",    "la",    "lai",   "lan",
    "lang",  "lao",    "le",     "lei",   "leng",   "li",    "lia",   "lian",
    "liang", "liao",   "lie",    "lin",   "ling",   "liu",   "lo",    "long",
    "lou",   "lu",     "luan",   "lve",   "lun",    "luo",   "lv",    "ma",
    "mai",   "man",    "mang",   "mao",   "me",     "mei",   "men",   "meng",
    "mi",    "mian",   "miao",   "mie",   "min",    "ming",  "miu",   "mo",
    "mou",   "mu",     "na",     "nai",   "nan",    "nang",  "nao",   "ne",
    "nei",   "nen",    "neng",   "ni",    "nian",   "niang", "niao",  "nie",
    "nin",   "ning",   "niu",    "nong",  "nou",    "nu",    "nuan",  "nve",
    "nun",   "nuo",    "nv",     "o",     "ou",     "pa",    "pai",   "pan",
    "pang",  "pao",    "pei",    "pen",   "peng",   "pi",    "pian",  "piao",
    "pie",   "pin",    "ping",   "po",    "pou",    "pu",    "qi",    "qia",
    "qian",  "qiang",  "qiao",   "qie",   "qin",    "qing",  "qiong", "qiu",
    "qu",    "quan",   "que",    "qun",   "ran",    "rang",  "rao",   "re",
    "ren",   "reng",   "ri",     "rong",  "rou",    "ru",    "rua",   "ruan",
    "rui",   "run",    "ruo",    "sa",    "sai",    "san",   "sang",  "sao",
    "se",    "sen",    "seng",   "sha",   "shai",   "shan",  "shang", "shao",
    "she",   "shei",   "shen",   "sheng", "shi",    "shou",  "shu",   "shua",
    "shuai", "shuan",  "shuang", "shui",  "shun",   "shuo",  "si",    "song",
    "sou",   "su",     "suan",   "sui",   "sun",    "suo",   "ta",    "tai",
    "tan",   "tang",   "tao",    "te",    "tei",    "teng",  "ti",    "tian",
    "tiao",  "tie",    "ting",   "tong",  "tou",    "tu",    "tuan",  "tui",
    "tun",   "tuo",    "wa",     "wai",   "wan",    "wang",  "wei",   "wen",
    "weng",  "wo",     "wu",     "xi",    "xia",    "xian",  "xiang", "xiao",
    "xie",   "xin",    "xing",   "xiong", "xiu",    "xu",    "xuan",  "xue",
    "xun",   "ya",     "yan",    "yang",  "yao",    "ye",    "yi",    "yin",
    "ying",  "yo",     "yong",   "you",   "yu",     "yuan",  "yue",   "yun",
    "za",    "zai",    "zan",    "zang",  "zao",    "ze",    "zei",   "zen",
    "zeng",  "zha",    "zhai",   "zhan",  "zhang",  "zhao",  "zhe",   "zhei",
    "zhen",  "zheng",  "zhi",    "zhong", "zhou",   "zhu",   "zhua",  "zhuai",
    "zhuan", "zhuang", "zhui",   "zhun",  "zhuo",   "zi",    "zong",  "zou",
    "zu",    "zuan",   "zui",    "zun",   "zuo",    "A",     "B",     "C",
    "D",     "E",      "F",      "G",     "H",      "I",     "J",     "K",
    "L",     "M",      "N",      "O",     "P",      "Q",     "R",     "S",
    "T",     "U",      "V",      "W",     "X",      "Y",     "Z",     "0",
    "1",     "2",      "3",      "4",     "5",      "6",     "7",     "8",
    "9"};

void usage(std::ostream &out) {
    out << "scel2org - Convert .scel file to libime compatible file (SEE NOTES "
           "BELOW)\n"
           "\n"
           "  usage: scel2org [OPTION] [scel file]\n"
           "\n"
           "  -o <file>  specify the output file, if not specified, the output "
           "will\n"
           "             be stdout.\n"
           "  -t         specify the output to be in format of extra table "
           "dict.\n"
           "  -a         Print non pinyin words.\n"
           "  -h         display this help.\n"
           "\n"
           "NOTES:\n"
           "   Always check the produced output for errors.\n";
}

struct ScelOption {
    bool printAll = false;
    bool printDel = false;
    bool table = false;

    std::ostream &out;

    uint32_t phraseCount = 0;
    uint32_t phraseOffset = 0;
    uint32_t delTblCount = 0;
    uint32_t delTblOffset = 0;
    uint32_t entryCount = 0;
    std::vector<std::string> pinyinIndex;
};

void readMetadata(std::istream &in, ScelOption &options) {
    if (!in.seekg(0, std::ios::beg)) {
        throw std::runtime_error("Failed to seek to begin");
    }
    decltype(header)::value_type headerBuf;
    readFixedBuffer(in, &headerBuf, "Failed to read header");
    if (!std::ranges::any_of(
            header, std::bind_front(std::equal_to<decltype(headerBuf)>(),
                                    std::cref(headerBuf)))) {
        throw std::runtime_error("Invalid header");
    }

    if (!in.seekg(PHRASE_OFFSET, std::ios::beg)) {
        throw std::runtime_error("Failed to seek to phrase offset");
    }
    readUInt32(in, &options.phraseCount, "Failed to read phrase count");
    readUInt32(in, &options.phraseOffset, "Failed to read phrase offset");

    if (!in.seekg(DELTBL_OFFSET, std::ios::beg)) {
        throw std::runtime_error("Failed to seek to deltbl offset");
    }
    readUInt32(in, &options.delTblCount, "Failed to read delete table count");
    readUInt32(in, &options.delTblOffset, "Failed to read delete table offset");

    if (!in.seekg(ENTRY_OFFSET, std::ios::beg)) {
        throw std::runtime_error("Failed to seek to entry offset");
    }
    readUInt32(in, &options.entryCount, "Failed to read entry count");

    if (!in.seekg(DESC_OFFSET, std::ios::beg)) {
        throw std::runtime_error("Failed to seek to description offset");
    }

    std::array<uint8_t, SOURCE_OFFSET - DESC_OFFSET> descBuf;
    readFixedBuffer(in, &descBuf, "Failed to read description");

    std::array<uint8_t, LONG_DESC_OFFSET - SOURCE_OFFSET> exampleBuf;
    readFixedBuffer(in, &exampleBuf, "Failed to read source description");

    std::array<uint8_t, EXAMPLE_OFFSET - LONG_DESC_OFFSET> longDescBuf;
    readFixedBuffer(in, &longDescBuf, "Failed to read long description");

    std::array<uint8_t, PINYIN_OFFSET - EXAMPLE_OFFSET> nextBuf;
    readFixedBuffer(in, &nextBuf, "Failed to read example words");

    std::cerr << "DESC:" << unicodeToUTF8(descBuf) << '\n';
    std::cerr << "SOURCE:" << unicodeToUTF8(exampleBuf) << '\n';
    std::cerr << "LONGDESC:" << unicodeToUTF8(longDescBuf) << '\n';
    std::cerr << "EXAMPLE:" << unicodeToUTF8(nextBuf) << '\n';
}

void readPinyinIndex(std::istream &in, ScelOption &options) {
    uint32_t pyCount;
    readUInt32(in, &pyCount, "Failed to read py count");

    std::vector<std::string> pys;
    for (uint32_t i = 0; i < pyCount; i++) {
        uint16_t index;
        readUInt16(in, &index, "Failed to read index");

        std::string py;
        readString(in, &py, "Failed to read py");

        // Replace ue with ve
        if (py == "lue" || py == "nue") {
            py[py.size() - 2] = 'v';
        }
        pys.push_back(py);
    }
    if (pys.size() == 0) {
        pys.assign(std::begin(defaultPinyins), std::end(defaultPinyins));
    }
    options.pinyinIndex = std::move(pys);
}

void readEntries(std::istream &in, const ScelOption &options) {
    if (options.table) {
        options.out << "[Phrase]\n";
    }

    for (uint32_t ec = 0; ec < options.entryCount; ec++) {
        uint16_t symCount;
        readUInt16(in, &symCount, "Failed to read sym count");

        std::vector<uint16_t> pyindex;
        readByteArray(in, &pyindex, "Failed to read pyindex");

        for (uint16_t s = 0; s < symCount; s++) {
            std::string bufout;
            readString(in, &bufout, "Failed to read text");

            if (!pyindex.empty()) {
                if (options.table) {
                    options.out << bufout << '\n';
                } else {
                    std::string pinyin;
                    try {
                        pinyin = stringutils::join(
                            pyindex | std::views::transform(
                                          [&options](uint16_t index) {
                                              return indexPinyin(
                                                  index, options.pinyinIndex);
                                          }),
                            "\'");
                    } catch (const std::exception &e) {
                        FCITX_ERROR()
                            << "Failed to convert pinyin: " << e.what()
                            << ", word: " << bufout;
                        continue;
                    }
                    options.out << bufout << "\t" << pinyin << "\t0\n";
                }
            }

            std::vector<uint16_t> buffer;
            readByteArray(in, &buffer, "failed to read buf");
        }
    }
}

void readPhrases(std::istream &in, const ScelOption &options) {
    if (options.phraseCount > 0) {
        if (!in.seekg(options.phraseOffset, std::ios::beg)) {
            throw std::runtime_error(std::format("Failed to seek to phrase"));
        }
    }
    for (uint32_t i = 0; i < options.phraseCount; i++) {
        char info[17];
        readOrAbort(in, info, 17, "Failed to read buf");

        std::string code;
        if (info[2] == 0x1) {
            std::vector<uint16_t> pyindex;
            readByteArray(in, &pyindex, "Failed to read pyindex");
            std::string bufout;
            readString(in, &bufout, "Failed to read text");
            if (!pyindex.empty()) {
                try {
                    code = stringutils::join(
                        pyindex |
                            std::views::transform([&options](uint16_t index) {
                                return indexPinyin(index, options.pinyinIndex);
                            }),
                        "\'");
                } catch (const std::exception &e) {
                    FCITX_ERROR() << "Failed to convert pinyin: " << e.what()
                                  << ", word: " << bufout;
                    continue;
                }

                if (options.table) {
                    options.out << bufout << '\n';
                } else {
                    options.out << bufout << "\t" << code << "\t0\n";
                }
            }
        } else {
            readString(in, &code, "Failed to read code");
            std::string bufout;
            readString(in, &bufout, "Failed to read text");

            if (options.printAll) {
                if (options.table) {
                    options.out << bufout << '\n';
                } else {
                    options.out << bufout << "\t" << code << "\t0\n";
                }
            }
        }
    }
}

void readDelTable(std::istream &in, const ScelOption &options) {
    if (!options.printDel) {
        return;
    }
    uint32_t delTblCount;
    if (options.delTblCount > 0) {
        if (!in.seekg(options.delTblOffset, std::ios::beg)) {
            throw std::runtime_error(
                std::format("Failed to seek to deltbl offset"));
        }
        delTblCount = options.delTblCount;
    } else {
        std::remove_const_t<decltype(deltbl)> delTblBuf{};
        in.read(reinterpret_cast<char *>(delTblBuf.data()), delTblBuf.size());
        if (!in || delTblBuf != deltbl) {
            return;
        }
        uint16_t delTblCount16;
        readUInt16(in, &delTblCount16, "Failed to read deltbl count");
        delTblCount = delTblCount16;
    }
    for (uint32_t i = 0; i < delTblCount; i++) {
        uint16_t count;
        readUInt16(in, &count, "Failed to read deltbl word count");
        count *= 2;
        std::vector<uint8_t> buf;
        buf.resize(count);
        readFixedBuffer(in, &buf, "Failed to read deltbl word");
        std::string bufout = unicodeToUTF8(buf);
        std::cerr << "DEL:" << bufout << "\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    int c;
    std::optional<std::string_view> outputFile;
    bool printDel = false;
    bool table = false;
    bool printAll = false;

    while ((c = getopt(argc, argv, "o:hdta")) != -1) {
        switch (c) {
        case 'o':
            outputFile = optarg;
            break;
        case 'd':
            printDel = true;
            break;
        case 't':
            table = true;
            break;
        case 'a':
            printAll = true;
            break;
        case 'h':
            usage(std::cout);
            return 0;
        default:
            usage(std::cerr);
            return 1;
        }
    }

    std::ofstream fout;
    std::ostream *pout;
    if (!outputFile.has_value() || outputFile == "-") {
        pout = &std::cout;
    } else {
        fout.open(std::string(*outputFile), std::ios::out | std::ios::binary);
        pout = &fout;
    }

    if (optind >= argc) {
        usage(std::cerr);
        return 1;
    }

    UnixFD fd = UnixFD::own(open(argv[optind], O_RDONLY));
    if (!fd.isValid()) {
        FCITX_ERROR() << "Cannot open file: " << argv[optind];
        return 1;
    }

    IFDStreamBuf fdStreamBuf(std::move(fd));
    std::istream in(&fdStreamBuf);

    ScelOption options{
        .printAll = printAll,
        .printDel = printDel,
        .table = table,
        .out = *pout,
        .pinyinIndex = {},
    };

    try {
        readMetadata(in, options);
        readPinyinIndex(in, options);
        readEntries(in, options);
        readPhrases(in, options);
        readDelTable(in, options);
    } catch (const std::exception &e) {
        FCITX_ERROR() << e.what();
        return 1;
    }

    return 0;
}
