/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINHELPER_AUXCODE_H_
#define _PINYINHELPER_AUXCODE_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace fcitx {

class AuxCode {
public:
    AuxCode() = default;

    void loadProfile(const std::string &profile);
    void loadFromFile(const std::string &path);
    bool isLoaded() const { return loaded_; }

    // Public for unit tests; internal use by matchPhrase only.
    std::string getFirstCode(const std::string &character) const;
    bool matchPhrase(const std::string &phrase,
                     const std::string &auxInput) const;

private:
    void parseStream(std::istream &in);

    std::unordered_map<std::string, std::vector<std::string>> table_;
    std::string loadedProfile_;
    bool loaded_ = false;
};

} // namespace fcitx

#endif // _PINYINHELPER_AUXCODE_H_
