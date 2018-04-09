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

signals:
    void finished(bool success);
    void message(QMessageBox::Icon icon, const QString &message);
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_PIPELINEJOB_H_
