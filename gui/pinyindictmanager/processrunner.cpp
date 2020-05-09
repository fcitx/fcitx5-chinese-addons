/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "processrunner.h"
#include "guicommon.h"
#include "log.h"
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>

namespace fcitx {

ProcessRunner::ProcessRunner(const QString &bin, const QStringList &args,
                             const QString &file, QObject *parent)
    : PipelineJob(parent), bin_(bin), args_(args), file_(file) {
    connect(&process_,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &ProcessRunner::processFinished);
}

void ProcessRunner::start() {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
    }

    qCDebug(dictmanager) << bin_ << args_;

    process_.start(bin_, args_);
    process_.closeReadChannel(QProcess::StandardError);
    process_.closeReadChannel(QProcess::StandardOutput);
}

void ProcessRunner::abort() { process_.kill(); }

void ProcessRunner::cleanUp() { QFile::remove(file_); }

void ProcessRunner::processFinished(int exitCode, QProcess::ExitStatus status) {
    if (status == QProcess::CrashExit) {
        emit message(QMessageBox::Critical, _("Converter crashed."));
        emit finished(false);
        return;
    }

    if (exitCode != 0) {
        emit message(QMessageBox::Warning, _("Convert failed."));
        emit finished(false);
        return;
    }

    emit finished(true);
}

} // namespace fcitx
