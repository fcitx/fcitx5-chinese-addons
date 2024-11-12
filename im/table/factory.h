/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _TABLE_FACTORY_H_
#define _TABLE_FACTORY_H_

#include <fcitx/addonfactory.h>
namespace fcitx {

class TableEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // _TABLE_FACTORY_H_