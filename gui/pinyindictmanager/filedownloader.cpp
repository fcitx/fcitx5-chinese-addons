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
        emit message(QMessageBox::Warning, _("Create temporary file failed."));
        emit finished(false);
        return;
    } else {
        emit message(QMessageBox::Information, _("Temporary file created."));
    }

    QNetworkRequest request(url_);
    request.setRawHeader(
        "Referer",
        QString("%1://%2").arg(url_.scheme()).arg(url_.host()).toLatin1());
    reply_ = nam_.get(request);

    if (!reply_) {
        emit message(QMessageBox::Warning, _("Failed to create request."));
        emit finished(false);
        return;
    }
    emit message(QMessageBox::Information, _("Download started."));

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
        emit message(QMessageBox::Information,
                     QString::fromUtf8(_("%1% Downloaded.")).arg(percent));
        progress_ = percent;
    }
}

void FileDownloader::downloadFinished() {
    file_.close();
    emit message(QMessageBox::Information, _("Download Finished"));
    emit finished(true);
}

} // namespace fcitx
