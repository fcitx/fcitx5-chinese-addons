/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphraseeditor.h"

namespace fcitx {

CustomPhraseEditor::CustomPhraseEditor(QWidget *parent)
    : FcitxQtConfigUIWidget(parent) {
    setupUi(this);

    connect(addButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::addPhrase);
    connect(removeButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::removePhrase);
    connect(clearButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::clear);
}

void CustomPhraseEditor::load() {}

void CustomPhraseEditor::save() {}

QString CustomPhraseEditor::title() { return _("Custom Phrase Editor"); }

bool CustomPhraseEditor::asyncSave() { return true; }

void CustomPhraseEditor::reload() {
    saveSubConfig("fcitx://config/addon/pinyin/customphraseeditor");
}

void CustomPhraseEditor::addPhrase() {}

void CustomPhraseEditor::removePhrase() {}

void CustomPhraseEditor::clear() {}

void CustomPhraseEditor::importFromFile() {}

} // namespace fcitx
