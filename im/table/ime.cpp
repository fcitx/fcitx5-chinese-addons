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
#include <fcitx-utils/standardpath.h>
#include <fcitx-config/iniparser.h>
#include <libime/table/tablebaseddictionary.h>
#include <boost/range/adaptor/reversed.hpp>
#include <fcntl.h>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include "config.h"

namespace fcitx {

TableIME::TableIME(libime::LanguageModelResolver* lm) : libime::TableIME(lm) {
}

const TableConfig &TableIME::config(boost::string_view name) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        throw std::runtime_error("Need to request dict first");
    }
    return iter->second.config;
}

libime::TableBasedDictionary *TableIME::requestDictImpl(boost::string_view name) {
    auto iter = tables_.find(name.to_string());
    if (iter == tables_.end()) {
        std::string filename = "inputmethod/";
        filename.append(name.begin(), name.end());
        filename += ".conf";
        auto files = StandardPath::global().openAll(StandardPath::Type::PkgData, filename, O_RDONLY);
        RawConfig rawConfig;
        // reverse the order, so we end up parse user file at last.
        for (const auto &file : files | boost::adaptors::reversed) {
            readFromIni(rawConfig, file.fd());
        }

        iter = tables_.emplace(std::piecewise_construct, std::make_tuple(name), std::make_tuple()).first;
        auto &config = iter->second.config;
        config.load(rawConfig);

        try {
            auto table = std::make_unique<libime::TableBasedDictionary>();
            auto dictFile = StandardPath::global().open(StandardPath::Type::PkgData, *config.file, O_RDONLY);
            if (dictFile.fd() < 0) {
                throw std::runtime_error("Couldn't open file");
            }
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(dictFile.fd(), boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::istream in(&buffer);
            table->load(in);
            iter->second.dict = table;
        } catch (const std::exception &) {
        }

        if (iter->second.dict) {
            try {
                auto dict = iter->second.dict.get();
                std::string fileName = "table";
                fileName
                auto dictFile = StandardPath::global().openUser(StandardPath::Type::PkgData, , O_RDONLY);
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(dictFile.fd(), boost::iostreams::file_descriptor_flags::
                                never_close_handle);
                std::istream in(&buffer);
                table->loadUser(in);
            } catch (const std::exception &) {
            }
        }
    }

    return iter->second.dict.get();
}

void TableIME::saveDictImpl(libime::TableBasedDictionary *dict) {
    auto iter = tableToName_.find(dict);
    if (iter == tableToName_.end()) {
        return;
    }
    auto &name = iter->second;
}

}
