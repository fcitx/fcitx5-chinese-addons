/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 for Android Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_CHINESE_ADDONS_PINYIN_PUBLIC_H_
#define _FCITX5_CHINESE_ADDONS_PINYIN_PUBLIC_H_

#include <string>
#include <fcitx/addoninstance.h>

/**
 * Input keys accepted by the engine's current built-in or custom Shuangpin
 * profile, in sorted order. An unavailable profile produces an empty string.
 */
FCITX_ADDON_DECLARE_FUNCTION(PinyinEngine, shuangpinInputKeys, std::string());

#endif
