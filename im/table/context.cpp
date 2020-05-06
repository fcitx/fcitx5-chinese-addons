//
// Copyright (C) 2017~2017 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2 of the
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
#include "context.h"

namespace fcitx {

TableContext::TableContext(libime::TableBasedDictionary &dict,
                           const TableConfig &config,
                           libime::UserLanguageModel &model)
    : libime::TableContext(dict, model), config_(config) {}

Text TableContext::preeditText(bool hint) const {
    Text text;
    if (!*config_.commitAfterSelect) {
        for (size_t i = 0, e = selectedSize(); i < e; i++) {
            auto seg = selectedSegment(i);
            if (std::get<bool>(seg)) {
                text.append(std::get<std::string>(seg),
                            {TextFormatFlag::Underline});
            } else {
                auto segText = hint ? customHint(std::get<std::string>(seg))
                                    : std::get<std::string>(seg);
                TextFormatFlags flags;
                if (!*config_.commitInvalidSegment) {
                    segText = stringutils::concat("(", segText, ")");
                    flags = TextFormatFlag::Underline;
                } else {
                    flags = {TextFormatFlag::DontCommit, TextFormatFlag::Strike,
                             TextFormatFlag::Underline};
                }

                text.append(segText, flags);
            }
        }
    }
    auto codeText = hint ? customHint(currentCode()) : currentCode();
    text.setCursor(text.textLength());
    text.append(codeText,
                {TextFormatFlag::Underline, TextFormatFlag::HighLight});

    return text;
}
} // namespace fcitx
