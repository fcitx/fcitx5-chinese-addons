/*
* Copyright (C) 2017~2017 by CSSlayer
* wengxt@gmail.com
*
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 2.1 of the
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
#ifndef _TABLE_STATE_H_
#define _TABLE_STATE_H_

#include "context.h"
#include "engine.h"
#include <fcitx/inputcontextproperty.h>

namespace fcitx {

enum class TableMode {
    Normal,
    ModifyDictionary,
    Pinyin,
    LookupPinyin,
};

class TableState : public InputContextProperty {
public:
    TableState(InputContext *ic, TableEngine *engine)
        : ic_(ic), engine_(engine) {}

    InputContext *ic_;
    TableEngine *engine_;
    bool lastIsPunc_ = false;

    TableContext *context(const InputMethodEntry *entry);
    void release();
    void reset(const InputMethodEntry *entry = nullptr);

    void keyEvent(const InputMethodEntry &entry, KeyEvent &event);

    void commitBuffer(bool commitCode);
    void updateUI();
    void pushLastCommit(const std::string &lastCommit,
                        const std::string &lastSegment);

private:
    bool handleCandidateList(const TableConfig &config, KeyEvent &event);
    bool handlePinyinMode(KeyEvent &event);
    bool handleLookupPinyinOrModifyDictionaryMode(KeyEvent &event);
    bool handleAddPhraseMode(KeyEvent &event);

    TableMode mode_ = TableMode::Normal;
    std::string pinyinModePrefix_;
    InputBuffer pinyinModeBuffer_{
        {InputBufferOption::AsciiOnly, InputBufferOption::FixedCursor}};
    size_t lookupPinyinIndex_ = 0;
    std::string lookupPinyinString_;
    std::string lastContext_;
    std::string lastCommit_;
    std::string lastSegment_;
    std::unique_ptr<TableContext> context_;
};
}

#endif // _TABLE_STATE_H_
