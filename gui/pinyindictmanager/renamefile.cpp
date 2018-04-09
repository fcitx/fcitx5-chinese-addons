//
// Copyright (C) 2018~2018 by CSSlayer
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
        emit message(QMessageBox::Critical, _("Converter crashed."));
        return;
    }
    emit finished(result);
}

} // namespace fcitx
