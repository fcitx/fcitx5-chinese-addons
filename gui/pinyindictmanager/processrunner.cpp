//
// Copyright (C) 2013~2018 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//
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
