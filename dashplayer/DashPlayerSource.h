/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef DASHPLAYER_SOURCE_H_

#define DASHPLAYER_SOURCE_H_

#include "DashPlayer.h"
//#include <media/stagefright/MediaDebug.h>

namespace android {

struct ABuffer;

struct DashPlayer::Source : public RefBase {
    Source() {}

    virtual void start() = 0;
    virtual void stop() {}

    // Returns OK iff more data was available,
    // an error or ERROR_END_OF_STREAM if not.
    virtual status_t feedMoreTSData() = 0;

    virtual sp<MetaData> getFormat(int audio) = 0;

    virtual status_t dequeueAccessUnit(
            int track, sp<ABuffer> *accessUnit) = 0;

    virtual status_t getDuration(int64_t *durationUs) {
        return INVALID_OPERATION;
    }

    virtual status_t seekTo(int64_t seekTimeUs) {
        return INVALID_OPERATION;
    }

    virtual bool isSeekable() {
        return false;
    }

    virtual status_t getNewSeekTime(int64_t* newSeek) {
        return INVALID_OPERATION;
    }

    virtual status_t prepareAsync() {
        return INVALID_OPERATION;
    }

    virtual bool isPrepareDone() {
        return INVALID_OPERATION;
    }

    virtual status_t getParameter(int key, void **data, size_t *size) {
        return INVALID_OPERATION;
    }

    virtual status_t setParameter(int key, void *data, size_t size) {
        return INVALID_OPERATION;
    }
    virtual void notifyRenderingPosition(int64_t nRenderingTS){}

    virtual status_t setupSourceData(const sp<AMessage> &msg, int iTrack){
        return INVALID_OPERATION;
    }
    virtual status_t postNextTextSample(sp<ABuffer> accessUnit,const sp<AMessage> &msg,int iTrack) {
        return INVALID_OPERATION;
    }

    virtual status_t getMediaPresence(bool &audio, bool &video, bool &text) {
       return INVALID_OPERATION;
    }

    virtual void pause() {
        ALOGE("Pause called on Wrong DataSource.. Please check !!!");
        //CHECK(false);
    }

    virtual void resume() {
        ALOGE("Resume called on Wrong DataSource.. Please check !!!");
        //CHECK(false);
    }

protected:
    virtual ~Source() {}

private:
    DISALLOW_EVIL_CONSTRUCTORS(Source);
};

}  // namespace android

#endif  // DASHPLAYER_SOURCE_H_

