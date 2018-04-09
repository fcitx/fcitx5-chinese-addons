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

private slots:
    void emitFinished(bool result);

private:
    QString from_, to_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_RENAMEFILE_H_
