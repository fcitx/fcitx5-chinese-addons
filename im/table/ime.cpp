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
                     const TableConfig &config) {
    libime::TableOptions options;

    options.setOrderPolicy(converOrderPolicy(*config.orderPolicy));
    options.setNoSortInputLength(*config.noSortInputLength);
    options.setAutoSelect(*config.autoSelect);
    options.setAutoSelectLength(*config.autoSelectLength);
    options.setNoMatchAutoSelectLength(*config.noMatchAutoSelectLength);
    options.setCommitRawInput(*config.commitRawInput);
    options.setMatchingKey(Key::keySymToUnicode(config.matchingKey->sym()));
    std::set<uint32_t> endKeys;
    for (auto &key : *config.endKey) {
        auto chr = Key::keySymToUnicode(key.sym());
        if (chr) {
            endKeys.insert(chr);
        }
    }
    options.setEndKey(endKeys);
    options.setExactMatch(*config.exactMatch);
    options.setLearning(*config.learning);
    options.setAutoPhraseLength(*config.autoPhraseLength);
    options.setSaveAutoPhraseAfter(*config.saveAutoPhraseAfter);
    options.setAutoRuleSet(std::unordered_set<std::string>(
        config.autoRuleSet->begin(), config.autoRuleSet->end()));
    options.setLanguageCode(*config.languageCode);

    dict->setTableOptions(options);
}
}

TableIME::TableIME(libime::LanguageModelResolver *lm) : lm_(lm) {}

std::tuple<libime::TableBasedDictionary *, libime::UserLanguageModel *,
           TableConfig *>
TableIME::requestDict(boost::string_view name) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        FCITX_LOG(Debug) << "Load table config for: " << name;
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
        auto &config = iter->second.config;
        config.load(rawConfig);

        try {
            auto dict = std::make_unique<libime::TableBasedDictionary>();
            auto dictFile = StandardPath::global().open(
                StandardPath::Type::PkgData, *config.file, O_RDONLY);
            FCITX_LOG(Debug) << "Load table at: " << *config.file;
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
                    stringutils::concat("table/", name.to_string(), ".user.dict"), O_RDONLY);
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(dictFile.fd(),
                           boost::iostreams::file_descriptor_flags::
                               never_close_handle);
                std::istream in(&buffer);
                dict->loadUser(in);
            } catch (const std::exception &) {
            }

            populateOptions(dict, iter->second.config);
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
            &iter->second.config};
}

void TableIME::saveAll() {
    for (auto &p : tables_) {
        saveDict(p.first);
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
}
