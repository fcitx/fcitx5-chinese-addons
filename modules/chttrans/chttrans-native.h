/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CHTTRANS_CHTTRANS_NATIVE_H_
#define _CHTTRANS_CHTTRANS_NATIVE_H_

#include "chttrans.h"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

class NativeBackend : public ChttransBackend {
    struct string_hash {
        using is_transparent = void;

        size_t operator()(const std::string_view str) const {
            constexpr std::hash<std::string_view> hasher{};
            return hasher(str);
        }
    };

public:
    using MapType = std::unordered_map<std::string, std::string, string_hash,
                                       std::equal_to<>>;
    std::string convertSimpToTrad(const std::string &) override;
    std::string convertTradToSimp(const std::string &) override;

protected:
    bool loadOnce(const ChttransConfig &) override;

private:
    MapType s2tMap_;
    MapType t2sMap_;
};

#endif // _CHTTRANS_CHTTRANS_NATIVE_H_
