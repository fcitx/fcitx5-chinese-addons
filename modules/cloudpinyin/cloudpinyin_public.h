/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
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
                             const std::string &selectedSentence, bool keep,
                             fcitx::InputContext *inputContext,
                             CloudPinyinSelectedCallback callback)
        : CandidateWord(fcitx::Text{}), selectedSentence_(selectedSentence),
          inputContext_(inputContext), callback_(std::move(callback)),
          keep_(keep) {
        // use cloud unicode char
        setText(fcitx::Text("\xe2\x98\x81"));
        auto ref = watch();
        cloudpinyin_->call<fcitx::ICloudPinyin::request>(
            pinyin, [ref](const std::string &pinyin, const std::string &hanzi) {
                FCITX_UNUSED(pinyin);
                auto *self = ref.get();
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
    const std::string &word() const { return word_; }
    fcitx::InputContext *inputContext() { return inputContext_; }

private:
    static constexpr long int LOADING_TIME_QUICK_THRESHOLD = 1000;

    void fill(const std::string &hanzi) {
        setText(fcitx::Text(hanzi));
        word_ = hanzi;
        filled_ = true;
        if (!constructor_) {
            update();
        }
    }

    void update() {
        auto *inputContext = inputContext_;
        auto candidateList = inputContext_->inputPanel().candidateList();
        if (!candidateList) {
            return;
        }
        auto *modifiable = candidateList->toModifiable();
        if (!modifiable) {
            return;
        }

        int idx = -1;
        std::optional<int> dupIndex;
        for (auto i = 0, e = modifiable->totalSize(); i < e; i++) {
            const auto &candidate = modifiable->candidateFromAll(i);
            if (static_cast<CandidateWord *>(this) == &candidate) {
                idx = i;
            } else {
                if (!dupIndex && word_ == candidate.text().toString()) {
                    dupIndex = i;
                }
            }
        }
        if (idx >= 0 && (dupIndex || word_.empty())) {
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - timestamp_)
                    .count();
            // If user make cloudpinyin to be the default one, we should prefer
            // to move the dup to current.
            if (idx == 0) {
                if (dupIndex) {
                    modifiable->remove(0);
                    modifiable->move(dupIndex.value() - 1, 0);
                } else {
                    // result is empty.
                    // Remove empty.
                    modifiable->remove(0);
                }

            } else {
                if (!keep_ && ms <= LOADING_TIME_QUICK_THRESHOLD) {
                    modifiable->remove(idx);
                } else {
                    setText(fcitx::Text("\xe2\x98\x81"));
                    word_ = std::string();
                    setPlaceHolder(!keep_);
                }
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
    bool keep_;
};

#endif // _CLOUDPINYIN_CLOUDPINYIN_PUBLIC_H_
