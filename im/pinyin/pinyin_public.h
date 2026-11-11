/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 for Android Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_CHINESE_ADDONS_PINYIN_PUBLIC_H_
#define _FCITX5_CHINESE_ADDONS_PINYIN_PUBLIC_H_

#include <string>
#include <vector>
#include <fcitx/addoninstance.h>

/**
 * Key hints from the engine's active built-in or custom Shuangpin profile.
 * Returns flattened triples: key, compound initials, compound finals.
 * Multiple labels are slash-separated. Non-alphabetic input keys may have
 * empty label fields. An unavailable profile produces an empty vector.
 */
FCITX_ADDON_DECLARE_FUNCTION(PinyinEngine, shuangpinKeyHints,
                             std::vector<std::string>());

#endif
