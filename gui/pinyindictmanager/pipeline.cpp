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
#include "pipeline.h"

namespace fcitx {

Pipeline::Pipeline(QObject *parent) : QObject(parent) {}

void Pipeline::addJob(PipelineJob *job) {
    job->setParent(this);
    jobs_.push_back(job);
    connect(job, &PipelineJob::finished, this, [this](bool success) {
        if (success) {
            startNext();
        } else {
            emitFinished(false);
        }
    });
}

void Pipeline::abort() {
    if (index_ < 0) {
        return;
    }
    jobs_[index_]->abort();
    index_ = -1;
}

void Pipeline::reset() {
    abort();
    for (auto job : jobs_) {
        delete job;
    }
    jobs_.clear();
}

void Pipeline::start() {
    Q_ASSERT(!jobs_.isEmpty());

    index_ = -1;
    startNext();
}

void Pipeline::startNext() {
    if (index_ + 1 == jobs_.size()) {
        emitFinished(true);
        return;
    }
    index_ += 1;
    jobs_[index_]->start();
}

void Pipeline::emitFinished(bool result) {
    for (auto job : jobs_) {
        job->cleanUp();
    }
    emit finished(result);
}

} // namespace fcitx
