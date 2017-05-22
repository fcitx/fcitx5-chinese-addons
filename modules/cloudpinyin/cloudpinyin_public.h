/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
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
#ifndef _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_
#define _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_

#include <fcitx-utils/trackableobject.h>
#include <fcitx/addoninstance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <functional>
#include <string>

typedef std::function<void(const std::string &pinyin, const std::string &hanzi)>
    CloudPinyinCallback;

typedef std::function<void(fcitx::InputContext *inputContext,
                           const std::string &selected,
                           const std::string &word)>
    CloudPinyinSelectedCallback;

FCITX_ADDON_DECLARE_FUNCTION(CloudPinyin, request,
                             void(const std::string &pinyin,
                                  CloudPinyinCallback));

class CloudPinyinCandidateWord
    : public fcitx::CandidateWord,
      public fcitx::TrackableObject<CloudPinyinCandidateWord> {
public:
    CloudPinyinCandidateWord(fcitx::AddonInstance *cloudpinyin_,
                             const std::string &pinyin,
                             const std::string &selectedSentence,
                             fcitx::InputContext *inputContext,
                             CloudPinyinSelectedCallback callback)
        : CandidateWord(fcitx::Text{}), selectedSentence_(selectedSentence),
          inputContext_(inputContext), callback_(callback) {
        // use cloud unicode char
        text() = fcitx::Text("\xe2\x98\x81");
        auto ref = watch();
        cloudpinyin_->call<fcitx::ICloudPinyin::request>(
            pinyin, [ref](const std::string &pinyin, const std::string &hanzi) {
                FCITX_UNUSED(pinyin);
                auto self = ref.get();
                if (self) {
                    self->text() = fcitx::Text(hanzi);
                    self->word_ = hanzi;
                    self->filled_ = true;
                    if (!self->constructor_) {
                        self->update();
                    }
                }
            });
        constructor_ = false;
    }

    void select(fcitx::InputContext *inputContext) const override {
        if (!filled_) {
            // not filled, do nothing
            return;
        }
        callback_(inputContext, selectedSentence_, word_);
    }

    bool filled() const { return filled_; }
    const std::string &word() { return word_; }

private:
    void update() {
        auto inputContext = inputContext_;
        auto candidateList = inputContext_->inputPanel().candidateList();
        if (!candidateList) {
            return;
        }
        auto modifiable = candidateList->toModifiable();
        if (!modifiable) {
            return;
        }

        int idx = -1;
        bool dup = false;
        for (auto i = 0, e = modifiable->totalSize(); i < e; i++) {
            auto &candidate = modifiable->candidateFromAll(i);
            if (static_cast<CandidateWord *>(this) == &candidate) {
                idx = i;
            } else {
                if (!dup && text().toString() == candidate.text().toString()) {
                    dup = true;
                }
            }
        }
        if (idx >= 0 && (dup || word_.empty())) {
            modifiable->remove(idx);
        }
        // use stack variable inputContext, because it may be removed already
        inputContext->updateUserInterface(
            fcitx::UserInterfaceComponent::InputPanel);
    }

    bool filled_ = false;
    std::string word_;
    std::string selectedSentence_;
    fcitx::InputContext *inputContext_;
    bool constructor_ = true;
    CloudPinyinSelectedCallback callback_;
};

#endif // _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_
