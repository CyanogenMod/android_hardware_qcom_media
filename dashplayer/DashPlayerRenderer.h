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

#ifndef DASHPLAYER_RENDERER_H_

#define DASHPLAYER_RENDERER_H_

#include "DashPlayer.h"

namespace android {

struct ABuffer;

struct DashPlayer::Renderer : public AHandler {
    Renderer(const sp<MediaPlayerBase::AudioSink> &sink,
             const sp<AMessage> &notify);

    void queueBuffer(
            bool audio,
            const sp<ABuffer> &buffer,
            const sp<AMessage> &notifyConsumed);
#ifdef QCOM_WFD_SINK
    virtual void queueEOS(bool audio, status_t finalResult);

    virtual void flush(bool audio);

    virtual void signalTimeDiscontinuity();

    virtual void signalAudioSinkChanged();

    virtual void pause();
    virtual void resume();
#else

    void queueEOS(bool audio, status_t finalResult);

    void flush(bool audio);

    void signalTimeDiscontinuity();

    void signalAudioSinkChanged();

    void pause();
    void resume();
    void notifySeekPosition(int64_t seekTime);
#endif /* QCOM_WFD_SINK */
    enum {
        kWhatEOS                = 'eos ',
        kWhatFlushComplete      = 'fluC',
        kWhatPosition           = 'posi',
    };

protected:
    virtual ~Renderer();

    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kWhatDrainAudioQueue    = 'draA',
        kWhatDrainVideoQueue    = 'draV',
        kWhatQueueBuffer        = 'queB',
        kWhatQueueEOS           = 'qEOS',
        kWhatFlush              = 'flus',
        kWhatAudioSinkChanged   = 'auSC',
        kWhatPause              = 'paus',
        kWhatResume             = 'resm',
    };

    struct QueueEntry {
        sp<ABuffer> mBuffer;
        sp<AMessage> mNotifyConsumed;
        size_t mOffset;
        status_t mFinalResult;
    };

    static const int64_t kMinPositionUpdateDelayUs;

    sp<MediaPlayerBase::AudioSink> mAudioSink;
    sp<AMessage> mNotify;
    List<QueueEntry> mAudioQueue;
    List<QueueEntry> mVideoQueue;
    uint32_t mNumFramesWritten;

    bool mDrainAudioQueuePending;
    bool mDrainVideoQueuePending;
    int32_t mAudioQueueGeneration;
    int32_t mVideoQueueGeneration;

    int64_t mAnchorTimeMediaUs;
    int64_t mAnchorTimeRealUs;
    int64_t mSeekTimeUs;

    Mutex mFlushLock;  // protects the following 2 member vars.
    bool mFlushingAudio;
    bool mFlushingVideo;

    bool mHasAudio;
    bool mHasVideo;
    bool mSyncQueues;

    bool mPaused;
    bool mWasPaused; // if paused then store the info

    int64_t mLastPositionUpdateUs;
    int64_t mVideoLateByUs;

    bool onDrainAudioQueue();
    void postDrainAudioQueue(int64_t delayUs = 0);

    void onDrainVideoQueue();
    void postDrainVideoQueue();
#ifdef QCOM_WFD_SINK
    virtual void onQueueBuffer(const sp<AMessage> &msg);
#else
    void onQueueBuffer(const sp<AMessage> &msg);
#endif /* QCOM_WFD_SINK */
    void onQueueEOS(const sp<AMessage> &msg);
    void onFlush(const sp<AMessage> &msg);
    void onAudioSinkChanged();
    void onPause();
    void onResume();

    void notifyEOS(bool audio, status_t finalResult);
    void notifyFlushComplete(bool audio);
    void notifyPosition(bool isEOS = false);
    void notifyVideoLateBy(int64_t lateByUs);

    void flushQueue(List<QueueEntry> *queue);
    bool dropBufferWhileFlushing(bool audio, const sp<AMessage> &msg);
    void syncQueuesDone();

    // for qualcomm statistics profiling
  public:
#ifdef QCOM_WFD_SINK
    virtual void registerStats(sp<DashPlayerStats> stats);
    virtual status_t setMediaPresence(bool audio, bool bValue);
#else
    void registerStats(sp<DashPlayerStats> stats);
    status_t setMediaPresence(bool audio, bool bValue);
#endif /* QCOM_WFD_SINK */
  private:
    sp<DashPlayerStats> mStats;

    DISALLOW_EVIL_CONSTRUCTORS(Renderer);
};

}  // namespace android

#endif  // DASHPLAYER_RENDERER_H_
