/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-opencc.h"

bool OpenCCBackend::loadOnce(const ChttransConfig &config) {
    updateConfig(config);
    return true;
}

void OpenCCBackend::updateConfig(const ChttransConfig &config) {
    auto s2tProfile = *config.openCCS2TProfile;
    if (s2tProfile.empty()) {
        s2tProfile = OPENCC_DEFAULT_CONFIG_SIMP_TO_TRAD;
    }

    try {
        auto s2t = std::make_unique<opencc::SimpleConverter>(s2tProfile);
        s2t_ = std::move(s2t);
    } catch (const std::exception &e) {
    }

    auto t2sProfile = *config.openCCT2SProfile;
    if (t2sProfile.empty()) {
        t2sProfile = OPENCC_DEFAULT_CONFIG_TRAD_TO_SIMP;
    }
    try {
        auto t2s = std::make_unique<opencc::SimpleConverter>(t2sProfile);
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
