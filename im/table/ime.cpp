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
#include "ime.h"
#include "config.h"
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcntl.h>
#include <libime/table/tablebaseddictionary.h>
#include <libime/table/tableoptions.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(table_logcategory, "table")

namespace {

libime::OrderPolicy converOrderPolicy(fcitx::OrderPolicy policy) {
    switch (policy) {
#define POLICY_CONVERT(NAME)                                                   \
    case fcitx::OrderPolicy::NAME:                                             \
        return libime::OrderPolicy::NAME;
        POLICY_CONVERT(No)
        POLICY_CONVERT(Freq)
        POLICY_CONVERT(Fast)
    }
    return libime::OrderPolicy::Freq;
}

void populateOptions(libime::TableBasedDictionary *dict,
                     const TableConfigRoot &root) {
    libime::TableOptions options;

    options.setOrderPolicy(converOrderPolicy(*root.config->orderPolicy));
    options.setNoSortInputLength(*root.config->noSortInputLength);
    options.setAutoSelect(*root.config->autoSelect);
    options.setAutoSelectLength(*root.config->autoSelectLength);
    options.setNoMatchAutoSelectLength(*root.config->noMatchAutoSelectLength);
    options.setCommitRawInput(*root.config->commitRawInput);
    options.setMatchingKey(
        Key::keySymToUnicode(root.config->matchingKey->sym()));
    std::set<uint32_t> endKeys;
    TABLE_DEBUG() << "End key" << *root.config->endKey;
    for (const auto &key : *root.config->endKey) {
        auto chr = Key::keySymToUnicode(key.sym());
        if (chr) {
            endKeys.insert(chr);
        }
    }
    options.setEndKey(endKeys);
    options.setExactMatch(*root.config->exactMatch);
    options.setLearning(*root.config->learning);
    options.setAutoPhraseLength(*root.config->autoPhraseLength);
    options.setSaveAutoPhraseAfter(*root.config->saveAutoPhraseAfter);
    options.setAutoRuleSet(std::unordered_set<std::string>(
        root.config->autoRuleSet->begin(), root.config->autoRuleSet->end()));
    options.setLanguageCode(*root.im->languageCode);

    dict->setTableOptions(options);
}
} // namespace

TableIME::TableIME(libime::LanguageModelResolver *lm) : lm_(lm) {}

std::tuple<libime::TableBasedDictionary *, libime::UserLanguageModel *,
           const TableConfig *>
TableIME::requestDict(boost::string_view name) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        TABLE_DEBUG() << "Load table config for: " << name;
        std::string filename = "inputmethod/";
        filename.append(name.begin(), name.end());
        filename += ".conf";
        auto files = StandardPath::global().openAll(StandardPath::Type::PkgData,
                                                    filename, O_RDONLY);
        RawConfig rawConfig;
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | boost::adaptors::reversed) {
            readFromIni(rawConfig, file.fd());
        }

        iter = tables_
                   .emplace(std::piecewise_construct, std::make_tuple(name),
                            std::make_tuple())
                   .first;
        auto &root = iter->second.root;
        root.load(rawConfig);

        try {
            auto dict = std::make_unique<libime::TableBasedDictionary>();
            auto dictFile = StandardPath::global().open(
                StandardPath::Type::PkgData, *root.config->file, O_RDONLY);
            FCITX_LOG(Debug) << "Load table at: " << *root.config->file;
            if (dictFile.fd() < 0) {
                throw std::runtime_error("Couldn't open file");
            }
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(dictFile.fd(), boost::iostreams::file_descriptor_flags::
                                          never_close_handle);
            std::istream in(&buffer);
            dict->load(in);
            iter->second.dict = std::move(dict);
        } catch (const std::exception &) {
        }

        if (auto dict = iter->second.dict.get()) {
            try {
                auto dictFile = StandardPath::global().openUser(
                    StandardPath::Type::PkgData,
                    stringutils::concat("table/", name.to_string(),
                                        ".user.dict"),
                    O_RDONLY);
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(dictFile.fd(),
                           boost::iostreams::file_descriptor_flags::
                               never_close_handle);
                std::istream in(&buffer);
                dict->loadUser(in);
            } catch (const std::exception &) {
            }

            populateOptions(dict, iter->second.root);
            auto lmFile = lm_->languageModelFileForLanguage(
                dict->tableOptions().languageCode());
            iter->second.model =
                std::make_unique<libime::UserLanguageModel>(lmFile);

            try {
                auto dictFile = StandardPath::global().openUser(
                    StandardPath::Type::PkgData,
                    stringutils::concat("table/", name, ".history"), O_RDONLY);
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(dictFile.fd(),
                           boost::iostreams::file_descriptor_flags::
                               never_close_handle);
                std::istream in(&buffer);
                iter->second.model->load(in);
            } catch (const std::exception &) {
            }
        }
    }

    return {iter->second.dict.get(), iter->second.model.get(),
            &(*iter->second.root.config)};
}

void TableIME::saveAll() {
    for (const auto &p : tables_) {
        saveDict(p.first);
    }
}

void TableIME::updateConfig(boost::string_view name, const RawConfig &config) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        return;
    }
    iter->second.root.load(config, true);

    if (iter->second.dict) {
        populateOptions(iter->second.dict.get(), iter->second.root);
    }
    safeSaveAsIni(iter->second.root,
                  stringutils::concat("inputmethod/", name, ".conf"));
}

void TableIME::releaseUnusedDict(const std::unordered_set<std::string> &names) {
    for (auto iter = tables_.begin(); iter != tables_.end();) {
        if (names.count(iter->first) == 0) {
            TABLE_DEBUG() << "Release unused table: " << iter->first;
            saveDict(iter->first);
            iter = tables_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void TableIME::saveDict(boost::string_view name) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        return;
    }
    libime::TableBasedDictionary *dict = iter->second.dict.get();
    libime::UserLanguageModel *lm = iter->second.model.get();
    auto fileName = stringutils::joinPath("table", name);

    StandardPath::global().safeSave(
        StandardPath::Type::PkgData, fileName + ".user.dict", [dict](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                dict->saveUser(out);
                return static_cast<bool>(out);
            } catch (const std::exception &) {
                return false;
            }
        });

    StandardPath::global().safeSave(
        StandardPath::Type::PkgData, fileName + ".history", [lm](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                lm->save(out);
                return static_cast<bool>(out);
            } catch (const std::exception &) {
                return false;
            }
        });
}
} // namespace fcitx
