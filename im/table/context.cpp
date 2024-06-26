/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "context.h"
#include "ime.h"
#include <cstddef>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/text.h>
#include <libime/core/prediction.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/table/tablebaseddictionary.h>
#include <libime/table/tablecontext.h>
#include <memory>
#include <string>
#include <utility>

namespace fcitx {

TableContext::TableContext(libime::TableBasedDictionary &dict,
                           const TableConfig &config,
                           libime::UserLanguageModel &model)
    : libime::TableContext(dict, model), config_(config),
      prediction_(std::make_unique<libime::Prediction>()) {
    prediction_->setUserLanguageModel(&model);
}

Text TableContext::preeditText(bool hint, bool clientPreedit) const {
    Text text;
    TextFormatFlag format =
        clientPreedit ? TextFormatFlag::Underline : TextFormatFlag::NoFlag;
    if (!*config_.commitAfterSelect) {
        for (size_t i = 0, e = selectedSize(); i < e; i++) {
            auto seg = selectedSegment(i);
            if (std::get<bool>(seg)) {
                text.append(std::get<std::string>(seg), {format});
            } else {
                auto segText = hint ? customHint(std::get<std::string>(seg))
                                    : std::get<std::string>(seg);
                TextFormatFlags flags;
                if (*config_.commitInvalidSegment) {
                    segText = stringutils::concat("(", segText, ")");
                    flags = format;
                } else {
                    flags = {TextFormatFlag::DontCommit, TextFormatFlag::Strike,
                             format};
                }

                text.append(std::move(segText), flags);
            }
        }
    }

    std::string codeText;
    if (*config_.firstCandidateAsPreedit && !candidates().empty()) {
        codeText = candidates().front().toString();
    } else {
        codeText = hint ? customHint(currentCode()) : currentCode();
    }

    text.append(std::move(codeText), {format});

    if (clientPreedit && *config_.preeditCursorPositionAtBeginning) {
        text.setCursor(0);
    } else {
        text.setCursor(text.textLength());
    }
    return text;
}
} // namespace fcitx
