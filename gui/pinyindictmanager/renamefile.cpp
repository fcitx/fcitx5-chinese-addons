/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "renamefile.h"
#include "guicommon.h"
#include "log.h"
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>

namespace fcitx {

RenameFile::RenameFile(const QString &from, const QString &to, QObject *parent)
    : PipelineJob(parent), from_(from), to_(to) {}

void RenameFile::start() {
    bool result = ::rename(from_.toLocal8Bit().constData(),
                           to_.toLocal8Bit().constData()) >= 0;
    QMetaObject::invokeMethod(this, "emitFinished", Qt::QueuedConnection,
                              Q_ARG(bool, result));
}

void RenameFile::abort() {}

void RenameFile::cleanUp() {}

void RenameFile::emitFinished(bool result) {
    if (!result) {
        Q_EMIT message(QMessageBox::Critical, _("Converter crashed."));
        return;
    }
    Q_EMIT finished(result);
}

} // namespace fcitx
