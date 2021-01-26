/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
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

public Q_SLOTS:
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
