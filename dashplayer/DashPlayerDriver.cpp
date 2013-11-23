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

//#define LOG_NDEBUG 0
#define LOG_TAG "DashPlayerDriver"
#include <utils/Log.h>

#include "DashPlayerDriver.h"

#include "DashPlayer.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>

namespace android {

DashPlayerDriver::DashPlayerDriver()
    : mResetInProgress(false),
      mDurationUs(-1),
      mPositionUs(-1),
      mNumFramesTotal(0),
      mNumFramesDropped(0),
      mLooper(new ALooper),
      mState(UNINITIALIZED),
      mAtEOS(false),
      mStartupSeekTimeUs(-1) {
    mLooper->setName("DashPlayerDriver Looper");

    mLooper->start(
            false, /* runOnCallingThread */
            true,  /* canCallJava */
            PRIORITY_AUDIO);

    mPlayer = new DashPlayer;
    mLooper->registerHandler(mPlayer);

    mPlayer->setDriver(this);
}

DashPlayerDriver::~DashPlayerDriver() {
    mLooper->stop();
    mLooper->unregisterHandler(mPlayer->id());
}

status_t DashPlayerDriver::initCheck() {
    return OK;
}

status_t DashPlayerDriver::setUID(uid_t uid) {
    mPlayer->setUID(uid);

    return OK;
}

status_t DashPlayerDriver::setDataSource(
        const char *url, const KeyedVector<String8, String8> *headers) {
    CHECK_EQ((int)mState, (int)UNINITIALIZED);

    mPlayer->setDataSource(url, headers);

    mState = STOPPED;

    return OK;
}

status_t DashPlayerDriver::setDataSource(int fd, int64_t offset, int64_t length) {
    CHECK_EQ((int)mState, (int)UNINITIALIZED);

    mPlayer->setDataSource(fd, offset, length);

    mState = STOPPED;

    return OK;
}

status_t DashPlayerDriver::setDataSource(const sp<IStreamSource> &source) {
    CHECK_EQ((int)mState, (int)UNINITIALIZED);

    mPlayer->setDataSource(source);

    mState = STOPPED;

    return OK;
}

#ifdef ANDROID_JB_MR2
status_t DashPlayerDriver::setVideoSurfaceTexture(
        const sp<IGraphicBufferProducer> &bufferProducer) {
    mPlayer->setVideoSurfaceTexture(bufferProducer);

    return OK;
}
#else
status_t DashPlayerDriver::setVideoSurfaceTexture(
        const sp<ISurfaceTexture> &surfaceTexture) {
    mPlayer->setVideoSurfaceTexture(surfaceTexture);

    return OK;
}
#endif

status_t DashPlayerDriver::prepare() {
    sendEvent(MEDIA_SET_VIDEO_SIZE, 0, 0);
    return OK;
}

status_t DashPlayerDriver::prepareAsync() {
    status_t err = UNKNOWN_ERROR;
    if (mPlayer != NULL) {
        err = mPlayer->prepareAsync();
    }

    if (err == OK) {
        err = prepare();
        notifyListener(MEDIA_PREPARED);
    } else if (err == -EWOULDBLOCK) {
        // this case only happens for DASH
        return OK;
    }
    return err;
}

status_t DashPlayerDriver::start() {
    switch (mState) {
        case UNINITIALIZED:
            return INVALID_OPERATION;
        case STOPPED:
        {
            mAtEOS = false;
            mPlayer->start();

            if (mStartupSeekTimeUs >= 0) {
                if (mStartupSeekTimeUs == 0) {
                    notifySeekComplete();
                } else {
                    mPlayer->seekToAsync(mStartupSeekTimeUs);
                }

                mStartupSeekTimeUs = -1;
            }

            break;
        }
        case PLAYING:
            return OK;
        default:
        {
            CHECK_EQ((int)mState, (int)PAUSED);
            if (mAtEOS){
                seekTo(0);
            }
            mPlayer->resume();
            break;
        }
    }

    mState = PLAYING;

    return OK;
}

status_t DashPlayerDriver::stop() {
    return pause();
}

status_t DashPlayerDriver::pause() {
    switch (mState) {
        case UNINITIALIZED:
            return INVALID_OPERATION;
        case STOPPED:
            return OK;
        case PLAYING:
            mPlayer->pause();
            break;
        default:
        {
            CHECK_EQ((int)mState, (int)PAUSED);
            return OK;
        }
    }

    mState = PAUSED;

    return OK;
}

bool DashPlayerDriver::isPlaying() {
    return mState == PLAYING && !mAtEOS;
}

status_t DashPlayerDriver::seekTo(int msec) {
    int64_t seekTimeUs = msec * 1000ll;

    switch (mState) {
        case UNINITIALIZED:
            return INVALID_OPERATION;
        case STOPPED:
        {
            mStartupSeekTimeUs = seekTimeUs;
            break;
        }
        case PLAYING:
        case PAUSED:
        {
            mAtEOS = false;
            mPlayer->seekToAsync(seekTimeUs);
            break;
        }

        default:
            TRESPASS();
            break;
    }

    return OK;
}

status_t DashPlayerDriver::getCurrentPosition(int *msec) {
    Mutex::Autolock autoLock(mLock);

    if (mPositionUs < 0) {
        *msec = 0;
    } else {
        *msec = (mPositionUs + 500ll) / 1000;
    }

    return OK;
}

status_t DashPlayerDriver::getDuration(int *msec) {
    Mutex::Autolock autoLock(mLock);

    if (mDurationUs < 0) {
        *msec = 0;
    } else {
        *msec = (mDurationUs + 500ll) / 1000;
    }

    return OK;
}

status_t DashPlayerDriver::reset() {
    Mutex::Autolock autoLock(mLock);
    mResetInProgress = true;

    mPlayer->resetAsync();

    while (mResetInProgress) {
        mCondition.wait(mLock);
    }

    mDurationUs = -1;
    mPositionUs = -1;
    mState = UNINITIALIZED;
    mStartupSeekTimeUs = -1;

    return OK;
}

status_t DashPlayerDriver::setLooping(int loop) {
    return INVALID_OPERATION;
}

player_type DashPlayerDriver::playerType() {
    return NU_PLAYER;
}

status_t DashPlayerDriver::invoke(const Parcel &request, Parcel *reply) {
    return INVALID_OPERATION;
}

void DashPlayerDriver::setAudioSink(const sp<AudioSink> &audioSink) {
    mPlayer->setAudioSink(audioSink);
}

status_t DashPlayerDriver::setParameter(int key, const Parcel &request) {

    status_t err = UNKNOWN_ERROR;
    if (mPlayer != NULL)
    {
        err = mPlayer->setParameter(key, request);
    }
    return err;
}

status_t DashPlayerDriver::getParameter(int key, Parcel *reply) {

    status_t err = UNKNOWN_ERROR;
    if (mPlayer != NULL)
    {
        err = mPlayer->getParameter(key, reply);
    }
    return err;
}

status_t DashPlayerDriver::getMetadata(
        const media::Metadata::Filter& ids, Parcel *records) {
    return INVALID_OPERATION;
}

void DashPlayerDriver::notifyResetComplete() {
    Mutex::Autolock autoLock(mLock);
    CHECK(mResetInProgress);
    mResetInProgress = false;
    mCondition.broadcast();
}

void DashPlayerDriver::notifyDuration(int64_t durationUs) {
    Mutex::Autolock autoLock(mLock);
    mDurationUs = durationUs;
}

void DashPlayerDriver::notifyPosition(int64_t positionUs) {
    Mutex::Autolock autoLock(mLock);
    mPositionUs = positionUs;
}

void DashPlayerDriver::notifySeekComplete() {
    notifyListener(MEDIA_SEEK_COMPLETE);
}

void DashPlayerDriver::notifyFrameStats(
        int64_t numFramesTotal, int64_t numFramesDropped) {
    Mutex::Autolock autoLock(mLock);
    mNumFramesTotal = numFramesTotal;
    mNumFramesDropped = numFramesDropped;
}

status_t DashPlayerDriver::dump(int fd, const Vector<String16> &args) const {
    if(mPlayer != NULL) {
      mPlayer->dump(fd, args);
    }
    return OK;
}

void DashPlayerDriver::notifyListener(int msg, int ext1, int ext2, const Parcel *obj) {
    if (msg == MEDIA_PLAYBACK_COMPLETE || msg == MEDIA_ERROR) {
        mAtEOS = true;
        if(msg == MEDIA_PLAYBACK_COMPLETE){
            pause();
        }
    }

    sendEvent(msg, ext1, ext2, obj);
}

}  // namespace android
