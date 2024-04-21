/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-opencc.h"
#include "chttrans.h"
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>

using namespace fcitx;

bool OpenCCBackend::loadOnce(const ChttransConfig &config) {
    updateConfig(config);
    return true;
}

std::string OpenCCBackend::locateProfile(const std::string &profile) {
    auto profilePath =
        openCCStandardPath().locate(StandardPath::Type::PkgData, profile);
    return profilePath.empty() ? profile : profilePath;
}

void OpenCCBackend::updateConfig(const ChttransConfig &config) {
    auto s2tProfile = *config.openCCS2TProfile;
    if (s2tProfile.empty() || s2tProfile == "default") {
        const std::string preferredS2TProfile = "s2tw.json";
        // Means it's resolved.
        if (locateProfile(preferredS2TProfile) != preferredS2TProfile) {
            s2tProfile = preferredS2TProfile;
        } else {
            s2tProfile = OPENCC_DEFAULT_CONFIG_SIMP_TO_TRAD;
        }
    }
    auto s2tProfilePath = locateProfile(s2tProfile);
    FCITX_DEBUG() << "s2tProfilePath: " << s2tProfilePath;

    try {
        auto s2t = std::make_unique<opencc::SimpleConverter>(s2tProfilePath);
        s2t_ = std::move(s2t);
    } catch (const std::exception &e) {
        FCITX_WARN() << "exception when loading s2t profile: " << e.what();
    }

    auto t2sProfile = *config.openCCT2SProfile;
    if (t2sProfile.empty() || t2sProfile == "default") {
        const std::string preferredT2SProfile = "tw2s.json";
        // Means it's resolved.
        if (locateProfile(preferredT2SProfile) != preferredT2SProfile) {
            t2sProfile = preferredT2SProfile;
        } else {
            t2sProfile = OPENCC_DEFAULT_CONFIG_TRAD_TO_SIMP;
        }
    }
    auto t2sProfilePath = locateProfile(t2sProfile);
    FCITX_DEBUG() << "t2sProfilePath: " << t2sProfilePath;

    try {
        auto t2s = std::make_unique<opencc::SimpleConverter>(t2sProfilePath);
        t2s_ = std::move(t2s);
    } catch (const std::exception &e) {
        FCITX_WARN() << "exception when loading t2s profile: " << e.what();
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
