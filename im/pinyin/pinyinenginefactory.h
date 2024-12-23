/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_PINYINENGINEFACTORY_H_
#define _PINYIN_PINYINENGINEFACTORY_H_

#include <fcitx/addonfactory.h>

namespace fcitx {

class PinyinEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // _PINYIN_PINYINENGINEFACTORY_H_