/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CHTTRANS_CHTTRANS_NATIVE_H_
#define _CHTTRANS_CHTTRANS_NATIVE_H_

#include "chttrans.h"
#include <unordered_map>

class NativeBackend : public ChttransBackend {
public:
    std::string convertSimpToTrad(const std::string &) override;
    std::string convertTradToSimp(const std::string &) override;

protected:
    bool loadOnce(const ChttransConfig &) override;

private:
    std::unordered_map<uint32_t, std::string> s2tMap_;
    std::unordered_map<uint32_t, std::string> t2sMap_;
};

#endif // _CHTTRANS_CHTTRANS_NATIVE_H_
