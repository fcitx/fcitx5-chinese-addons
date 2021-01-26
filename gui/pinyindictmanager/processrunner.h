/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_PROCESSRUNNER_H_
#define _PINYINDICTMANAGER_PROCESSRUNNER_H_

#include "pipelinejob.h"
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>

namespace fcitx {

class ProcessRunner : public PipelineJob {
    Q_OBJECT
public:
    explicit ProcessRunner(const QString &bin, const QStringList &args,
                           const QString &file, QObject *parent = nullptr);
    void start() override;
    void abort() override;
    void cleanUp() override;

private Q_SLOTS:
    void processFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess process_;
    QString bin_;
    QStringList args_;
    QString file_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_PROCESSRUNNER_H_
