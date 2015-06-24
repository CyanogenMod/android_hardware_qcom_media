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

#ifndef DASH_PACKET_SOURCE_H_

#define DASH_PACKET_SOURCE_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/MediaSource.h>
#include <utils/threads.h>
#include <utils/List.h>
#include "ATSParser.h"

namespace android {

struct ABuffer;

struct DashPacketSource : public MediaSource {
    DashPacketSource(const sp<MetaData> &meta);

    void setFormat(const sp<MetaData> &meta);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

    bool hasBufferAvailable(status_t *finalResult);

    // Returns the difference between the last and the first queued
    // presentation timestamps since the last discontinuity (if any).
    int64_t getBufferedDurationUs(status_t *finalResult);

    status_t nextBufferTime(int64_t *timeUs);

    void queueAccessUnit(const sp<ABuffer> &buffer);

    void queueDiscontinuity(
            ATSParser::DiscontinuityType type, const sp<AMessage> &extra);

    void signalEOS(status_t result);

    status_t dequeueAccessUnit(sp<ABuffer> *buffer);
    void updateFormat(const sp<MetaData> &meta);
    int getQueueSize();


protected:
    virtual ~DashPacketSource();

private:
    Mutex mLock;
    Condition mCondition;

    bool mIsAudio;
    sp<MetaData> mFormat;
    List<sp<ABuffer> > mBuffers;
    status_t mEOSResult;
    int mLogLevel;

    bool wasFormatChange(int32_t discontinuityType) const;

    DISALLOW_EVIL_CONSTRUCTORS(DashPacketSource);
};


}  // namespace android

#endif  // DASH_PACKET_SOURCE_H_
