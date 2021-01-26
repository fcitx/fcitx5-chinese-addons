/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <QTemporaryFile>

#include "filedownloader.h"
#include "guicommon.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {

FileDownloader::FileDownloader(const QUrl &url, const QString &dest,
                               QObject *parent)
    : PipelineJob(parent), url_(url), file_(dest), progress_(0) {}

void FileDownloader::start() {
    if (!file_.open(QIODevice::WriteOnly)) {
        Q_EMIT message(QMessageBox::Warning,
                       _("Create temporary file failed."));
        Q_EMIT finished(false);
        return;
    }
    Q_EMIT message(QMessageBox::Information, _("Temporary file created."));

    QNetworkRequest request(url_);
    request.setRawHeader(
        "Referer",
        QString("%1://%2").arg(url_.scheme()).arg(url_.host()).toLatin1());
    reply_ = nam_.get(request);

    if (!reply_) {
        Q_EMIT message(QMessageBox::Warning, _("Failed to create request."));
        Q_EMIT finished(false);
        return;
    }
    Q_EMIT message(QMessageBox::Information, _("Download started."));

    connect(reply_, &QNetworkReply::readyRead, this,
            &FileDownloader::readyToRead);
    connect(reply_, &QNetworkReply::finished, this,
            &FileDownloader::downloadFinished);
    connect(reply_, &QNetworkReply::downloadProgress, this,
            &FileDownloader::updateProgress);
}

void FileDownloader::abort() {
    reply_->abort();
    delete reply_;
    reply_ = nullptr;
}

void FileDownloader::cleanUp() { file_.remove(); }

void FileDownloader::readyToRead() { file_.write(reply_->readAll()); }

void FileDownloader::updateProgress(qint64 downloaded, qint64 total) {
    if (total <= 0) {
        return;
    }

    int percent = (int)(((qreal)downloaded / total) * 100);
    if (percent > 100) {
        percent = 100;
    }
    if (percent >= progress_ + 10) {
        Q_EMIT message(QMessageBox::Information,
                       QString::fromUtf8(_("%1% Downloaded.")).arg(percent));
        progress_ = percent;
    }
}

void FileDownloader::downloadFinished() {
    file_.close();
    Q_EMIT message(QMessageBox::Information, _("Download Finished"));
    Q_EMIT finished(true);
}

} // namespace fcitx
