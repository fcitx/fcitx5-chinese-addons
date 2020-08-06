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
#define BUFLEN 0x1000

#define DESC_START 0x130
#define DESC_LENGTH (0x338 - 0x130)

#define LDESC_LENGTH (0x540 - 0x338)
#define NEXT_LENGTH (0x1540 - 0x540)

#define PINYIN_SIZE 4

template <typename T>
void readOrAbort(const UnixFD &fd, T *value, int n, const char *error) {
    if (fs::safeRead(fd.fd(), value, n * sizeof(T)) !=
        static_cast<int>(n * sizeof(T))) {
        FCITX_FATAL() << error;
    }
}

template <typename T>
void readOrAbort(const UnixFD &fd, T *value, const char *error) {
    return readOrAbort(fd, value, 1, error);
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

    while ((c = getopt(argc, argv, "o:h")) != -1) {
        switch (c) {
        case 'o':
            outputFile = optarg;
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
        int16_t index;
        int16_t count;
        readOrAbort(fd, &index, "failed to read index");
        readOrAbort(fd, &count, "failed to read pinyin count");

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
        int16_t symcount;
        int16_t count;
        int16_t wordcount;
        auto readResult = fs::safeRead(fd.fd(), &symcount, sizeof(int16_t));
        if (readResult == 0) {
            break;
        }

        if (readResult < 0) {
            FCITX_FATAL() << "Failed to read result";
        }

        readOrAbort(fd, &count, "Failed to read count");

        wordcount = count / 2;
        std::vector<int16_t> pyindex;
        pyindex.resize(wordcount);

        readOrAbort(fd, pyindex.data(), wordcount, "Failed to read pyindex");

        int s;

        for (s = 0; s < symcount; s++) {
            std::vector<char> buf;
            readOrAbort(fd, &count, "Failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), count, "Failed to read text");
            std::string bufout = unicodeToUTF8(buf.data(), buf.size());

            *out << bufout << "\t";
            *out << pys[pyindex[0]];
            for (auto i = 1; i < wordcount; i++) {
                *out << '\'' << pys[pyindex[i]];
            }

            *out << "\t0" << std::endl;

            readOrAbort(fd, &count, "failed to read count");
            buf.resize(count);
            readOrAbort(fd, buf.data(), buf.size(), "failed to read buf");
        }
    }

    return 0;
}
