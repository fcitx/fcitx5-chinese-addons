/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
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
    for (auto *job : jobs_) {
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
    for (auto *job : jobs_) {
        job->cleanUp();
    }
    Q_EMIT finished(result);
}

} // namespace fcitx
