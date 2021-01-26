/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_PIPELINE_H_
#define _PINYINDICTMANAGER_PIPELINE_H_

#include "pipelinejob.h"
#include <QObject>
#include <QVector>

namespace fcitx {

class Pipeline : public QObject {
    Q_OBJECT
public:
    Pipeline(QObject *parent = nullptr);

    void addJob(PipelineJob *job);
    void start();
    void abort();
    void reset();

Q_SIGNALS:
    void finished(bool);
    void messages(const QString &message);

private:
    void startNext();
    void emitFinished(bool);

    QVector<PipelineJob *> jobs_;
    int index_ = -1;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_PIPELINE_H_
