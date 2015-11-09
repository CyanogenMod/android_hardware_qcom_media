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
#define LOG_TAG "DashPlayerRenderer"

#include "DashPlayerRenderer.h"
#include <cutils/properties.h>
#include <utils/Log.h>

#define DPR_MSG_ERROR(...) ALOGE(__VA_ARGS__)
#define DPR_MSG_HIGH(...) if(mLogLevel >= 1){ALOGE(__VA_ARGS__);}
#define DPR_MSG_MEDIUM(...) if(mLogLevel >= 2){ALOGE(__VA_ARGS__);}
#define DPR_MSG_LOW(...) if(mLogLevel >= 3){ALOGE(__VA_ARGS__);}

namespace android {

// static
const int64_t DashPlayer::Renderer::kMinPositionUpdateDelayUs = 100000ll;

DashPlayer::Renderer::Renderer(
        const sp<MediaPlayerBase::AudioSink> &sink,
        const sp<AMessage> &notify)
    : mAudioSink(sink),
      mNotify(notify),
      mNumFramesWritten(0),
      mDrainAudioQueuePending(false),
      mDrainVideoQueuePending(false),
      mAudioQueueGeneration(0),
      mVideoQueueGeneration(0),
      mAnchorTimeMediaUs(-1),
      mAnchorTimeRealUs(-1),
      mSeekTimeUs(0),
      mFlushingAudio(false),
      mFlushingVideo(false),
      mHasAudio(false),
      mHasVideo(false),
      mSyncQueues(false),
      mNumVideoframesReceived(0),
      mPendingPostAudioDrains(true),
      mPaused(false),
      mWasPaused(false),
      mLastPositionUpdateUs(-1ll),
      mVideoLateByUs(0ll),
      mStats(NULL),
      mLogLevel(0),
      mDelayPending(false),
      mDelayToQueueUs(0),
      mDelayToQueueTimeRealUs(0) {

      mAVSyncDelayWindowUs = 40000;

      char avSyncDelayMsec[PROPERTY_VALUE_MAX] = {0};
      property_get("persist.dash.avsync.window.msec", avSyncDelayMsec, NULL);

      if(*avSyncDelayMsec) {
          int64_t avSyncDelayWindowUs = atoi(avSyncDelayMsec) * 1000;

          if(avSyncDelayWindowUs > 0) {
             mAVSyncDelayWindowUs = avSyncDelayWindowUs;
          }
      }

      DPR_MSG_LOW("AVsync window in Us %lld", mAVSyncDelayWindowUs);

      char property_value[PROPERTY_VALUE_MAX] = {0};
      property_get("persist.dash.debug.level", property_value, NULL);

      if(*property_value) {
          mLogLevel = atoi(property_value);
      }
}

DashPlayer::Renderer::~Renderer() {
    if(mStats != NULL) {
        mStats->logStatistics();
        mStats->logSyncLoss();
        mStats = NULL;
    }
}

void DashPlayer::Renderer::queueBuffer(
        bool audio,
        const sp<ABuffer> &buffer,
        const sp<AMessage> &notifyConsumed) {
    sp<AMessage> msg = new AMessage(kWhatQueueBuffer, this);
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setBuffer("buffer", buffer);
    msg->setMessage("notifyConsumed", notifyConsumed);
    msg->post();
}

void DashPlayer::Renderer::queueEOS(bool audio, status_t finalResult) {
    CHECK_NE(finalResult, (status_t)OK);

    if(mSyncQueues)
      syncQueuesDone();

    sp<AMessage> msg = new AMessage(kWhatQueueEOS, this);
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setInt32("finalResult", finalResult);
    msg->post();
}

void DashPlayer::Renderer::queueDelay(int64_t delayUs) {
    Mutex::Autolock autoLock(mDelayLock);
    if (mDelayPending) {
        // Earlier posted delay still processing.
        mDelayToQueueUs = delayUs;
        mDelayToQueueTimeRealUs = ALooper::GetNowUs();
        DPR_MSG_HIGH("queueDelay Delay already queued earlier. Cache this delay %lld msecs and queue later",
                               mDelayToQueueUs/1000);
        return;
    }

    // Pause audio sink
    if (mHasAudio) {
        mAudioSink->pause();
    }

    DPR_MSG_ERROR("queueDelay delay introduced in rendering %lld msecs", delayUs/1000);

    (new AMessage(kWhatDelayQueued, this ))->post(delayUs);
    mDelayPending = true;
    mDelayToQueueUs = 0;
}

void DashPlayer::Renderer::flush(bool audio) {
    {
        Mutex::Autolock autoLock(mFlushLock);
        if (audio) {
            CHECK(!mFlushingAudio);
            mFlushingAudio = true;
        } else {
            CHECK(!mFlushingVideo);
            mFlushingVideo = true;
        }
    }

    sp<AMessage> msg = new AMessage(kWhatFlush, this);
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->post();
}

void DashPlayer::Renderer::signalTimeDiscontinuity() {
    CHECK(mAudioQueue.empty());
    CHECK(mVideoQueue.empty());
    mAnchorTimeMediaUs = -1;
    mAnchorTimeRealUs = -1;
    mWasPaused = false;
    mSeekTimeUs = 0;
    mSyncQueues = mHasAudio && mHasVideo;
    mNumVideoframesReceived = 0;
    mPendingPostAudioDrains = true;
    mHasAudio = false;
    mHasVideo = false;
    DPR_MSG_HIGH("signalTimeDiscontinuity mHasAudio %d mHasVideo %d mSyncQueues %d",mHasAudio,mHasVideo,mSyncQueues);
}

void DashPlayer::Renderer::pause() {
    (new AMessage(kWhatPause, this))->post();
}

void DashPlayer::Renderer::resume() {
    (new AMessage(kWhatResume, this))->post();
}

void DashPlayer::Renderer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatDrainAudioQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mAudioQueueGeneration) {
                break;
            }

            mDrainAudioQueuePending = false;

            if (onDrainAudioQueue()) {
                uint32_t numFramesPlayed;
                CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed),
                         (status_t)OK);

                uint32_t numFramesPendingPlayout =
                    mNumFramesWritten - numFramesPlayed;

                // This is how long the audio sink will have data to
                // play back.
                int64_t delayUs =
                    (int64_t)(mAudioSink->msecsPerFrame()
                        * (float)(numFramesPendingPlayout * 1000ll));

                // Let's give it more data after about half that time
                // has elapsed.
                postDrainAudioQueue(delayUs / 2);
            }
            break;
        }

        case kWhatDrainVideoQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mVideoQueueGeneration) {
                break;
            }

            mDrainVideoQueuePending = false;

            onDrainVideoQueue();

            postDrainVideoQueue();
            break;
        }

        case kWhatQueueBuffer:
        {
            onQueueBuffer(msg);
            break;
        }

        case kWhatQueueEOS:
        {
            onQueueEOS(msg);
            break;
        }

        case kWhatFlush:
        {
            onFlush(msg);
            break;
        }

        case kWhatAudioSinkChanged:
        {
            onAudioSinkChanged();
            break;
        }

        case kWhatPause:
        {
            onPause();
            break;
        }

        case kWhatResume:
        {
            onResume();
            break;
        }

        case kWhatDelayQueued:
        {
            onDelayQueued();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}


void DashPlayer::Renderer::onDelayQueued() {
    DPR_MSG_HIGH("onDelayQueued resume rendering");

    int64_t delayToQueueUs;
    int64_t delayToQueueTimeRealUs;
    {
        Mutex::Autolock autoLock(mDelayLock);
        mDelayPending = false;
        delayToQueueUs = mDelayToQueueUs;
        delayToQueueTimeRealUs = mDelayToQueueTimeRealUs;
    }

    if (delayToQueueUs > 0) {
        // This is to handle back to delay delays queued which first delay
        // is already in progress. Compute the net elapsed time from when
        // the second delay was posted and queueDelay with the elapsed time
        int64_t delayUs = delayToQueueUs - (ALooper::GetNowUs() - delayToQueueTimeRealUs);
        if (delayUs > 0) {
            DPR_MSG_HIGH("onDelayQueued delay was posted again mDelayToQueueUs %lld msecs. Calling queueDelay()",
                               delayToQueueUs/1000);
            queueDelay(delayUs);
            return;
        }
    }

    if(mHasAudio && !mPaused) {
        mAudioSink->start();
    }
    postDrainAudioQueue();
    postDrainVideoQueue();
}

void DashPlayer::Renderer::postDrainAudioQueue(int64_t delayUs) {
    if (mDelayPending || mDrainAudioQueuePending || mSyncQueues || mPaused) {
        return;
    }

    if (mAudioQueue.empty()) {
        return;
    }

    mDrainAudioQueuePending = true;
    sp<AMessage> msg = new AMessage(kWhatDrainAudioQueue, this);
    msg->setInt32("generation", mAudioQueueGeneration);
    msg->post(delayUs);
}

void DashPlayer::Renderer::signalAudioSinkChanged() {
    (new AMessage(kWhatAudioSinkChanged,this))->post();
}

bool DashPlayer::Renderer::onDrainAudioQueue() {
    if(mDelayPending) {
        return false;
    }

    uint32_t numFramesPlayed;

    // Check if first frame is EOS, process EOS and return
    if(1 == mAudioQueue.size())
    {
       QueueEntry *entry = &*mAudioQueue.begin();
       if (entry->mBuffer == NULL) {
        ALOGE("onDrainAudioQueue process EOS");
        notifyEOS(true /* audio */, entry->mFinalResult);

        mAudioQueue.erase(mAudioQueue.begin());
        entry = NULL;
        return false;
      }
    }

    if (mAudioSink->getPosition(&numFramesPlayed) != OK) {
        return false;
    }

    ssize_t numFramesAvailableToWrite =
        mAudioSink->frameCount() - (mNumFramesWritten - numFramesPlayed);

    size_t numBytesAvailableToWrite =
        numFramesAvailableToWrite * mAudioSink->frameSize();

    while (numBytesAvailableToWrite > 0 && !mAudioQueue.empty()) {
        QueueEntry *entry = &*mAudioQueue.begin();

        if (entry->mBuffer == NULL) {
            // EOS

            notifyEOS(true /* audio */, entry->mFinalResult);

            mAudioQueue.erase(mAudioQueue.begin());
            entry = NULL;
            return false;
        }

        if (entry->mOffset == 0) {
            int64_t mediaTimeUs;
            CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

            DPR_MSG_HIGH("rendering audio at media time %.2f secs", (double)mediaTimeUs / 1E6);

            mAnchorTimeMediaUs = mediaTimeUs;

            uint32_t numFramesPlayed;
            CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed), (status_t)OK);

            uint32_t numFramesPendingPlayout =
                mNumFramesWritten - numFramesPlayed;

            int64_t realTimeOffsetUs =
                (int64_t)(((float)mAudioSink->latency() / 2
                    + (float)numFramesPendingPlayout
                        * mAudioSink->msecsPerFrame()) * 1000ll);

            mAnchorTimeRealUs =
                ALooper::GetNowUs() + realTimeOffsetUs;
        }

        size_t copy = entry->mBuffer->size() - entry->mOffset;
        if (copy > numBytesAvailableToWrite) {
            copy = numBytesAvailableToWrite;
        }

        CHECK_EQ(mAudioSink->write(
                    entry->mBuffer->data() + entry->mOffset, copy),
                 (ssize_t)copy);

        entry->mOffset += copy;
        if (entry->mOffset == entry->mBuffer->size()) {
            entry->mNotifyConsumed->post();
            mAudioQueue.erase(mAudioQueue.begin());

            entry = NULL;
        }

        numBytesAvailableToWrite -= copy;
        size_t copiedFrames = copy / mAudioSink->frameSize();
        mNumFramesWritten += (uint32_t)copiedFrames;
    }

    notifyPosition();

    return !mAudioQueue.empty();
}

void DashPlayer::Renderer::postDrainVideoQueue() {
    if (mDelayPending || mDrainVideoQueuePending || mSyncQueues || mPaused) {
        return;
    }

    if (mVideoQueue.empty()) {
        return;
    }

    QueueEntry &entry = *mVideoQueue.begin();

    sp<AMessage> msg = new AMessage(kWhatDrainVideoQueue, this);
    msg->setInt32("generation", mVideoQueueGeneration);

    int64_t delayUs;

    if (entry.mBuffer == NULL) {
        // EOS doesn't carry a timestamp.
        delayUs = 0;
    } else {
        int64_t mediaTimeUs;
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

        if (mAnchorTimeMediaUs < 0) {
            delayUs = 0;

            if (!mHasAudio) {
                mAnchorTimeMediaUs = mediaTimeUs;
                mAnchorTimeRealUs = ALooper::GetNowUs();
            }
        } else {
            if ( (!mHasAudio && mHasVideo) && (mWasPaused == true))
            {
               mAnchorTimeMediaUs = mediaTimeUs;
               mAnchorTimeRealUs = ALooper::GetNowUs();
               mWasPaused = false;
            }

            int64_t realTimeUs =
                (mediaTimeUs - mAnchorTimeMediaUs) + mAnchorTimeRealUs;

            delayUs = realTimeUs - ALooper::GetNowUs();
        }
    }

    msg->post(delayUs);

    mDrainVideoQueuePending = true;
}

void DashPlayer::Renderer::onDrainVideoQueue() {
    if(mDelayPending) {
        return;
    }

    if (mVideoQueue.empty()) {
        return;
    }

    QueueEntry *entry = &*mVideoQueue.begin();

    if (entry->mBuffer == NULL) {
        // EOS

        notifyPosition(true);

        notifyEOS(false /* audio */, entry->mFinalResult);

        mVideoQueue.erase(mVideoQueue.begin());
        entry = NULL;

        mVideoLateByUs = 0ll;

        return;
    }

    int64_t mediaTimeUs;
    CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

    int64_t realTimeUs = mediaTimeUs - mAnchorTimeMediaUs + mAnchorTimeRealUs;
    int64_t nowUs = ALooper::GetNowUs();
    mVideoLateByUs = nowUs - realTimeUs;

    bool tooLate = (mVideoLateByUs > mAVSyncDelayWindowUs);

    if (tooLate) {
        DPR_MSG_HIGH("video late by %lld us (%.2f secs)",
             mVideoLateByUs, (double)mVideoLateByUs / 1E6);
        if(mStats != NULL) {
            mStats->recordLate(realTimeUs,nowUs,mVideoLateByUs,mAnchorTimeRealUs);
        }
    } else {
        DPR_MSG_HIGH("rendering video at media time %.2f secs", (double)mediaTimeUs / 1E6);
        if(mStats != NULL) {
            mStats->recordOnTime(realTimeUs,nowUs,mVideoLateByUs);
            mStats->incrementTotalRenderingFrames();
            mStats->logFps();
        }
    }

    entry->mNotifyConsumed->setInt32("render", !tooLate);
    entry->mNotifyConsumed->post();
    mVideoQueue.erase(mVideoQueue.begin());
    entry = NULL;

    notifyPosition();
}

void DashPlayer::Renderer::notifyEOS(bool audio, status_t finalResult) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->setInt32("finalResult", finalResult);
    notify->post();
}

void DashPlayer::Renderer::onQueueBuffer(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (audio) {
        mHasAudio = true;
    } else {
        mHasVideo = true;
    }

    if (dropBufferWhileFlushing(audio, msg)) {
        return;
    }

    sp<ABuffer> buffer;
    CHECK(msg->findBuffer("buffer", &buffer));

    sp<AMessage> notifyConsumed;
    CHECK(msg->findMessage("notifyConsumed", &notifyConsumed));

    QueueEntry entry;
    entry.mBuffer = buffer;
    entry.mNotifyConsumed = notifyConsumed;
    entry.mOffset = 0;
    entry.mFinalResult = OK;

    if (audio) {
        mAudioQueue.push_back(entry);

        if ((mHasVideo && !mPendingPostAudioDrains) || !mHasVideo) {
            postDrainAudioQueue();
            return;
        } else {
            int64_t audioTimeUs;
            (buffer->meta())->findInt64("timeUs", &audioTimeUs);
            DPR_MSG_HIGH("Not rendering audio Sample with TS: %lld  until first two video frames are decoded", audioTimeUs);
        }
    } else {
        mVideoQueue.push_back(entry);

        if (mNumVideoframesReceived < 2) {
            int64_t videoTimeUs;
            (buffer->meta())->findInt64("timeUs", &videoTimeUs);

            if (mNumVideoframesReceived == 0) {
                DPR_MSG_HIGH("Received first video Sample with TS: %lld", videoTimeUs);
                mNumVideoframesReceived++;
                return;
            } else if (mNumVideoframesReceived == 1) {
                DPR_MSG_HIGH("Received second video Sample with TS: %lld", videoTimeUs);
                mNumVideoframesReceived++;
            }
        }

        if (mPendingPostAudioDrains) {
            mPendingPostAudioDrains = false;
            postDrainAudioQueue();
        }
        postDrainVideoQueue();
    }

    if (!mSyncQueues || mAudioQueue.empty() || mVideoQueue.empty()) {
        return;
    }

    sp<ABuffer> firstAudioBuffer = (*mAudioQueue.begin()).mBuffer;
    sp<ABuffer> firstVideoBuffer = (*mVideoQueue.begin()).mBuffer;

    if (firstAudioBuffer == NULL || firstVideoBuffer == NULL) {
        // EOS signalled on either queue.
        syncQueuesDone();
        return;
    }

    int64_t firstAudioTimeUs;
    int64_t firstVideoTimeUs;
    CHECK(firstAudioBuffer->meta()
            ->findInt64("timeUs", &firstAudioTimeUs));
    CHECK(firstVideoBuffer->meta()
            ->findInt64("timeUs", &firstVideoTimeUs));

    int64_t diff = firstVideoTimeUs - firstAudioTimeUs;

    DPR_MSG_LOW("queueDiff = %.2f secs", (double)diff / 1E6);

    if (diff > 100000ll) {
        // Audio data starts More than 0.1 secs before video.
        // Drop some audio.

        (*mAudioQueue.begin()).mNotifyConsumed->post();
        mAudioQueue.erase(mAudioQueue.begin());
        return;
    }

    syncQueuesDone();
}

void DashPlayer::Renderer::syncQueuesDone() {
    if (!mSyncQueues) {
        return;
    }

    mSyncQueues = false;

    if (!mAudioQueue.empty()) {
        postDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
        postDrainVideoQueue();
    }
}

void DashPlayer::Renderer::onQueueEOS(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (dropBufferWhileFlushing(audio, msg)) {
        return;
    }

    int32_t finalResult;
    CHECK(msg->findInt32("finalResult", &finalResult));

    QueueEntry entry;
    entry.mOffset = 0;
    entry.mFinalResult = finalResult;

    if (audio) {
        mAudioQueue.push_back(entry);
        postDrainAudioQueue();
    } else {
        mVideoQueue.push_back(entry);
        postDrainVideoQueue();
    }
}

void DashPlayer::Renderer::onFlush(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    // If we're currently syncing the queues, i.e. dropping audio while
    // aligning the first audio/video buffer times and only one of the
    // two queues has data, we may starve that queue by not requesting
    // more buffers from the decoder. If the other source then encounters
    // a discontinuity that leads to flushing, we'll never find the
    // corresponding discontinuity on the other queue.
    // Therefore we'll stop syncing the queues if at least one of them
    // is flushed.
    syncQueuesDone();

    if (audio) {
        flushQueue(&mAudioQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingAudio = false;

        mDrainAudioQueuePending = false;
        ++mAudioQueueGeneration;
    } else {
        flushQueue(&mVideoQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingVideo = false;

        mDrainVideoQueuePending = false;
        ++mVideoQueueGeneration;
        if(mStats != NULL) {
            mStats->setVeryFirstFrame(true);
        }
    }

    notifyFlushComplete(audio);
}

void DashPlayer::Renderer::flushQueue(List<QueueEntry> *queue) {
    while (!queue->empty()) {
        QueueEntry *entry = &*queue->begin();

        if (entry->mBuffer != NULL) {
            entry->mNotifyConsumed->post();
        }

        queue->erase(queue->begin());
        entry = NULL;
    }
}

void DashPlayer::Renderer::notifyFlushComplete(bool audio) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushComplete);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->post();
}

bool DashPlayer::Renderer::dropBufferWhileFlushing(
        bool audio, const sp<AMessage> &msg) {
    bool flushing = false;

    {
        Mutex::Autolock autoLock(mFlushLock);
        if (audio) {
            flushing = mFlushingAudio;
        } else {
            flushing = mFlushingVideo;
        }
    }

    if (!flushing) {
        return false;
    }

    sp<AMessage> notifyConsumed;
    if (msg->findMessage("notifyConsumed", &notifyConsumed)) {
        notifyConsumed->post();
    }

    return true;
}

void DashPlayer::Renderer::onAudioSinkChanged() {
    CHECK(!mDrainAudioQueuePending);
    mNumFramesWritten = 0;
    uint32_t written;
    if (mAudioSink->getFramesWritten(&written) == OK) {
        mNumFramesWritten = written;
    }
}

void DashPlayer::Renderer::notifyPosition(bool isEOS) {
    if (mAnchorTimeRealUs < 0 || mAnchorTimeMediaUs < 0) {
        return;
    }

    int64_t nowUs = ALooper::GetNowUs();

    if ((!isEOS) && (mLastPositionUpdateUs >= 0
            && nowUs < mLastPositionUpdateUs + kMinPositionUpdateDelayUs)) {
        return;
    }
    mLastPositionUpdateUs = nowUs;

    int64_t positionUs = (mSeekTimeUs != 0) ? mSeekTimeUs : ((nowUs - mAnchorTimeRealUs) + mAnchorTimeMediaUs);

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatPosition);
    notify->setInt64("positionUs", positionUs);
    notify->setInt64("videoLateByUs", mVideoLateByUs);
    notify->post();
}

void DashPlayer::Renderer::notifySeekPosition(int64_t seekTime){
  mSeekTimeUs = seekTime;
  int64_t nowUs = ALooper::GetNowUs();
  mLastPositionUpdateUs = nowUs;
  sp<AMessage> notify = mNotify->dup();
  notify->setInt32("what", kWhatPosition);
  notify->setInt64("positionUs", seekTime);
  notify->setInt64("videoLateByUs", mVideoLateByUs);
  notify->post();

}


void DashPlayer::Renderer::onPause() {
    CHECK(!mPaused);

    mDrainAudioQueuePending = false;
    ++mAudioQueueGeneration;

    mDrainVideoQueuePending = false;
    ++mVideoQueueGeneration;

    if (mHasAudio) {
        mAudioSink->pause();
    }

    DPR_MSG_LOW("now paused audio queue has %d entries, video has %d entries",
          mAudioQueue.size(), mVideoQueue.size());

    mPaused = true;
    mWasPaused = true;

    if(mStats != NULL) {
        int64_t positionUs;
        if(mAnchorTimeRealUs < 0 || mAnchorTimeMediaUs < 0) {
            positionUs = -1000;
        } else {
            int64_t nowUs = ALooper::GetNowUs();
            positionUs = (nowUs - mAnchorTimeRealUs) + mAnchorTimeMediaUs;
        }

        mStats->logPause(positionUs);
    }
}

void DashPlayer::Renderer::onResume() {
    if (!mPaused) {
        return;
    }

    if (mHasAudio && !mDelayPending) {
        mAudioSink->start();
    }

    mPaused = false;

    if (!mAudioQueue.empty()) {
        postDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
        postDrainVideoQueue();
    }
}

void DashPlayer::Renderer::registerStats(sp<DashPlayerStats> stats) {
    if(mStats != NULL) {
        mStats = NULL;
    }
    mStats = stats;
}

status_t DashPlayer::Renderer::setMediaPresence(bool audio, bool bValue)
{
   if (audio)
   {
      DPR_MSG_LOW("mHasAudio set to %d from %d",bValue,mHasAudio);
      mHasAudio = bValue;
   }
   else
   {
     DPR_MSG_LOW("mHasVideo set to %d from %d",bValue,mHasVideo);
     mHasVideo = bValue;
   }
   return OK;
}

}  // namespace android

