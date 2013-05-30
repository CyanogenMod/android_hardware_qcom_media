/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <utils/Log.h>
#include "DashPlayerStats.h"

#define NO_MIMETYPE_AVAILABLE "N/A"

namespace android {

DashPlayerStats::DashPlayerStats() {
      Mutex::Autolock autoLock(mStatsLock);
      mMIME = new char[strlen(NO_MIMETYPE_AVAILABLE)+1];
      strcpy(mMIME,NO_MIMETYPE_AVAILABLE);
      mNumVideoFramesDecoded = 0;
      mNumVideoFramesDropped = 0;
      mConsecutiveFramesDropped = 0;
      mCatchupTimeStart = 0;
      mNumTimesSyncLoss = 0;
      mMaxEarlyDelta = 0;
      mMaxLateDelta = 0;
      mMaxTimeSyncLoss = 0;
      mTotalFrames = 0;
      mFirstFrameLatencyStartUs = getTimeOfDayUs();
      mLastFrame = 0;
      mLastFrameUs = 0;
      mStatisticsFrames = 0;
      mFPSSumUs = 0;
      mVeryFirstFrame = true;
      mSeekPerformed = false;
      mTotalTime = 0;
      mFirstFrameTime = 0;
      mTotalRenderingFrames = 0;
      mBufferingEvent = false;
      mFd = -1;
      mFileOut = NULL;
}

DashPlayerStats::~DashPlayerStats() {
    Mutex::Autolock autoLock(mStatsLock);
    if(mFileOut){
      fclose(mFileOut);
      mFileOut = NULL;
    }
    if(mMIME) {
        delete[] mMIME;
    }
}

void DashPlayerStats::setFileDescAndOutputStream(int fd) {
    Mutex::Autolock autoLock(mStatsLock);
    mFd = fd;
    if(mFileOut){
      fclose(mFileOut);
      mFileOut = NULL;
    }
    mFileOut = fdopen(dup(fd), "w");
}

void DashPlayerStats::setMime(const char* mime) {
    Mutex::Autolock autoLock(mStatsLock);
    if(mime != NULL) {
        int mimeLen = strlen(mime);
        if(mMIME) {
            delete[] mMIME;
        }

        mMIME = new char[mimeLen+1];
        strcpy(mMIME,mime);
    }
}

void DashPlayerStats::setVeryFirstFrame(bool vff) {
    Mutex::Autolock autoLock(mStatsLock);
    mVeryFirstFrame = true;
}

void DashPlayerStats::notifySeek() {
    Mutex::Autolock autoLock(mStatsLock);
    mFirstFrameLatencyStartUs = getTimeOfDayUs();
    mSeekPerformed = true;
}

void DashPlayerStats::notifyBufferingEvent() {
    Mutex::Autolock autoLock(mStatsLock);
    mBufferingEvent = true;
}

void DashPlayerStats::incrementTotalFrames() {
    Mutex::Autolock autoLock(mStatsLock);
    mTotalFrames++;
}

void DashPlayerStats::incrementTotalRenderingFrames() {
    Mutex::Autolock autoLock(mStatsLock);
    mTotalRenderingFrames++;
}

void DashPlayerStats::incrementDroppedFrames() {
    Mutex::Autolock autoLock(mStatsLock);
    mNumVideoFramesDropped++;
}

void DashPlayerStats::logStatistics() {
    if(mFileOut) {
        Mutex::Autolock autoLock(mStatsLock);
        fprintf(mFileOut, "=====================================================\n");
        fprintf(mFileOut, "Mime Type: %s\n",mMIME);
        fprintf(mFileOut, "Number of total frames: %llu\n",mTotalFrames);
        fprintf(mFileOut, "Number of frames dropped: %lld\n",mNumVideoFramesDropped);
        fprintf(mFileOut, "Number of frames rendered: %llu\n",mTotalRenderingFrames);
        fprintf(mFileOut, "Percentage dropped: %.2f\n",
                           mTotalFrames == 0 ? 0.0 : (double)mNumVideoFramesDropped / mTotalFrames);
        fprintf(mFileOut, "=====================================================\n");
    }
}

void DashPlayerStats::logPause(int64_t positionUs) {
    if(mFileOut) {
        fprintf(mFileOut, "=====================================================\n");
        fprintf(mFileOut, "Pause position: %lld ms\n",positionUs/1000);
        fprintf(mFileOut, "=====================================================\n");
    }
}

void DashPlayerStats::logSeek(int64_t seekTimeUs) {
    if(mFileOut) {
        Mutex::Autolock autoLock(mStatsLock);
        fprintf(mFileOut, "=====================================================\n");
        fprintf(mFileOut, "Seek position: %lld ms\n",seekTimeUs/1000);
        fprintf(mFileOut, "Seek latency: %lld ms\n",(getTimeOfDayUs() - mFirstFrameLatencyStartUs)/1000);
        fprintf(mFileOut, "=====================================================\n");
    }
}

void DashPlayerStats::recordLate(int64_t ts, int64_t clock, int64_t delta, int64_t anchorTime) {
    Mutex::Autolock autoLock(mStatsLock);
    mNumVideoFramesDropped++;
    mConsecutiveFramesDropped++;
    if (mConsecutiveFramesDropped == 1){
      mCatchupTimeStart = anchorTime;
    }

    logLate(ts,clock,delta);
}

void DashPlayerStats::recordOnTime(int64_t ts, int64_t clock, int64_t delta) {
    Mutex::Autolock autoLock(mStatsLock);
    mNumVideoFramesDecoded++;
    mConsecutiveFramesDropped = 0;
    logOnTime(ts,clock,delta);
}

void DashPlayerStats::logSyncLoss() {
    if(mFileOut) {
        Mutex::Autolock autoLock(mStatsLock);
        fprintf(mFileOut, "=====================================================\n");
        fprintf(mFileOut, "Number of times AV Sync Losses = %u\n", mNumTimesSyncLoss);
        fprintf(mFileOut, "Max Video Ahead time delta = %u\n", -mMaxEarlyDelta/1000);
        fprintf(mFileOut, "Max Video Behind time delta = %u\n", mMaxLateDelta/1000);
        fprintf(mFileOut, "Max Time sync loss = %u\n",mMaxTimeSyncLoss/1000);
        fprintf(mFileOut, "=====================================================\n");
    }
}

void DashPlayerStats::logFps() {
    if (mFileOut) {
        Mutex::Autolock autoLock(mStatsLock);
        int64_t now = getTimeOfDayUs();

        if(mTotalRenderingFrames < 2){
           mLastFrameUs = now;
           mFirstFrameTime = now;
        }

        mTotalTime = now - mFirstFrameTime;
        int64_t diff = now - mLastFrameUs;
        if (diff > 250000 && !mVeryFirstFrame && !mBufferingEvent) {
             double fps =((mTotalRenderingFrames - mLastFrame) * 1E6)/diff;
             if (mStatisticsFrames == 0) {
                 fps =((mTotalRenderingFrames - mLastFrame - 1) * 1E6)/diff;
             }
             fprintf(mFileOut, "Frames per second: %.4f, Duration of measurement: %lld\n", fps,diff);
             mFPSSumUs += fps;
             ++mStatisticsFrames;
             mLastFrameUs = now;
             mLastFrame = mTotalRenderingFrames;
         }

        if(mSeekPerformed) {
            mVeryFirstFrame = false;
            mSeekPerformed = false;
        } else if(mVeryFirstFrame) {
            logFirstFrame();
            fprintf(mFileOut, "setting first frame time\n");
            mLastFrameUs = now;
        } else if(mBufferingEvent) {
            mLastFrameUs = now;
            mLastFrame = mTotalRenderingFrames;
        }
        mBufferingEvent = false;
    }
}

void DashPlayerStats::logFpsSummary() {
    if (mFileOut) {
        logStatistics();
        logSyncLoss();
        {
            Mutex::Autolock autoLock(mStatsLock);
            fprintf(mFileOut, "=========================================================\n");
            fprintf(mFileOut, "Average Frames Per Second: %.4f\n", mFPSSumUs/((double)mStatisticsFrames));
            fprintf(mFileOut, "Total Frames (rendered) / Total Time: %.4f\n", ((double)(mTotalRenderingFrames-1)*1E6)/((double)mTotalTime));
            fprintf(mFileOut, "========================================================\n");
        }
    }
}

int64_t DashPlayerStats::getTimeOfDayUs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// WARNING: Most private functions are only thread-safe within mStatsLock
inline void DashPlayerStats::logFirstFrame() {
    fprintf(mFileOut, "=====================================================\n");
    fprintf(mFileOut, "First frame latency: %lld ms\n",(getTimeOfDayUs()-mFirstFrameLatencyStartUs)/1000);
    fprintf(mFileOut, "=====================================================\n");
    mVeryFirstFrame = false;
}

inline void DashPlayerStats::logCatchUp(int64_t ts, int64_t clock, int64_t delta) {
    if (mConsecutiveFramesDropped > 0) {
        mNumTimesSyncLoss++;
        if (mMaxTimeSyncLoss < (clock - mCatchupTimeStart) && clock > 0 && ts > 0) {
            mMaxTimeSyncLoss = clock - mCatchupTimeStart;
        }
    }
}

inline void DashPlayerStats::logLate(int64_t ts, int64_t clock, int64_t delta) {
    if (mMaxLateDelta < delta && clock > 0 && ts > 0) {
        mMaxLateDelta = delta;
    }
}

inline void DashPlayerStats::logOnTime(int64_t ts, int64_t clock, int64_t delta) {
    bool needLogLate = false;
    logCatchUp(ts, clock, delta);
    if (delta <= 0) {
        if ((-delta) > (-mMaxEarlyDelta) && clock > 0 && ts > 0) {
            mMaxEarlyDelta = delta;
        }
    }
    else {
        needLogLate = true;
    }

    if(needLogLate) logLate(ts, clock, delta);
}

} // namespace android
