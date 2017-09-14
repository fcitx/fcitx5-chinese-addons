/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
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
#ifndef _FULLWIDTH_FULLWIDTH_PUBLIC_H_
#define _FULLWIDTH_FULLWIDTH_PUBLIC_H_

#include <fcitx/addoninstance.h>

FCITX_ADDON_DECLARE_FUNCTION(Fullwidth, enable, void(const std::string &im));
FCITX_ADDON_DECLARE_FUNCTION(Fullwidth, disable, void(const std::string &im));

#endif // _FULLWIDTH_FULLWIDTH_PUBLIC_H_
