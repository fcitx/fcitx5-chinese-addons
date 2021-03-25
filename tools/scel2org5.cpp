/*
 * SPDX-FileCopyrightText: 2010-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <codecvt>
#include <cstring>
#if defined(__linux__) || defined(__GLIBC__)
#include <endian.h>
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
#define DELTBL_SIZE 10
#define BUFLEN 0x1000

#define DESC_START 0x130
#define DESC_LENGTH (0x338 - 0x130)

#define LDESC_LENGTH (0x540 - 0x338)
#define NEXT_LENGTH (0x1540 - 0x540)

#define PINYIN_SIZE 4

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

static const char header_str[HEADER_SIZE] = {'\x40', '\x15', '\0',   '\0',
                                             '\x44', '\x43', '\x53', '\x01',
                                             '\x01', '\0',   '\0',   '\0'};
static const char pinyin_str[PINYIN_SIZE] = {'\x9d', '\x01', '\0', '\0'};
static const char deltbl_str[HEADER_SIZE] = {
    '\x45', '\0', '\x4c', '\0', '\x54', '\0', '\x42', '\0', '\x4c', '\0'};

static void usage() {
    puts("scel2org - Convert .scel file to libime compatible file (SEE NOTES "
         "BELOW)\n"
         "\n"
         "  usage: scel2org [OPTION] [scel file]\n"
         "\n"
         "  -o <file>  specify the output file, if not specified, the output "
         "will\n"
         "             be stdout.\n"
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

    while ((c = getopt(argc, argv, "o:hd")) != -1) {
        switch (c) {
        case 'o':
            outputFile = optarg;
            break;
        case 'd':
            printDel = true;
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
    FCITX_ASSERT(memcmp(headerBuf, header_str, HEADER_SIZE) == 0)
        << " format error.";

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

    char pyBuf[PINYIN_SIZE];
    readOrAbort(fd, pyBuf, PINYIN_SIZE, "Failed to read py");
    FCITX_ASSERT(memcmp(pyBuf, pinyin_str, PINYIN_SIZE) == 0);

    std::vector<std::string> pys;

    while (true) {
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

        if (py == "zuo") {
            break;
        }
    }

    while (true) {
        uint16_t symcount;
        uint16_t count;
        uint16_t wordcount;
        readUInt16(fd, &symcount);

        // Just in case we read a invalid value.
        if (symcount > 128) {
            FCITX_ERROR() << "Error at offset: " << lseek(fd.fd(), 0, SEEK_CUR);
            break;
        }

        readUInt16(fd, &count, "Failed to read count");

        wordcount = count / 2;
        std::vector<uint16_t> pyindex;
        pyindex.resize(wordcount);

        for (uint16_t i = 0; i < wordcount; i++) {
            readUInt16(fd, &pyindex[i], "Failed to read pyindex");
            if (pyindex[i] >= pys.size()) {
                FCITX_FATAL() << "Invalid pinyin index";
            }
        }

        for (uint16_t s = 0; s < symcount; s++) {
            std::vector<char> buf;
            readUInt16(fd, &count, "Failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), count, "Failed to read text");
            std::string bufout = unicodeToUTF8(buf.data(), buf.size());

            *out << bufout << "\t";
            *out << pys[pyindex[0]];
            for (auto i = 1; i < wordcount; i++) {
                *out << '\'' << pys[pyindex[i]];
            }

            *out << "\t0" << std::endl;

            readUInt16(fd, &count, "failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), buf.size(), "failed to read buf");
        }
    }

    char delTblBuf[DELTBL_SIZE];
    if (fs::safeRead(fd.fd(), delTblBuf, DELTBL_SIZE) != DELTBL_SIZE ||
        memcmp(delTblBuf, deltbl_str, DELTBL_SIZE) != 0) {
        return 0;
    }

    if (!printDel) {
        return 0;
    }

    uint16_t delTblCount;
    readUInt16(fd, &delTblCount);
    for (int i = 0; i < delTblCount; i++) {
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
