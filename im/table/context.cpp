/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "context.h"

namespace fcitx {

TableContext::TableContext(libime::TableBasedDictionary &dict,
                           const TableConfig &config,
                           libime::UserLanguageModel &model)
    : libime::TableContext(dict, model), config_(config) {}

Text TableContext::preeditText(bool hint, bool clientPreedit) const {
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
                if (*config_.commitInvalidSegment) {
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

    std::string codeText;
    if (*config_.firstCandidateAsPreedit && !candidates().empty()) {
        codeText = candidates().front().toString();
    } else {
        codeText = hint ? customHint(currentCode()) : currentCode();
    }

    text.append(codeText, {TextFormatFlag::Underline});

    if (clientPreedit) {
        text.setCursor(0);
    } else {
        text.setCursor(text.textLength());
    }
    return text;
}
} // namespace fcitx
