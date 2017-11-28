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
#ifndef _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_
#define _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_

#include <chrono>
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
FCITX_ADDON_DECLARE_FUNCTION(CloudPinyin, toggleKey, const fcitx::KeyList &());
FCITX_ADDON_DECLARE_FUNCTION(CloudPinyin, resetError, void());

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
        setText(fcitx::Text("\xe2\x98\x81"));
        auto ref = watch();
        cloudpinyin_->call<fcitx::ICloudPinyin::request>(
            pinyin, [ref](const std::string &pinyin, const std::string &hanzi) {
                FCITX_UNUSED(pinyin);
                auto self = ref.get();
                if (self) {
                    self->fill(hanzi);
                }
            });
        constructor_ = false;
    }

    void select(fcitx::InputContext *inputContext) const override {
        if (!filled_ || word_.empty()) {
            // not filled, do nothing
            return;
        }
        callback_(inputContext, selectedSentence_, word_);
    }

    bool filled() const { return filled_; }
    const std::string &word() { return word_; }

private:
    static constexpr long int LOADING_TIME_QUICK_THRESHOLD = 300;

    void fill(const std::string &hanzi) {
        setText(fcitx::Text(hanzi));
        word_ = hanzi;
        filled_ = true;
        if (!constructor_) {
            update();
        }
    }

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
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - timestamp_)
                    .count();
            if (ms > LOADING_TIME_QUICK_THRESHOLD) {
                setText(fcitx::Text("\xe2\x98\x81"));
                word_ = std::string();
            } else {
                modifiable->remove(idx);
            }
        }
        // use stack variable inputContext, because it may be removed already
        inputContext->updateUserInterface(
            fcitx::UserInterfaceComponent::InputPanel);
    }

    std::chrono::high_resolution_clock::time_point timestamp_ =
        std::chrono::high_resolution_clock::now();
    bool filled_ = false;
    std::string word_;
    std::string selectedSentence_;
    fcitx::InputContext *inputContext_;
    bool constructor_ = true;
    CloudPinyinSelectedCallback callback_;
};

#endif // _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_
