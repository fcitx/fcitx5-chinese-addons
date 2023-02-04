/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-opencc.h"
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>

bool OpenCCBackend::loadOnce(const ChttransConfig &config) {
    updateConfig(config);
    return true;
}

void OpenCCBackend::updateConfig(const ChttransConfig &config) {
    using namespace fcitx;
    
    auto s2tProfile = config.openCCS2TProfile->empty()
                    ? OPENCC_DEFAULT_CONFIG_SIMP_TO_TRAD
                    : *config.openCCS2TProfile;
    auto s2tProfilePath = StandardPath::global().locate(
        StandardPath::Type::Data, stringutils::joinPath("opencc", s2tProfile)
    );
    if (s2tProfilePath.empty()) {
        s2tProfilePath = s2tProfile;
    }

    try {
        auto s2t = std::make_unique<opencc::SimpleConverter>(s2tProfilePath);
        s2t_ = std::move(s2t);
    } catch (const std::exception &e) {
    }

    auto t2sProfile = config.openCCT2SProfile->empty()
                    ? OPENCC_DEFAULT_CONFIG_TRAD_TO_SIMP
                    : *config.openCCT2SProfile;
    auto t2sProfilePath = StandardPath::global().locate(
        StandardPath::Type::Data, stringutils::joinPath("opencc", t2sProfile)
    );
    if (t2sProfilePath.empty()) {
        t2sProfilePath = t2sProfile;
    }

    try {
        auto t2s = std::make_unique<opencc::SimpleConverter>(t2sProfilePath);
        t2s_ = std::move(t2s);
    } catch (const std::exception &e) {
    }
}

std::string OpenCCBackend::convertSimpToTrad(const std::string &str) {
    if (s2t_) {
        try {
            return s2t_->Convert(str);
        } catch (const std::exception &e) {
        }
    }
    return str;
}

std::string OpenCCBackend::convertTradToSimp(const std::string &str) {
    if (t2s_) {
        try {
            return t2s_->Convert(str);
        } catch (const std::exception &e) {
        }
    }
    return str;
}
