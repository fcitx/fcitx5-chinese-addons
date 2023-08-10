/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphraseeditor.h"
#include "customphrasemodel.h"
#include <QStyledItemDelegate>

namespace fcitx {

class Delegate : public QStyledItemDelegate {
    Q_OBJECT
};

CustomPhraseEditor::CustomPhraseEditor(QWidget *parent)
    : FcitxQtConfigUIWidget(parent), model_(new CustomPhraseModel(this)) {
    setupUi(this);

    connect(addButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::addPhrase);
    connect(removeButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::removePhrase);
    connect(clearButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::clear);

    tableView_->setModel(model_);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Enable, QHeaderView::ResizeToContents);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Key, QHeaderView::ResizeToContents);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Phrase, QHeaderView::Stretch);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Order, QHeaderView::ResizeToContents);

    load();
}

void CustomPhraseEditor::load() { model_->load(); }

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

#include "customphraseeditor.moc"