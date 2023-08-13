/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "editordialog.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {
EditorDialog::EditorDialog(QWidget *parent) : QDialog(parent) { setupUi(this); }

EditorDialog::~EditorDialog() {}

void EditorDialog::setKey(const QString &s) { keyLineEdit->setText(s); }

void EditorDialog::setValue(const QString &s) { valueLineEdit->setText(s); }

void EditorDialog::setOrder(int order) { orderSpinBox->setValue(order); }

QString EditorDialog::key() const { return keyLineEdit->text(); }

QString EditorDialog::value() const { return valueLineEdit->text(); }

int EditorDialog::order() const { return orderSpinBox->value(); }
} // namespace fcitx
