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
#ifndef _TABLE_TABLEDICTRESOLVER_H_
#define _TABLE_TABLEDICTRESOLVER_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-utils/i18n.h>
#include <libime/table/tableime.h>

namespace fcitx {

FCITX_CONFIG_ENUM(OrderPolicy, No, Freq, Fast);

FCITX_CONFIGURATION(
    TableConfig, Option<std::string> file{this, "Table/File", _("File")};
    Option<KeyList> prevPage{
        this, "Table/PrevPage", _("Prev page"), {Key(FcitxKey_Up)}};
    Option<KeyList> nextPage{
        this, "Table/NextPage", _("Next page"), {Key(FcitxKey_Down)}};
    Option<KeyList> selection{
        this,
        "Table/Selection",
        _("Selection"),
        {Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
         Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
         Key(FcitxKey_9), Key(FcitxKey_0)}};
    Option<int, IntConstrain> pageSize{this, "Table/PageSize", "Page size", 5,
                                       IntConstrain(3, 10)};
    Option<bool> useFullWidth{this, "Table/UseFullWidth", _("Use full width"),
                              true};
    Option<Key> quickphrase{this, "Table/QuickPhraseKey",
                            _("Key to trigger quickphrase")};
    Option<std::string> icon{this, "Table/Icon", _("Icon")};
    Option<int> noSortInputLength{this, "Table/NoSortLength",
                                  _("Don't sort word shorter")};
    Option<std::string> pinyinKey{this, "Table/PinyinKey",
                                  _("Prefix key to trigger Pinyin")};
    Option<bool> autoSelect{this, "Table/AutoSelect",
                            _("Auto select candidate")};
    Option<bool> autoSelectLength{this, "Table/AutoSelectLength",
                                  _("Auto select candidate Length")};
    Option<int> noMatchAutoSelectLength{
        this, "Table/NoMatchAutoSelectLength",
        _("Auto select last candidate when there is no new match")};
    Option<int> commitRawInput{
        this, "Table/CommitRawInput",
        _("Commit raw input when there is no candidate")};
    Option<OrderPolicy> orderPolicy{this, "Table/OrderPolicy",
                                    _("Order policy")};
    Option<KeyList> endKey{this, "Table/EndKey", _("End key")};
    Option<Key> matchingKey{this, "Table/MatchingKey",
                            _("Wildcard matching Key")};
    Option<bool> exactMatch{this, "Table/ExactMatch", _("Exact Match")};
    Option<bool> autoLearning{this, "Table/AutoLearning", _("Auto learning")};
    Option<int> autoPhraseLength{this, "Table/AutoPhraseLength",
                                 _("Auto phrase length")};
    Option<bool> saveAutoPhrase{this, "Table/SaveAutoPhrase",
                                _("Save auto phrase")};
    Option<bool> noMatchDontCommit{this, "Table/NoMatchDontCommit",
                                   _("Do not commit when there is no match")};
    Option<bool> hint{this, "Table/Hint", _("Display Hint for word")};
    Option<bool> displayCustomHint{this, "Table/DisplayCustomHint",
                                   _("Display custom hint")};
    Option<bool> firstCandidateAsPreedit{this, "Table/FirstCandidateAsPreedit",
                                         _("First candidate as preedit")};
    Option<std::vector<std::string>> autoRuleSet{this, "Table/AutoRuleSet",
                                                 _("Auto rule set")};
    Option<std::string> languageCode{this, "InputMethod/LangCode",
                                     "Language Code"};);

struct TableData {
    TableConfig config;
    std::unique_ptr<libime::TableBasedDictionary> dict;
};

class TableIME : public libime::TableIME {
public:
    TableIME(libime::LanguageModelResolver *lmResolver);

    const TableConfig &config(boost::string_view name);

protected:
    libime::TableBasedDictionary *
    requestDictImpl(boost::string_view name) override;
    void saveDictImpl(libime::TableBasedDictionary *dict) override;

private:
    std::unordered_map<std::string, TableData> tables_;
    std::unordered_map<libime::TableBasedDictionary *, std::string>
        tableToName_;
};
}

#endif // _TABLE_TABLEDICTRESOLVER_H_
