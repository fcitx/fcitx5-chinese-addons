//
// Copyright (C) 2017~2017 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//
#ifndef _CHTTRANS_CHTTRANS_NATIVE_H_
#define _CHTTRANS_CHTTRANS_NATIVE_H_

#include "chttrans.h"
#include <unordered_map>

class NativeBackend : public ChttransBackend {
public:
    std::string convertSimpToTrad(const std::string &) override;
    std::string convertTradToSimp(const std::string &) override;

protected:
    bool loadOnce() override;

private:
    std::unordered_map<uint32_t, std::string> s2tMap_;
    std::unordered_map<uint32_t, std::string> t2sMap_;
};

#endif // _CHTTRANS_CHTTRANS_NATIVE_H_
