/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _TABLE_CONTEXT_H_
#define _TABLE_CONTEXT_H_

#include "ime.h"
#include <fcitx/text.h>
#include <libime/table/tablecontext.h>

namespace fcitx {

class TableContext : public libime::TableContext {
public:
    TableContext(libime::TableBasedDictionary &dict, const TableConfig &config,
                 libime::UserLanguageModel &model);

    const TableConfig &config() { return config_; }
    std::string customHint(const std::string &code) const {
        if (*config_.displayCustomHint) {
            return dict().hint(code);
        }
        return code;
    }

    Text preeditText(bool hint, bool clientPreedit) const;

private:
    const TableConfig &config_;
};
} // namespace fcitx

#endif // _TABLE_CONTEXT_H_
