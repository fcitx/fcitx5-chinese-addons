/*
 * SPDX-FileCopyrightText: 2010-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <codecvt>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
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
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/unixfd.h>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <locale>
#include <unistd.h>

using namespace fcitx;

#define HEADER_SIZE 12
#define DELTBL_SIZE 8
#define BUFLEN 0x1000

#define PHRASE_OFFSET 0x5C
#define ENTRY_OFFSET 0x120
#define DESC_START 0x130
#define DESC_LENGTH (0x338 - 0x130)

#define LDESC_LENGTH (0x540 - 0x338)
#define NEXT_LENGTH (0x1540 - 0x540)

template <typename T>
void readOrAbort(const UnixFD &fd, T *value, int n,
                 const char *error = nullptr) {
    if (fs::safeRead(fd.fd(), value, n * sizeof(T)) !=
        static_cast<int>(n * sizeof(T))) {
        if (error) {
            FCITX_FATAL() << error;
        } else {
            exit(0);
        }
    }
}

template <typename T>
void readOrAbort(const UnixFD &fd, T *value, const char *error = nullptr) {
    return readOrAbort(fd, value, 1, error);
}

void readUInt16(const UnixFD &fd, uint16_t *value,
                const char *error = nullptr) {
    readOrAbort(fd, value, error);
    *value = le16toh(*value);
}

void readUInt32(const UnixFD &fd, uint32_t *value,
                const char *error = nullptr) {
    readOrAbort(fd, value, error);
    *value = le32toh(*value);
}

std::string unicodeToUTF8(const char16_t *value, size_t size) {
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
        .to_bytes(value, value + size);
}

std::string unicodeToUTF8(const char *value, size_t size) {
    FCITX_ASSERT(size % 2 == 0) << "Invalid size of string";
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

std::string indexPinyin(uint32_t index, std::vector<std::string> vec) {
    if (index < vec.size())
        return vec[index];

    if (index - vec.size() == 43)
        return "#";

    if (index - vec.size() >= 10)
        FCITX_WARN() << "Invalid index: " << index;

    return std::to_string(index - vec.size());
}

static const char header_str[4] = {'\x40', '\x15', '\0', '\0'};
static const char magic_str1[4] = {'\x44', '\x43', '\x53', '\x01'};
static const char magic_str2[4] = {'\x45', '\x43', '\x53', '\x01'};
static const char magic_str3[4] = {'\xd2', '\x6d', '\x53', '\x01'};
static const char version_str[4] = {'\x01', '\0', '\0', '\0'};
static const char deltbl_str[DELTBL_SIZE] = {'\x4c', '\0', '\x54', '\0',
                                             '\x42', '\0', '\x4c', '\0'};
static const std::vector<std::string> default_pys = {
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
    "T",     "U",      "V",      "W",     "X",      "Y",     "Z",
};

static void usage() {
    puts(
        "scel2org - Convert .scel file to libime compatible file (SEE NOTES "
        "BELOW)\n"
        "\n"
        "  usage: scel2org [OPTION] [scel file]\n"
        "\n"
        "  -o <file>  specify the output file, if not specified, the output "
        "will\n"
        "             be stdout.\n"
        "  -t         specify the output to be in format of extra table dict.\n"
        "  -h         display this help.\n"
        "\n"
        "NOTES:\n"
        "   Always check the produced output for errors.\n");
    exit(1);
}

int main(int argc, char **argv) {
    int c;
    const char *outputFile = nullptr;
    bool printDel = false;
    bool table = false;

    while ((c = getopt(argc, argv, "o:hdt")) != -1) {
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
        case 'h':
        default:
            usage();
            break;
        }
    }

    std::ofstream fout;
    std::ostream *out;
    if (!outputFile || strcmp(outputFile, "-") == 0) {
        out = &std::cout;
    } else {
        fout.open(outputFile, std::ios::out | std::ios::binary);
        out = &fout;
    }

    if (optind >= argc) {
        usage();
        return 1;
    }

    UnixFD fd = UnixFD::own(open(argv[optind], O_RDONLY));
    if (!fd.isValid()) {
        FCITX_ERROR() << "Cannot open file: " << argv[optind];
        return 1;
    }

    char headerBuf[HEADER_SIZE];
    readOrAbort(fd, headerBuf, HEADER_SIZE, "Failed to read header");
    FCITX_ASSERT((memcmp(headerBuf, header_str, 4) == 0) &&
                 ((memcmp(headerBuf + 4, magic_str1, 4) == 0) ||
                  (memcmp(headerBuf + 4, magic_str2, 4) == 0) ||
                  (memcmp(headerBuf + 4, magic_str3, 4) == 0)) &&
                 (memcmp(headerBuf + 8, version_str, 4) == 0))
        << " format error.";

    FCITX_ASSERT(lseek(fd.fd(), PHRASE_OFFSET, SEEK_SET) !=
                 static_cast<off_t>(-1));
    uint32_t phraseCount;
    readUInt32(fd, &phraseCount, "Failed to read phrase count");
    uint32_t phraseOffset;
    readUInt32(fd, &phraseOffset, "Failed to read phrase offset");

    // skip 8 bytes
    FCITX_ASSERT(lseek(fd.fd(), 8, SEEK_CUR) != static_cast<off_t>(-1));
    uint32_t delTblOffset;
    readUInt32(fd, &delTblOffset, "Failed to read delete table offset");
    // skip 4 bytes
    FCITX_ASSERT(lseek(fd.fd(), 4, SEEK_CUR) != static_cast<off_t>(-1));
    uint32_t delTblCount;
    readUInt32(fd, &delTblCount, "Failed to read delete table count");

    FCITX_ASSERT(lseek(fd.fd(), ENTRY_OFFSET, SEEK_SET) !=
                 static_cast<off_t>(-1));
    uint32_t entryCount;
    readUInt32(fd, &entryCount, "Failed to read entry count");

    FCITX_ASSERT(lseek(fd.fd(), DESC_START, SEEK_SET) !=
                 static_cast<off_t>(-1));

    char descBuf[DESC_LENGTH];
    readOrAbort(fd, descBuf, DESC_LENGTH, "Failed to read description");
    std::cerr << "DESC:" << unicodeToUTF8(descBuf, DESC_LENGTH) << std::endl;

    char ldescBuf[LDESC_LENGTH];
    readOrAbort(fd, ldescBuf, LDESC_LENGTH, "Failed to read long description");
    std::cerr << "LDESC:" << unicodeToUTF8(ldescBuf, LDESC_LENGTH) << std::endl;

    char nextBuf[NEXT_LENGTH];
    readOrAbort(fd, nextBuf, NEXT_LENGTH, "Failed to read next description");
    std::cerr << "NEXT:" << unicodeToUTF8(nextBuf, NEXT_LENGTH) << std::endl;

    uint32_t pyCount;
    readUInt32(fd, &pyCount, "Failed to read py count");

    std::vector<std::string> pys;
    for (uint32_t i = 0; i < pyCount; i++) {
        uint16_t index;
        uint16_t count;
        readUInt16(fd, &index, "failed to read index");
        readUInt16(fd, &count, "failed to read pinyin count");

        std::vector<char> buf;
        buf.resize(count);

        readOrAbort(fd, buf.data(), count, "Failed to read py");

        std::string py = unicodeToUTF8(buf.data(), buf.size());

        // Replace ue with ve
        if (py == "lue" || py == "nue") {
            py[py.size() - 2] = 'v';
        }
        pys.push_back(py);
    }

    if (table) {
        *out << "[Phrase]" << std::endl;
    } else if (pys.size() == 0) {
        pys = default_pys;
    }

    if (pys.size() < default_pys.size()) {
        for (uint32_t i = 0; i < 26; i++) {
            pys.push_back(std::string(1, char(i + 65)));
        }
    }

    for (uint32_t ec = 0; ec < entryCount; ec++) {
        uint16_t symCount;
        uint16_t count;
        uint16_t wordCount;

        readUInt16(fd, &symCount);
        readUInt16(fd, &count, "Failed to read count");

        wordCount = count / 2;
        std::vector<uint16_t> pyindex;
        pyindex.resize(wordCount);

        for (uint16_t i = 0; i < wordCount; i++) {
            readUInt16(fd, &pyindex[i], "Failed to read pyindex");
            if (pyindex[i] >= pys.size()) {
                FCITX_WARN() << "Invalid pinyin index: " << pyindex[i]
                             << " at offset: " << lseek(fd.fd(), 0, SEEK_CUR);
            }
        }

        for (uint16_t s = 0; s < symCount; s++) {
            std::vector<char> buf;
            readUInt16(fd, &count, "Failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), count, "Failed to read text");
            std::string bufout = unicodeToUTF8(buf.data(), buf.size());

            if (wordCount > 0) {
                if (table) {
                    *out << bufout << std::endl;
                } else {
                    *out << bufout << "\t";
                    *out << indexPinyin(pyindex[0], pys);
                    for (auto i = 1; i < wordCount; i++) {
                        *out << '\'';
                        *out << indexPinyin(pyindex[i], pys);
                    }
                    *out << "\t0" << std::endl;
                }
            }

            readUInt16(fd, &count, "failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), buf.size(), "failed to read buf");
        }
    }

    if (phraseCount > 0) {
        FCITX_ASSERT(lseek(fd.fd(), phraseOffset, SEEK_SET) !=
                     static_cast<off_t>(-1));
    }
    for (uint32_t i = 0; i < phraseCount; i++) {
        char info[17];
        readOrAbort(fd, info, 17, "Failed to read buf");

        uint16_t count;
        readUInt16(fd, &count, "Failed to read count");

        std::string code;
        if (info[2] == 0x1) {
            std::vector<uint16_t> pyindex;
            count /= 2;
            pyindex.resize(count);
            for (auto &index : pyindex) {
                readUInt16(fd, &index, "Failed to read pyindex");
            }
            if (count > 0) {
                code = indexPinyin(pyindex[0], pys);
                for (auto i = 1; i < count; i++) {
                    code += '\'';
                    code += indexPinyin(pyindex[i], pys);
                }
            }
        } else {
            std::vector<char> buf;
            buf.resize(count);
            readOrAbort(fd, buf.data(), count, "Failed to read buf");
            code = unicodeToUTF8(buf.data(), buf.size());
        }

        std::vector<char> buf;
        readUInt16(fd, &count, "Failed to read count");
        buf.resize(count);
        readOrAbort(fd, buf.data(), count, "Failed to read buf");
        std::string bufout = unicodeToUTF8(buf.data(), buf.size());

        if (table) {
            *out << bufout << std::endl;
        } else {
            *out << bufout << "\t" << code << "\t0" << std::endl;
        }
    }

    if (!printDel) {
        return 0;
    }
    if (delTblCount > 0) {
        FCITX_ASSERT(lseek(fd.fd(), delTblOffset, SEEK_SET) !=
                     static_cast<off_t>(-1));
    } else {
        char delTblBuf[DELTBL_SIZE];
        if (fs::safeRead(fd.fd(), delTblBuf, DELTBL_SIZE) != DELTBL_SIZE ||
            memcmp(delTblBuf, deltbl_str, DELTBL_SIZE) != 0) {
            return 0;
        }
        uint16_t _delTblCount;
        readUInt16(fd, &_delTblCount);
        delTblCount = _delTblCount;
    }
    for (uint32_t i = 0; i < delTblCount; i++) {
        uint16_t count;
        readUInt16(fd, &count);
        count *= 2;
        std::vector<char> buf;
        buf.resize(count);
        readOrAbort(fd, buf.data(), count, "Failed to read text");
        std::string bufout = unicodeToUTF8(buf.data(), buf.size());
        std::cerr << "DEL:" << bufout << std::endl;
    }

    return 0;
}
