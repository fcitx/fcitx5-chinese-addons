/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-opencc.h"

bool OpenCCBackend::loadOnce() {
    try {
        s2t_ = std::make_unique<opencc::SimpleConverter>(
            OPENCC_DEFAULT_CONFIG_SIMP_TO_TRAD);
        t2s_ = std::make_unique<opencc::SimpleConverter>(
            OPENCC_DEFAULT_CONFIG_TRAD_TO_SIMP);
    } catch (const std::exception &e) {
        return false;
    }
    return true;
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
