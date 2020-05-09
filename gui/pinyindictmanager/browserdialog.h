/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_BROWSERDIALOG_H_
#define _PINYINDICTMANAGER_BROWSERDIALOG_H_

#include "config.h"
#ifdef USE_WEBKIT
#include "ui_browserdialog_webkit.h"
#else
#include "ui_browserdialog.h"
#endif
#include <QDialog>
#include <QMessageBox>
#include <QUrl>

namespace fcitx {

class WebPage;

class BrowserDialog : public QDialog, public Ui::BrowserDialog {
    friend class WebPage;
    Q_OBJECT
public:
    explicit BrowserDialog(QWidget *parent = nullptr);
    virtual ~BrowserDialog();
    const QUrl &url() const { return url_; }
    const QString &name() const { return name_; }

private:
    bool linkClicked(const QUrl &url);
    QString decodeName(const QByteArray &in);
    QString name_;
    QUrl url_;
    WebPage *page_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_BROWSERDIALOG_H_
