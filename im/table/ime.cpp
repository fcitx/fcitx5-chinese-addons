/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "ime.h"
#include <cstdint>
#include <exception>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcntl.h>
#include <fstream>
#include <ios>
#include <istream>
#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/core/utils.h>
#include <libime/table/tablebaseddictionary.h>
#include <libime/table/tableoptions.h>
#include <memory>
#include <ostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(table_logcategory, "table")

namespace {

struct BinaryOrTextDict {
    bool operator()(const std::string &path, const std::string &dir,
                    bool isUser) const {
        FCITX_UNUSED(dir);
        FCITX_UNUSED(isUser);
        return stringutils::endsWith(path, ".txt") ||
               stringutils::endsWith(path, ".dict");
    }
};

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
    options.setEndKey(std::move(endKeys));
    options.setExactMatch(*root.config->exactMatch);
    options.setLearning(*root.config->learning);
    options.setAutoPhraseLength(*root.config->autoPhraseLength);
    options.setSaveAutoPhraseAfter(*root.config->saveAutoPhraseAfter);
    options.setAutoRuleSet(std::unordered_set<std::string>(
        root.config->autoRuleSet->begin(), root.config->autoRuleSet->end()));
    options.setLanguageCode(*root.im->languageCode);
    options.setSortByCodeLength(*root.config->sortByCodeLength);

    dict->setTableOptions(std::move(options));
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

        const std::string filename = stringutils::joinPath(
            "inputmethod", stringutils::concat(name, ".conf"));
        auto files = StandardPath::global().openAll(StandardPath::Type::PkgData,
                                                    filename, O_RDONLY);
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | std::views::reverse) {
            RawConfig rawConfig;
            readFromIni(rawConfig, file.fd());
            root.load(rawConfig, true);
        }

        // So "Default" can be reset to current value.
        root.syncDefaultValueToCurrent();

        const std::string customization =
            stringutils::joinPath("table", stringutils::concat(name, ".conf"));
        files = StandardPath::global().openAll(StandardPath::Type::PkgConfig,
                                               customization, O_RDONLY);
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | std::views::reverse) {
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
            IFDStreamBuf buffer(dictFile.fd());
            std::istream in(&buffer);
            dict->load(in);
            iter->second.dict = std::move(dict);
        } catch (const std::exception &e) {
            TABLE_ERROR() << "Failed to load table: " << *root.config->file
                          << ", error: " << e.what();
        }

        if (auto *dict = iter->second.dict.get()) {
            try {
                auto dictFile = StandardPath::global().openUser(
                    StandardPath::Type::PkgData,
                    stringutils::concat("table/", name, ".user.dict"),
                    O_RDONLY);
                IFDStreamBuf buffer(dictFile.fd());
                std::istream in(&buffer);
                dict->loadUser(in);
            } catch (const std::exception &e) {
                TABLE_DEBUG() << e.what();
            }

            dict->removeAllExtra();
            auto extraDicts = StandardPath::global().locate(
                StandardPath::Type::PkgData,
                stringutils::concat("table/", name, ".dict.d"),
                BinaryOrTextDict());
            for (const auto &[name, file] : extraDicts) {
                try {
                    std::ifstream in(file, std::ios::in | std::ios::binary);
                    const auto fileFormat = stringutils::endsWith(name, ".txt")
                                                ? libime::TableFormat::Text
                                                : libime::TableFormat::Binary;
                    dict->loadExtra(in, fileFormat);
                } catch (const std::exception &e) {
                    TABLE_DEBUG() << e.what();
                }
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
                IFDStreamBuf buffer(dictFile.fd());
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

    StandardPath::global().safeSave(StandardPath::Type::PkgData,
                                    fileName + ".user.dict", [dict](int fd) {
                                        OFDStreamBuf buffer(fd);
                                        std::ostream out(&buffer);
                                        try {
                                            dict->saveUser(out);
                                            return static_cast<bool>(out);
                                        } catch (const std::exception &) {
                                            return false;
                                        }
                                    });

    StandardPath::global().safeSave(StandardPath::Type::PkgData,
                                    fileName + ".history", [lm](int fd) {
                                        OFDStreamBuf buffer(fd);
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
