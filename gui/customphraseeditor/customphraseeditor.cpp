/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphraseeditor.h"
#include "customphrasemodel.h"
#include "editordialog.h"
#include <QLineEdit>
#include <QObject>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcntl.h>
#include <qdesktopservices.h>
#include <qfilesystemwatcher.h>
#include <qmessagebox.h>
#include <qnamespace.h>

namespace fcitx {

class OrderDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    OrderDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        QSpinBox *spinBox = new QSpinBox(parent);
        spinBox->setFrame(false);
        spinBox->setMinimum(1);
        spinBox->setMaximum(100);
        return spinBox;
    }
    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override {
        int value = index.model()->data(index, Qt::EditRole).toInt();

        QSpinBox *spinBox = static_cast<QSpinBox *>(editor);
        spinBox->setValue(value);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        QSpinBox *spinBox = static_cast<QSpinBox *>(editor);
        spinBox->interpretText();
        int value = spinBox->value();

        model->setData(index, value, Qt::EditRole);
    }
};

class KeyDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    KeyDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        auto *lineEdit = new QLineEdit(parent);
        auto *validator = new QRegularExpressionValidator(lineEdit);

        QRegularExpression reg("[a-zA-Z]+");
        validator->setRegularExpression(reg);
        lineEdit->setValidator(validator);
        return lineEdit;
    }
    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override {
        auto value = index.model()->data(index, Qt::EditRole).toString();

        auto *lineEdit = static_cast<QLineEdit *>(editor);
        lineEdit->setText(value);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        QLineEdit *lineEdit = static_cast<QLineEdit *>(editor);
        if (lineEdit->hasAcceptableInput()) {
            model->setData(index, lineEdit->text(), Qt::EditRole);
        }
    }
};

class ValueDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    ValueDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        auto *textEdit = new QTextEdit(parent);
        textEdit->setAcceptRichText(false);
        return textEdit;
    }
    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override {
        auto value = index.model()->data(index, Qt::EditRole).toString();

        auto *textEdit = static_cast<QTextEdit *>(editor);
        textEdit->setPlainText(value);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        auto *textEdit = static_cast<QTextEdit *>(editor);
        model->setData(index, textEdit->toPlainText(), Qt::EditRole);
    }
};

CustomPhraseEditor::CustomPhraseEditor(QWidget *parent)
    : FcitxQtConfigUIWidget(parent), model_(new CustomPhraseModel(this)),
      userFile_(QString::fromStdString(stringutils::joinPath(
          StandardPath::global().userDirectory(StandardPath::Type::PkgData),
          customPhraseFileName))) {
    setupUi(this);

    connect(addButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::addPhrase);
    connect(removeButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::removePhrase);
    connect(clearButton_, &QPushButton::clicked, this,
            &CustomPhraseEditor::clear);
    connect(externalEditor_, &QPushButton::clicked, this,
            &CustomPhraseEditor::openExternal);
    connect(helpButton_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(
            this, QString(_("Usage")),
            QString::fromStdString(customPhraseHelpMessage()));
    });
    connectFileWatcher();

    tableView_->setModel(model_);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Enable, QHeaderView::ResizeToContents);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Key, QHeaderView::ResizeToContents);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Phrase, QHeaderView::Stretch);
    tableView_->horizontalHeader()->setSectionResizeMode(
        CustomPhraseModel::Column_Order, QHeaderView::ResizeToContents);
    tableView_->setItemDelegateForColumn(CustomPhraseModel::Column_Key,
                                         new KeyDelegate(this));
    tableView_->setItemDelegateForColumn(CustomPhraseModel::Column_Phrase,
                                         new ValueDelegate(this));
    tableView_->setItemDelegateForColumn(CustomPhraseModel::Column_Order,
                                         new OrderDelegate(this));

    connect(model_, &CustomPhraseModel::needSaveChanged, this,
            &CustomPhraseEditor::changed);
    load();
}

void CustomPhraseEditor::load() { model_->load(); }

void CustomPhraseEditor::save() {
    disconnectFileWatcher();
    QFutureWatcher<bool> *futureWatcher = model_->save();
    connect(futureWatcher, &QFutureWatcherBase::finished, this, [this]() {
        saveFinished();
        connectFileWatcher();
    });
}

QString CustomPhraseEditor::title() { return _("Custom Phrase Editor"); }

bool CustomPhraseEditor::asyncSave() { return true; }

void CustomPhraseEditor::addPhrase() {
    EditorDialog *dialog = new EditorDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->open();
    connect(dialog, &QDialog::accepted, this,
            &CustomPhraseEditor::addPhraseAccepted);
}

void CustomPhraseEditor::addPhraseAccepted() {
    const EditorDialog *dialog =
        qobject_cast<const EditorDialog *>(QObject::sender());

    model_->addItem(dialog->key(), dialog->value(), dialog->order(), true);
    QModelIndex last = model_->index(model_->rowCount() - 1, 0);
    tableView_->setCurrentIndex(last);
    tableView_->scrollTo(last);
}

void CustomPhraseEditor::removePhrase() {
    if (!tableView_->currentIndex().isValid())
        return;
    int row = tableView_->currentIndex().row();
    model_->deleteItem(row);
}

void CustomPhraseEditor::clear() { model_->deleteAllItem(); }

void CustomPhraseEditor::openExternal() {
    disconnectFileWatcher();
    model_->save()->waitForFinished();
    connectFileWatcher();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userFile_));
}

void CustomPhraseEditor::updated() {
    disconnectFileWatcher();
    if (QMessageBox::Yes ==
        QMessageBox::question(
            this, _("File updated"),
            _("Do you want to reload custom phrase from disk?"))) {
        load();
        // Trigger a reload on fcitx
        saveSubConfig("fcitx://config/addon/pinyin/customphrase");
    } else {
        Q_EMIT changed(true);
    }
    connectFileWatcher();
}

void CustomPhraseEditor::disconnectFileWatcher() {
    disconnect(&watcher_, &QFileSystemWatcher::fileChanged, this,
               &CustomPhraseEditor::updated);
}
void CustomPhraseEditor::connectFileWatcher() {
    watcher_.removePath(userFile_);
    watcher_.addPath(userFile_);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
            &CustomPhraseEditor::updated, Qt::UniqueConnection);
}

} // namespace fcitx

#include "customphraseeditor.moc"