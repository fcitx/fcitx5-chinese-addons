/*
 * SPDX-FileCopyrightText: 2023-2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_CUSTOMPHRASE_H_
#define _PINYIN_CUSTOMPHRASE_H_

#include <cstddef>
#include <cstdint>
#include <fcitx-utils/macros.h>
#include <functional>
#include <istream>
#include <libime/core/datrie.h>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx {

class CustomPhrase {
public:
    explicit CustomPhrase(int order, std::string value)
        : order_(order), value_(std::move(value)) {}
    FCITX_INLINE_DEFINE_DEFAULT_DTOR_COPY_AND_MOVE_WITH_SPEC(CustomPhrase,
                                                             noexcept)

    int order() const { return order_; }
    const std::string &value() const { return value_; }
    void setOrder(int order) { order_ = order; }
    std::string &mutableValue() { return value_; }

    bool isDynamic() const;

    std::string evaluate(const std::function<std::string(std::string_view key)>
                             &evaluator) const;

    static std::string builtinEvaluator(std::string_view key);

private:
    int order_ = -1;
    std::string value_;
};

class CustomPhraseDict {
public:
    using TrieType = libime::DATrie<uint32_t>;
    CustomPhraseDict();

    void load(std::istream &in, bool loadDisabled = false);
    void save(std::ostream &out) const;
    void clear();

    const std::vector<CustomPhrase> *lookup(std::string_view key) const;

    void addPhrase(std::string_view key, std::string_view value, int order);
    void pinPhrase(std::string_view key, std::string_view value);
    void removePhrase(std::string_view key, std::string_view value);

    template <typename T>
    void foreach(const T &callback) {
        std::string buf;
        index_.foreach([this, &buf, &callback](uint32_t index, size_t len,
                                               TrieType::position_type pos) {
            index_.suffix(buf, len, pos);
            callback(buf, data_[index]);
            return true;
        });
    }

private:
    std::vector<CustomPhrase> *getOrCreateEntry(std::string_view key);
    TrieType index_;
    std::vector<std::vector<CustomPhrase>> data_;
};

} // namespace fcitx

#endif // _PINYIN_SYMBOLDICTIONARY_H_
