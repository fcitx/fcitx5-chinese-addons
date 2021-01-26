/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_PIPELINEJOB_H_
#define _PINYINDICTMANAGER_PIPELINEJOB_H_

#include <QMessageBox>
#include <QObject>

namespace fcitx {

class PipelineJob : public QObject {
    Q_OBJECT
public:
    PipelineJob(QObject *parent = nullptr);

    virtual void start() = 0;
    virtual void abort() = 0;
    virtual void cleanUp() = 0;

Q_SIGNALS:
    void finished(bool success);
    void message(QMessageBox::Icon icon, const QString &message);
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_PIPELINEJOB_H_
