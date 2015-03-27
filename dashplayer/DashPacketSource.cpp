/*
 *Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *Not a Contribution, Apache license notifications and license are retained
 *for attribution purposes only.
 *Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DashPacketSource.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <utils/Vector.h>

namespace android {

DashPacketSource::DashPacketSource(const sp<MetaData> &meta)
    : mIsAudio(false),
      mFormat(meta),
      mEOSResult(OK),
      mStreamPID(0),
      mProgramPID(0),
      mFirstPTS(0) {
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if (!strncasecmp("audio/", mime, 6)) {
        mIsAudio = true;
    }
}

void DashPacketSource::setFormat(const sp<MetaData> &meta) {
    Mutex::Autolock autoLock(mLock);
    CHECK(mFormat == NULL);
    mFormat = meta;
}

void DashPacketSource::updateFormat(const sp<MetaData> &meta) {
    Mutex::Autolock autoLock(mLock);
    mFormat = meta;
}

DashPacketSource::~DashPacketSource() {
}

status_t DashPacketSource::start(MetaData *params) {
    return OK;
}

status_t DashPacketSource::stop() {
    return OK;
}

void DashPacketSource::setStreamInfo(unsigned streamPID, unsigned programPID, uint64_t firstPTS){
    mStreamPID = streamPID;
    mProgramPID = programPID;
    mFirstPTS = firstPTS;
}

status_t DashPacketSource::getStreamInfo(unsigned& streamPID, unsigned& programPID, uint64_t& firstPTS){
    streamPID = mStreamPID;
    programPID = mProgramPID;
    firstPTS = mFirstPTS;
    return OK;
}

sp<MetaData> DashPacketSource::getFormat() {
    Mutex::Autolock autoLock(mLock);
    return mFormat;
}

status_t DashPacketSource::dequeueAccessUnit(sp<ABuffer> *buffer) {
    buffer->clear();

    Mutex::Autolock autoLock(mLock);
    while (mEOSResult == OK && mBuffers.empty()) {
        mCondition.wait(mLock);
    }

    if (!mBuffers.empty()) {
        *buffer = *mBuffers.begin();
        mBuffers.erase(mBuffers.begin());

        int32_t discontinuity;
        if ((*buffer)->meta()->findInt32("discontinuity", &discontinuity)) {
            if (wasFormatChange(discontinuity)) {
                mFormat.clear();
            }

            return INFO_DISCONTINUITY;
        }

        return OK;
    }

    return mEOSResult;
}

status_t DashPacketSource::read(
        MediaBuffer **out, const ReadOptions *) {
    *out = NULL;

    Mutex::Autolock autoLock(mLock);
    while (mEOSResult == OK && mBuffers.empty()) {
        mCondition.wait(mLock);
    }

    if (!mBuffers.empty()) {
        const sp<ABuffer> buffer = *mBuffers.begin();
        mBuffers.erase(mBuffers.begin());

        int32_t discontinuity;
        if (buffer->meta()->findInt32("discontinuity", &discontinuity)) {
            if (wasFormatChange(discontinuity)) {
                mFormat.clear();
            }

            return INFO_DISCONTINUITY;
        } else {
            int64_t timeUs;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            MediaBuffer *mediaBuffer = new MediaBuffer(buffer);

            mediaBuffer->meta_data()->setInt64(kKeyTime, timeUs);

            *out = mediaBuffer;
            return OK;
        }
    }

    return mEOSResult;
}

bool DashPacketSource::wasFormatChange(
        int32_t discontinuityType) const {
    if (mIsAudio) {
        return (discontinuityType & ATSParser::DISCONTINUITY_AUDIO_FORMAT) != 0;
    }

    return (discontinuityType & ATSParser::DISCONTINUITY_VIDEO_FORMAT) != 0;
}

void DashPacketSource::queueAccessUnit(const sp<ABuffer> &buffer) {
    int32_t damaged;
    if (buffer->meta()->findInt32("damaged", &damaged) && damaged) {
        // LOG(VERBOSE) << "discarding damaged AU";
        return;
    }

    int64_t timeUs;
    CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
    ALOGV("queueAccessUnit timeUs=%lld us (%.2f secs)", timeUs, timeUs / 1E6);

    Mutex::Autolock autoLock(mLock);
    mBuffers.push_back(buffer);
    ALOGV("@@@@:: DashPacketSource --> size is %d ",mBuffers.size() );
    mCondition.signal();
}

int DashPacketSource::getQueueSize() {
    return mBuffers.size();
}

void DashPacketSource::queueDiscontinuity(
        ATSParser::DiscontinuityType type,
        const sp<AMessage> &extra) {
    Mutex::Autolock autoLock(mLock);

    if (type == ATSParser::DISCONTINUITY_TIME) {
        ALOGI("Flushing all Access units for seek");
        mBuffers.clear();
        mEOSResult = OK;
        mCondition.signal();
        return;
    }

    // Leave only discontinuities in the queue.
    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end()) {
        sp<ABuffer> oldBuffer = *it;

        int32_t oldDiscontinuityType;
        if (!oldBuffer->meta()->findInt32(
                    "discontinuity", &oldDiscontinuityType)) {
            it = mBuffers.erase(it);
            continue;
        }

        ++it;
    }

    mEOSResult = OK;

    sp<ABuffer> buffer = new ABuffer(0);
    buffer->meta()->setInt32("discontinuity", static_cast<int32_t>(type));
    buffer->meta()->setMessage("extra", extra);

    mBuffers.push_back(buffer);
    mCondition.signal();
}

void DashPacketSource::signalEOS(status_t result) {
    CHECK(result != OK);

    Mutex::Autolock autoLock(mLock);
    mEOSResult = result;
    mCondition.signal();
}

bool DashPacketSource::hasBufferAvailable(status_t *finalResult) {
    Mutex::Autolock autoLock(mLock);
    if (!mBuffers.empty()) {
        return true;
    }

    *finalResult = mEOSResult;
    return false;
}

int64_t DashPacketSource::getBufferedDurationUs(status_t *finalResult) {
    Mutex::Autolock autoLock(mLock);

    *finalResult = mEOSResult;

    if (mBuffers.empty()) {
        return 0;
    }

    int64_t time1 = -1;
    int64_t time2 = -1;

    List<sp<ABuffer> >::iterator it = mBuffers.begin();
    while (it != mBuffers.end()) {
        const sp<ABuffer> &buffer = *it;

        int64_t timeUs;
        if (buffer->meta()->findInt64("timeUs", &timeUs)) {
            if (time1 < 0) {
                time1 = timeUs;
            }

            time2 = timeUs;
        } else {
            // This is a discontinuity, reset everything.
            time1 = time2 = -1;
        }

        ++it;
    }

    return time2 - time1;
}

status_t DashPacketSource::nextBufferTime(int64_t *timeUs) {
    *timeUs = 0;

    Mutex::Autolock autoLock(mLock);

    if (mBuffers.empty()) {
        return mEOSResult != OK ? mEOSResult : -EWOULDBLOCK;
    }

    sp<ABuffer> buffer = *mBuffers.begin();
    CHECK(buffer->meta()->findInt64("timeUs", timeUs));
    return OK;
}

status_t DashPacketSource::nextBufferIsSync(bool* isSyncFrame) {
    Mutex::Autolock autoLock(mLock);
    CHECK(isSyncFrame != NULL);

    if (mBuffers.empty()) {
        return mEOSResult != OK ? mEOSResult : -EWOULDBLOCK;
    }

    sp<ABuffer> buffer = *mBuffers.begin();

    *isSyncFrame = false;
    int32_t value = 0;
    if (buffer->meta()->findInt32("isSync", &value) && (value == 1)) {
       *isSyncFrame = true;
    }
    return OK;
}

}  // namespace android
