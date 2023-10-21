/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
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
#include <libime/core/utils.h>
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
    options.setAutoSelectRegex(*root.config->autoSelectRegex);
    options.setNoMatchAutoSelectLength(*root.config->noMatchAutoSelectLength);
    options.setNoMatchAutoSelectRegex(*root.config->noMatchAutoSelectRegex);
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
    options.setSortByCodeLength(*root.config->sortByCodeLength);

    dict->setTableOptions(options);
}
} // namespace

TableIME::TableIME(libime::LanguageModelResolver *lm) : lm_(lm) {}

std::tuple<libime::TableBasedDictionary *, libime::UserLanguageModel *,
           const TableConfig *>
TableIME::requestDict(const std::string &name) {
    auto iter = tables_.find(name);
    if (iter == tables_.end()) {
        TABLE_DEBUG() << "Load table config for: " << name;
        iter = tables_
                   .emplace(std::piecewise_construct, std::make_tuple(name),
                            std::make_tuple())
                   .first;
        auto &root = iter->second.root;

        std::string filename = stringutils::joinPath(
            "inputmethod", stringutils::concat(name, ".conf"));
        auto files = StandardPath::global().openAll(StandardPath::Type::PkgData,
                                                    filename, O_RDONLY);
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | boost::adaptors::reversed) {
            RawConfig rawConfig;
            readFromIni(rawConfig, file.fd());
            root.load(rawConfig, true);
        }

        // So "Default" can be reset to current value.
        root.syncDefaultValueToCurrent();

        std::string customization =
            stringutils::joinPath("table", stringutils::concat(name, ".conf"));
        files = StandardPath::global().openAll(StandardPath::Type::PkgConfig,
                                               customization, O_RDONLY);
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | boost::adaptors::reversed) {
            RawConfig rawConfig;
            readFromIni(rawConfig, file.fd());
            root.load(rawConfig, true);
        }

        try {
            auto dict = std::make_unique<libime::TableBasedDictionary>();
            auto dictFile = StandardPath::global().open(
                StandardPath::Type::PkgData, *root.config->file, O_RDONLY);
            TABLE_DEBUG() << "Load table at: " << *root.config->file;
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

        if (auto *dict = iter->second.dict.get()) {
            try {
                auto dictFile = StandardPath::global().openUser(
                    StandardPath::Type::PkgData,
                    stringutils::concat("table/", name, ".user.dict"),
                    O_RDONLY);
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(dictFile.fd(),
                           boost::iostreams::file_descriptor_flags::
                               never_close_handle);
                std::istream in(&buffer);
                dict->loadUser(in);
            } catch (const std::exception &e) {
                TABLE_DEBUG() << e.what();
            }

            populateOptions(dict, iter->second.root);
            std::shared_ptr<const libime::StaticLanguageModelFile> lmFile;
            try {
                if (*iter->second.root.config->useSystemLanguageModel) {
                    lmFile = lm_->languageModelFileForLanguage(
                        dict->tableOptions().languageCode());
                }
            } catch (...) {
                TABLE_DEBUG()
                    << "Load language model for "
                    << dict->tableOptions().languageCode() << " failed.";
            }
            iter->second.model =
                std::make_unique<libime::UserLanguageModel>(lmFile);
            iter->second.model->setUseOnlyUnigram(
                !*iter->second.root.config->useContextBasedOrder);

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
            } catch (const std::exception &e) {
                TABLE_DEBUG() << e.what();
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

void TableIME::updateConfig(const std::string &name, const RawConfig &config) {
    auto iter = tables_.find(name);
    if (iter == tables_.end()) {
        return;
    }
    iter->second.root.config.mutableValue()->load(config, true);

    if (iter->second.dict) {
        populateOptions(iter->second.dict.get(), iter->second.root);
    }

    safeSaveAsIni(iter->second.root, StandardPath::Type::PkgConfig,
                  stringutils::concat("table/", name, ".conf"));
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

void TableIME::saveDict(const std::string &name) {
    auto iter = tables_.find(name);
    if (iter == tables_.end()) {
        return;
    }
    libime::TableBasedDictionary *dict = iter->second.dict.get();
    libime::UserLanguageModel *lm = iter->second.model.get();
    if (!dict || !lm || !*iter->second.root.config->learning) {
        return;
    }
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

void TableIME::reloadAllDict() {
    std::unordered_set<std::string> names;
    for (const auto &pair : tables_) {
        names.insert(pair.first);
    }
    tables_.clear();
    for (const auto &name : names) {
        requestDict(name);
    }
}

} // namespace fcitx
