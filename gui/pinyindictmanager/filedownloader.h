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
#ifndef _PINYINDICTMANAGER_FILEDOWNLOADER_H_
#define _PINYINDICTMANAGER_FILEDOWNLOADER_H_

#include "pipelinejob.h"
#include <QByteArray>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTemporaryFile>

namespace fcitx {

class FileDownloader : public PipelineJob {
    Q_OBJECT
public:
    explicit FileDownloader(const QUrl &url, const QString &dest,
                            QObject *parent = nullptr);

    void start() override;
    void abort() override;
    void cleanUp() override;

public slots:
    void readyToRead();
    void downloadFinished();
    void updateProgress(qint64, qint64);

private:
    QUrl url_;
    QFile file_;
    QNetworkAccessManager nam_;
    QNetworkReply *reply_ = nullptr;
    int progress_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_FILEDOWNLOADER_H_
