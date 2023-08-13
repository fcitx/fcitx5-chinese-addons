/*
 * SPDX-FileCopyrightText: 2023-2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <fcitx-utils/macros.h>
#include <libime/core/datrie.h>
#include <string>
#include <string_view>
#include <vector>

namespace fcitx {

class CustomPhrase {
public:
    explicit CustomPhrase(int order, std::string value)
        : order_(order), value_(value) {}
    FCITX_INLINE_DEFINE_DEFAULT_DTOR_COPY_AND_MOVE_WITH_SPEC(CustomPhrase,
                                                             noexcept)

    int order() const { return order_; }
    const std::string &value() const { return value_; }
    void setOrder(int order) { order_ = order; }
    std::string &mutableValue() { return value_; }

    std::string evaluate(const std::function<std::string(std::string_view key)>
                             &evaluator) const;

    static std::string builtinEvaluator(std::string_view key);

private:
    int order_ = -1;
    std::string value_;
};

class CustomPhraseDict {
public:
    CustomPhraseDict();

    void load(std::istream &in, bool loadDisabled = false);
    void save(std::ostream &out) const;
    void clear();

    const std::vector<CustomPhrase> *lookup(std::string_view key) const;

    void addPhrase(std::string_view key, std::string_view value, int order);

    template <typename T>
    void foreach(const T &callback) {
        std::string buf;
        index_.foreach([this, &buf, &callback](
                           uint32_t index, size_t len,
                           libime::DATrie<uint32_t>::position_type pos) {
            index_.suffix(buf, len, pos);
            callback(buf, data_[index]);
            return true;
        });
    }

private:
    libime::DATrie<uint32_t> index_;
    std::vector<std::vector<CustomPhrase>> data_;
};

} // namespace fcitx
