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
#ifndef _TABLE_TABLEDICTRESOLVER_H_
#define _TABLE_TABLEDICTRESOLVER_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/table/tablebaseddictionary.h>
#include <tuple>

namespace fcitx {

FCITX_CONFIG_ENUM(OrderPolicy, No, Freq, Fast);

FCITX_CONFIGURATION(
    TableConfig, HiddenOption<std::string> file{this, "File", _("File")};
    Option<KeyList> prevPage{
        this, "PrevPage", _("Prev page"), {Key(FcitxKey_Up)}};
    Option<KeyList> nextPage{
        this, "NextPage", _("Next page"), {Key(FcitxKey_Down)}};
    Option<KeyList> prevCandidate{
        this, "PrevCandidate", "Prev Candidate", {Key("Left")}};
    Option<KeyList> nextCandidate{
        this, "NextCandidate", "Next Candidate", {Key("Right")}};
    HiddenOption<KeyList> selection{
        this,
        "Selection",
        _("Selection"),
        {Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
         Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
         Key(FcitxKey_9), Key(FcitxKey_0)}};
    Option<int, IntConstrain> pageSize{this, "PageSize", "Page size", 5,
                                       IntConstrain(3, 10)};
    Option<bool> useFullWidth{this, "UseFullWidth", _("Use full width"), true};
    Option<Key> quickphrase{this, "QuickPhraseKey",
                            _("Key to trigger quickphrase")};
    HiddenOption<std::string> icon{this, "Icon", _("Icon")};
    Option<int> noSortInputLength{this, "NoSortInputLength",
                                  _("Don't sort word shorter")};
    Option<Key> pinyinKey{this, "PinyinKey", _("Prefix key to trigger Pinyin")};
    Option<bool> autoSelect{this, "AutoSelect", _("Auto select candidate")};
    Option<int> autoSelectLength{this, "AutoSelectLength",
                                 _("Auto select candidate Length")};
    Option<int> noMatchAutoSelectLength{
        this, "NoMatchAutoSelectLength",
        _("Auto select last candidate when there is no new match")};
    Option<int> commitRawInput{
        this, "CommitRawInput",
        _("Commit raw input when there is no candidate")};
    Option<OrderPolicy> orderPolicy{this, "OrderPolicy", _("Order policy")};
    HiddenOption<KeyList> endKey{this, "EndKey", _("End key")};
    Option<Key> matchingKey{this, "MatchingKey", _("Wildcard matching Key")};
    Option<int> autoPhraseLength{this, "AutoPhraseLength",
                                 _("Auto phrase length"), -1};
    Option<int> saveAutoPhraseAfter{this, "SaveAutoPhraseAfter",
                                    _("Save auto phrase"), -1};
    Option<bool> exactMatch{this, "ExactMatch", _("Exact Match")};
    Option<bool> learning{this, "Learning", _("Learning"), true};
    Option<bool> hint{this, "Hint", _("Display Hint for word")};
    Option<bool> displayCustomHint{this, "DisplayCustomHint",
                                   _("Display custom hint")};
    HiddenOption<std::vector<std::string>> autoRuleSet{this, "AutoRuleSet",
                                                       _("Auto rule set")};);

FCITX_CONFIGURATION(PartialIMInfo, HiddenOption<std::string> languageCode{
                                       this, "LangCode", "Language Code"};);

struct NoSaveAnnotation {
    bool skipDescription() const { return true; }
    bool skipSave() const { return true; }
    void dumpDescription(RawConfig &) const {}
};

FCITX_CONFIGURATION(TableConfigRoot,
                    Option<TableConfig> config{this, "Table", "Table"};
                    Option<PartialIMInfo, NoConstrain<PartialIMInfo>,
                           DefaultMarshaller<PartialIMInfo>, NoSaveAnnotation>
                        im{this, "InputMethod", "InputMethod"};);

struct TableData {
    TableConfigRoot root;
    std::unique_ptr<libime::TableBasedDictionary> dict;
    std::unique_ptr<libime::UserLanguageModel> model;
};

class TableIME {
public:
    TableIME(libime::LanguageModelResolver *lmResolver);

    const TableConfig &config(boost::string_view name);

public:
    std::tuple<libime::TableBasedDictionary *, libime::UserLanguageModel *,
               const TableConfig *>
    requestDict(boost::string_view name);
    void saveDict(boost::string_view name);
    void saveAll();
    void updateConfig(boost::string_view name, const RawConfig &config);

    void releaseUnusedDict(const std::unordered_set<std::string> &names);

private:
    libime::LanguageModelResolver *lm_;
    std::unordered_map<std::string, TableData> tables_;
};

FCITX_DECLARE_LOG_CATEGORY(table_logcategory);

#define TABLE_DEBUG() FCITX_LOGC(::fcitx::table_logcategory, Debug)
} // namespace fcitx

#endif // _TABLE_TABLEDICTRESOLVER_H_
