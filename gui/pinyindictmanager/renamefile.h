/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_RENAMEFILE_H_
#define _PINYINDICTMANAGER_RENAMEFILE_H_

#include "pipelinejob.h"
#include <QFutureWatcher>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QtConcurrent>

namespace fcitx {

class RenameFile : public PipelineJob {
    Q_OBJECT
public:
    explicit RenameFile(const QString &from, const QString &to,
                        QObject *parent = nullptr);
    void start() override;
    void abort() override;
    void cleanUp() override;

private Q_SLOTS:
    void emitFinished(bool result);

private:
    QString from_, to_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_RENAMEFILE_H_
