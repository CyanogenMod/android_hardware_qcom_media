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
      mSetSurfaceInProgress(false),
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
         const sp<IMediaHTTPService> & /*httpService*/,
        const char *url, const KeyedVector<String8, String8> *headers) {
    CHECK_EQ((int)mState, (int)UNINITIALIZED);

    status_t ret = mPlayer->setDataSource(url, headers);

    mState = STOPPED;

    return ret;
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

status_t DashPlayerDriver::setVideoSurfaceTexture(
        const sp<IGraphicBufferProducer> &bufferProducer) {
    Mutex::Autolock autoLock(mLock);

    if (mResetInProgress) {
      return INVALID_OPERATION;
    }

    ALOGE("DashPlayerDriver::setVideoSurfaceTexture call and block");

    mSetSurfaceInProgress = true;

    mPlayer->setVideoSurfaceTexture(bufferProducer);

    while (mSetSurfaceInProgress) {
       mCondition.wait(mLock);
    }

  return OK;
}

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

status_t DashPlayerDriver::setLooping(int /*loop*/) {
    return INVALID_OPERATION;
}

player_type DashPlayerDriver::playerType() {
    return NU_PLAYER;
}

void DashPlayerDriver::setQCTimedTextListener(const bool val) {
  mPlayer->setQCTimedTextListener(val);
}

status_t DashPlayerDriver::invoke(const Parcel &request, Parcel *reply) {
   status_t ret = INVALID_OPERATION;

   if (reply == NULL) {
       ALOGE("reply is a NULL pointer");
       return BAD_VALUE;
    }

    int32_t methodId;
    ret = request.readInt32(&methodId);
    if (ret != OK) {
        ALOGE("Failed to retrieve the requested method to invoke");
        return ret;
    }

    switch (methodId) {
       case KEY_DASH_GET_ADAPTION_PROPERTIES:
        {
          ALOGV("calling KEY_DASH_GET_ADAPTION_PROPERTIES");
          ret = getParameter(methodId,reply);
          break;
        }
        case KEY_DASH_SET_ADAPTION_PROPERTIES:
        {
          ALOGV("calling KEY_DASH_SET_ADAPTION_PROPERTIES");
          int32_t val = 0;
          ret = setParameter(methodId,request);
          val = (ret == OK)? 1:0;
          reply->setDataPosition(0);
          reply->writeInt32(val);
          break;
       }
       case KEY_DASH_MPD_QUERY:
       {
         ALOGV("calling KEY_DASH_MPD_QUERY");
         ret = getParameter(methodId,reply);
         break;
       }
       case KEY_DASH_QOE_EVENT:
           ALOGV("calling KEY_DASH_QOE_EVENT");
           ret = setParameter(methodId,request);
           break;

       case KEY_DASH_QOE_PERIODIC_EVENT:
           ALOGV("calling KEY_DASH_QOE_PERIODIC_EVENT");
           ret = getParameter(methodId,reply);
           break;

       case KEY_DASH_REPOSITION_RANGE:
           ALOGV("calling KEY_DASH_REPOSITION_RANGE");
           ret = getParameter(methodId,reply);
           break;

       case KEY_DASH_SEEK_EVENT:
       {
          ALOGV("calling KEY_DASH_SEEK_EVENT seekTo()");
          int32_t msec;
          ret = request.readInt32(&msec);
          if (ret != OK)
          {
            ALOGE("Invoke: invalid seek value");
          }
          else
          {
            ret = seekTo(msec);
            int32_t val = (ret == OK)? 1:0;
            reply->setDataPosition(0);
            reply->writeInt32(val);
          }
          break;
       }

       case KEY_DASH_PAUSE_EVENT:
       {
          ALOGV("calling KEY_DASH_PAUSE_EVENT pause()");
          ret = pause();
          int32_t val = (ret == OK)? 1:0;
          reply->setDataPosition(0);
          reply->writeInt32(val);
          break;
       }

       case KEY_DASH_RESUME_EVENT:
       {
          ALOGV("calling KEY_DASH_RESUME_EVENT pause()");
          ret = start();
          int32_t val = (ret == OK)? 1:0;
          reply->setDataPosition(0);
          reply->writeInt32(val);
          break;
       }

       case KEY_QCTIMEDTEXT_LISTENER:
       {
         ALOGV("calling KEY_QCTIMEDTEXT_LISTENER");

         int32_t val = 0;
         ret = request.readInt32(&val);
         if (ret != OK)
         {
           ALOGE("Invoke KEY_QCTIMEDTEXT_LISTENER: invalid val");
         }
         else
         {
           bool bVal = (val == 1)? true:false;
           setQCTimedTextListener(bVal);
           reply->setDataPosition(0);
           reply->writeInt32(1);
         }
         break;
       }

       case INVOKE_ID_GET_TRACK_INFO:
       {
         // Ignore the invoke call for INVOKE_ID_GET_TRACK_INFO with success return code
         // to avoid mediaplayer java exception
         ALOGV("Calling INVOKE_ID_GET_TRACK_INFO to invoke");
         ret = getParameter(methodId,reply);
         break;
       }

       default:
       {
         ALOGE("Invoke:unHandled requested method%d",methodId);
         ret = INVALID_OPERATION;
         break;
       }
     }

    return ret;
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
        const media::Metadata::Filter& /*ids*/, Parcel * /*records*/) {
    return INVALID_OPERATION;
}

void DashPlayerDriver::notifyResetComplete() {
    Mutex::Autolock autoLock(mLock);
    CHECK(mResetInProgress);
    mResetInProgress = false;
    mCondition.broadcast();
}

void DashPlayerDriver::notifySetSurfaceComplete() {
    Mutex::Autolock autoLock(mLock);
    CHECK(mSetSurfaceInProgress);
    mSetSurfaceInProgress = false;
    ALOGE("DashPlayerDriver::notifySetSurfaceComplete done");
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
    }

    sendEvent(msg, ext1, ext2, obj);
}

}  // namespace android
