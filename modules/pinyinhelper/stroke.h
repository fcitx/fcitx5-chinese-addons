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
#ifndef _PINYINHELPER_STROKE_H_
#define _PINYINHELPER_STROKE_H_

#include <cstdint>
#include <libime/core/datrie.h>
#include <string>
#include <utility>

namespace fcitx {

class Stroke {
public:
    Stroke();

    bool load();
    std::vector<std::pair<std::string, std::string>>
    lookup(std::string_view input, int limit);
    std::string prettyString(const std::string &input) const;

private:
    libime::DATrie<int32_t> dict_;
};
} // namespace fcitx

#endif // _PINYINHELPER_STROKE_H_
