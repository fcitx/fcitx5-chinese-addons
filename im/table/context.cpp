/*
* Copyright (C) 2017~2017 by CSSlayer
* wengxt@gmail.com
*
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING. If not,
* see <http://www.gnu.org/licenses/>.
*/
#include "context.h"

namespace fcitx {

TableContext::TableContext(libime::TableBasedDictionary &dict,
                           const TableConfig &config,
                           libime::UserLanguageModel &model)
    : libime::TableContext(dict, model), config_(config) {}

Text TableContext::preeditText() const {
    Text text;
    for (size_t i = 0, e = selectedSize(); i < e; i++) {
        auto seg = selectedSegment(i);
        if (std::get<bool>(seg)) {
            text.append(std::get<std::string>(seg),
                        {TextFormatFlag::Underline, TextFormatFlag::HighLight});
        } else {
            text.append("(" + std::get<std::string>(seg) + ")",
                        {TextFormatFlag::DontCommit, TextFormatFlag::Strike,
                         TextFormatFlag::Underline, TextFormatFlag::HighLight});
        }
    }
    text.append(currentCode(), {TextFormatFlag::Underline});

    text.setCursor(text.textLength());
    return text;
}
}
