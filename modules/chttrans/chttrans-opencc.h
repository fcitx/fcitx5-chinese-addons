/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CHTTRANS_CHTTRANS_OPENCC_H_
#define _CHTTRANS_CHTTRANS_OPENCC_H_

#include "chttrans.h"
#include <opencc.h>

class OpenCCBackend : public ChttransBackend {
public:
    std::string convertSimpToTrad(const std::string &) override;
    std::string convertTradToSimp(const std::string &) override;

    void updateConfig(const ChttransConfig &config) override;

protected:
    bool loadOnce(const ChttransConfig &config) override;

private:
    std::unique_ptr<opencc::SimpleConverter> s2t_;
    std::unique_ptr<opencc::SimpleConverter> t2s_;
};

#endif // _CHTTRANS_CHTTRANS_OPENCC_H_
