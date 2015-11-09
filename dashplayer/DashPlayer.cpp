/*
 *Copyright (c) 2013 - 2014, The Linux Foundation. All rights reserved.
 *Not a Contribution, Apache license notifications and license are retained
 *for attribution purposes only.
 *
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

#define LOG_TAG "DashPlayer"
#define SRMax 30
#include <utils/Log.h>
#include <dlfcn.h>  // for dlopen/dlclose
#include "DashPlayer.h"
#include "DashPlayerDecoder.h"
#include "DashPlayerDriver.h"
#include "DashPlayerRenderer.h"
#include "DashPlayerSource.h"
#include "ATSParser.h"
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <gui/IGraphicBufferProducer.h>
#include "avc_utils.h"
#include "OMX_QCOMExtns.h"
#include <gralloc_priv.h>
#include <cutils/properties.h>
#include <utils/Log.h>
#include <media/msm_media_info.h>
#include <qcmediaplayer.h>

#define DP_MSG_ERROR(...) ALOGE(__VA_ARGS__)
#define DP_MSG_HIGH(...) if(mLogLevel >= 1){ALOGE(__VA_ARGS__);}
#define DP_MSG_MEDIUM(...) if(mLogLevel >= 2){ALOGE(__VA_ARGS__);}
#define DP_MSG_LOW(...) if(mLogLevel >= 3){ALOGE(__VA_ARGS__);}

#define AUDIO_DISCONTINUITY_THRESHOLD 100000ll
#define AUDIO_TS_DISCONTINUITY_THRESHOLD 200000ll

namespace android {

struct DashPlayer::Action : public RefBase {
    Action() {}

    virtual void execute(DashPlayer *player) = 0;

private:
    DISALLOW_EVIL_CONSTRUCTORS(Action);
};

struct DashPlayer::SetSurfaceAction : public Action {
    SetSurfaceAction(const sp<Surface> &wrapper)
        : mWrapper(wrapper) {
    }

    virtual void execute(DashPlayer *player) {
        player->performSetSurface(mWrapper);
    }

private:
    sp<Surface> mWrapper;

    DISALLOW_EVIL_CONSTRUCTORS(SetSurfaceAction);
};

struct DashPlayer::ShutdownDecoderAction : public Action {
    ShutdownDecoderAction(bool audio, bool video)
        : mAudio(audio),
          mVideo(video) {
    }

    virtual void execute(DashPlayer *player) {
        player->performDecoderShutdown(mAudio, mVideo);
    }

private:
    bool mAudio;
    bool mVideo;

    DISALLOW_EVIL_CONSTRUCTORS(ShutdownDecoderAction);
};

// Use this if there's no state necessary to save in order to execute
// the action.
struct DashPlayer::SimpleAction : public Action {
    typedef void (DashPlayer::*ActionFunc)();

    SimpleAction(ActionFunc func)
        : mFunc(func) {
    }

    virtual void execute(DashPlayer *player) {
        (player->*mFunc)();
    }

private:
    ActionFunc mFunc;

    DISALLOW_EVIL_CONSTRUCTORS(SimpleAction);
};

////////////////////////////////////////////////////////////////////////////////

DashPlayer::DashPlayer()
    : mUIDValid(false),
      mVideoIsAVC(false),
      mRenderer(NULL),
      mAudioEOS(false),
      mVideoEOS(false),
      mScanSourcesPending(false),
      isSetSurfaceTexturePending(false),
      mScanSourcesGeneration(0),
      mBufferingNotification(false),
      mTimeDiscontinuityPending(false),
      mFlushingAudio(NONE),
      mFlushingVideo(NONE),
      mResetInProgress(false),
      mResetPostponed(false),
      mSetVideoSize(true),
      mSkipRenderingAudioUntilMediaTimeUs(-1ll),
      mSkipRenderingVideoUntilMediaTimeUs(-1ll),
      mVideoLateByUs(0ll),
      mPauseIndication(false),
      mSRid(0),
      mStats(NULL),
      mLogLevel(0),
      mTimedTextCEAPresent(false),
      mTimedTextCEASamplesDisc(false),
      mQCTimedTextListenerPresent(false),
      mCurrentWidth(0),
      mCurrentHeight(0),
      mColorFormat(0),
      mDPBSize(0),
      mDPBCheckToDelayRendering(true),
      mVideoDecoderStartTimeUs(0),
      mVideoDecoderSetupTimeUs(0),
      mDelayRenderingUs(0),
      mFirstVideoSampleUs(-1),
      mVideoSampleDurationUs(0),
      mLastReadAudioMediaTimeUs(-1),
      mLastReadAudioRealTimeUs(-1) {
      mTrackName = new char[6];

      char property_value[PROPERTY_VALUE_MAX] = {0};
      property_get("persist.dash.debug.level", property_value, NULL);

      if(*property_value) {
          mLogLevel = atoi(property_value);
      }

}

DashPlayer::~DashPlayer() {
    if (mRenderer != NULL) {
        looper()->unregisterHandler(mRenderer->id());
    }
    if (mAudioDecoder != NULL) {
      looper()->unregisterHandler(mAudioDecoder->id());
    }
    if (mVideoDecoder != NULL) {
      looper()->unregisterHandler(mVideoDecoder->id());
    }
    if (mTextDecoder != NULL) {
      looper()->unregisterHandler(mTextDecoder->id());
    }
    if(mStats != NULL) {
        mStats->logFpsSummary();
        mStats = NULL;
    }
    if (mTrackName != NULL) {
       delete[] mTrackName;
       mTrackName = NULL;
    }
}

void DashPlayer::setUID(uid_t uid) {
    mUIDValid = true;
    mUID = uid;
}

void DashPlayer::setDriver(const wp<DashPlayerDriver> &driver) {
    mDriver = driver;
}

void DashPlayer::setDataSource(const sp<IStreamSource> &/*source*/) {
    DP_MSG_ERROR("DashPlayer::setDataSource not Implemented...");
}

status_t DashPlayer::setDataSource(
        const char *url, const KeyedVector<String8, String8> *headers) {
    sp<AMessage> msg = new AMessage(kWhatSetDataSource, this);

    sp<Source> source;
    if (!strncasecmp(url, "http://", 7) &&
          (strlen(url) >= 4 && !strcasecmp(".mpd", &url[strlen(url) - 4]))) {
           /* Load the DASH HTTP Live source librery here */
           DP_MSG_LOW("DashPlayer setDataSource url sting %s",url);
           source = LoadCreateSource(url, headers, mUIDValid, mUID);
           if (source != NULL) {
              msg->setObject("source", source);
              msg->post();
              return OK;
           } else {
             DP_MSG_ERROR("Error creating DASH source");
             return UNKNOWN_ERROR;
           }
    }
    else
    {
      DP_MSG_ERROR("Unsupported URL");
      return UNKNOWN_ERROR;
    }
}

void DashPlayer::setDataSource(int /*fd*/, int64_t /*offset*/, int64_t /*length*/) {
   DP_MSG_ERROR("DashPlayer::setDataSource not Implemented...");
}

void DashPlayer::setVideoSurfaceTexture(const sp<IGraphicBufferProducer> &bufferProducer) {
    sp<AMessage> msg = new AMessage(kWhatSetVideoNativeWindow, this);

    if (bufferProducer == NULL) {
        msg->setObject("surface", NULL);
        DP_MSG_ERROR("DashPlayer::setVideoSurfaceTexture bufferproducer = NULL ");
    } else {
        DP_MSG_ERROR("DashPlayer::setVideoSurfaceTexture bufferproducer = %p", bufferProducer.get());
        msg->setObject(
                "surface", new Surface(bufferProducer,  true /* controlledByApp */));
    }

    msg->post();
}

void DashPlayer::setAudioSink(const sp<MediaPlayerBase::AudioSink> &sink) {
    sp<AMessage> msg = new AMessage(kWhatSetAudioSink, this);
    msg->setObject("sink", sink);
    msg->post();
}

void DashPlayer::start() {
    (new AMessage(kWhatStart, this))->post();
}

void DashPlayer::pause() {
    (new AMessage(kWhatPause, this))->post();
}

void DashPlayer::resume() {
    (new AMessage(kWhatResume, this))->post();
}

void DashPlayer::resetAsync() {
    (new AMessage(kWhatReset, this))->post();
}

void DashPlayer::seekToAsync(int64_t seekTimeUs) {
    sp<AMessage> msg = new AMessage(kWhatSeek, this);
    msg->setInt64("seekTimeUs", seekTimeUs);
    msg->post();
}

// static
bool DashPlayer::IsFlushingState(FlushStatus state, bool *needShutdown) {
    switch (state) {
        case FLUSHING_DECODER:
            if (needShutdown != NULL) {
                *needShutdown = false;
            }
            return true;

        case FLUSHING_DECODER_SHUTDOWN:
        case SHUTTING_DOWN_DECODER:
            if (needShutdown != NULL) {
                *needShutdown = true;
            }
            return true;

        default:
            return false;
    }
}

void DashPlayer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatSetDataSource:
        {
            DP_MSG_ERROR("kWhatSetDataSource");

            CHECK(mSource == NULL);

            sp<RefBase> obj;
            CHECK(msg->findObject("source", &obj));

            mSource = static_cast<Source *>(obj.get());
            prepareSource();

            break;
        }

        case kWhatSetVideoNativeWindow:
        {
            /* if MediaPlayer calls setDisplay(NULL) in the middle of the playback, */
            /* block this call to perform following sequence on video decoder       */
            /*     flush-->shutdown-->then update nativewindow to NULL              */

            /* Mediaplayer can also call valid native window to enable video        */
            /* playback again dynamically, in such case scan sources will trigger   */
            /* reinstantiation of video decoder and video playback continues.       */
            /*  TODO: Dynamic disible and reenable of video also requies support    */
            /* from dash source.                                                    */
            DP_MSG_ERROR("kWhatSetVideoNativeWindow");

/*
              if existing instance mNativeWindow=NULL, just set mNativeWindow to the new value passed
              postScanSources() called below to handle use case
                 - Initial valid nativewindow
                 - first call from app to set nativewindow to null but mVideoDecoder exists. So scansources loop will not be running
                 - second call to set nativewindow to valid object. Enters below if() portion. Need to trigger scansources to instatiate mVideoDecoder
            */
            if(mNativeWindow == NULL)
            {
            sp<RefBase> obj;
            CHECK(msg->findObject("surface", &obj));

            mNativeWindow = static_cast<Surface *>(obj.get());
              DP_MSG_ERROR("kWhatSetVideoNativeWindow valid nativewindow  %p", mNativeWindow.get());
              if (mDriver != NULL) {
              sp<DashPlayerDriver> driver = mDriver.promote();
              if (driver != NULL) {
                 driver->notifySetSurfaceComplete();
                }
              }

              DP_MSG_ERROR("kWhatSetVideoNativeWindow nativewindow %d", mScanSourcesPending);
              postScanSources();
              break;
            }

            /* Already existing valid mNativeWindow and valid mVideoDecoder
                 - Perform shutdown sequence
                 - postScanSources() to instantiate mVideoDecoder with the new native window object.
               If no mVideoDecoder existed, and new nativewindow set to NULL push blank buffers to native window (embms audio only switch use case)
            */

            sp<RefBase> obj;
            CHECK(msg->findObject("surface", &obj));

            if(mVideoDecoder == NULL && obj.get() == NULL)
            {
              ANativeWindow *nativeWindow = mNativeWindow.get();
              PushBlankBuffersToNativeWindow(nativeWindow);
            }

            mDeferredActions.push_back(new ShutdownDecoderAction(
                                       false /* audio */, true /* video */));

            DP_MSG_ERROR("kWhatSetVideoNativeWindow old nativewindow  %p", mNativeWindow.get());
            DP_MSG_ERROR("kWhatSetVideoNativeWindow new nativewindow  %p", obj.get());

            mDeferredActions.push_back(
            new SetSurfaceAction(static_cast<Surface *>(obj.get())));

            if (obj.get() != NULL) {
            // If there is a new surface texture, instantiate decoders
            // again if possible.
            mDeferredActions.push_back(
            new SimpleAction(&DashPlayer::performScanSources));
            }

            isSetSurfaceTexturePending = true;
            processDeferredActions();
            break;
        }

        case kWhatSetAudioSink:
        {
            DP_MSG_ERROR("kWhatSetAudioSink");

            sp<RefBase> obj;
            CHECK(msg->findObject("sink", &obj));

            mAudioSink = static_cast<MediaPlayerBase::AudioSink *>(obj.get());
            break;
        }

        case kWhatStart:
        {
            DP_MSG_ERROR("kWhatStart");

            mVideoIsAVC = false;
            mAudioEOS = false;
            mVideoEOS = false;
            mSkipRenderingAudioUntilMediaTimeUs = -1;
            mSkipRenderingVideoUntilMediaTimeUs = -1;
            mVideoLateByUs = 0;
            if (mSource != NULL)
            {
              mSource->start();
            }

            mRenderer = new Renderer(
                    mAudioSink,
                    new AMessage(kWhatRendererNotify, this));
            // for qualcomm statistics profiling
            mStats = new DashPlayerStats();
            mRenderer->registerStats(mStats);
            looper()->registerHandler(mRenderer);

            postScanSources();
            break;
        }

        case kWhatScanSources:
        {
            if (!mPauseIndication) {
                int32_t generation = 0;
                CHECK(msg->findInt32("generation", &generation));
                if (generation != mScanSourcesGeneration) {
                    // Drop obsolete msg.
                    break;
                }

                mScanSourcesPending = false;

                //Exit scanSources if source was destroyed
                //Later after source gets recreated and started (setDataSource() and start()) scanSources is posted again
                if (mSource == NULL)
                {
                  DP_MSG_ERROR("Source is null. Exit scanSources\n");
                  break;
                }

                DP_MSG_LOW("scanning sources haveAudio=%d, haveVideo=%d haveText=%d",
                mAudioDecoder != NULL, mVideoDecoder != NULL, mTextDecoder!= NULL);


                if(mNativeWindow != NULL) {
                    instantiateDecoder(kVideo, &mVideoDecoder);
                }

                if (mAudioSink != NULL) {
                    instantiateDecoder(kAudio, &mAudioDecoder);
                }

                instantiateDecoder(kText, &mTextDecoder);

                status_t err;
                if ((err = mSource->feedMoreTSData()) != OK) {
                    if (mAudioDecoder == NULL && mVideoDecoder == NULL) {
                        // We're not currently decoding anything (no audio or
                        // video tracks found) and we just ran out of input data.

                        if (err == ERROR_END_OF_STREAM) {
                            notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                        } else {
                            notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
                        }
                    }
                    break;
                }
                if ((mAudioDecoder == NULL && mAudioSink != NULL)     ||
                    (mVideoDecoder == NULL && mNativeWindow != NULL)  ||
                    (mTextDecoder == NULL)) {
                        msg->post(100000ll);
                        mScanSourcesPending = true;
                }

                if (mTimeDiscontinuityPending && mRenderer != NULL){
                    mRenderer->signalTimeDiscontinuity();
                    mTimeDiscontinuityPending = false;
                }
            }
            break;
        }

        case kWhatVideoNotify:
        case kWhatAudioNotify:
        case kWhatTextNotify:
        {
            int track = -1;
            if (msg->what() == kWhatAudioNotify)
                track = kAudio;
            else if (msg->what() == kWhatVideoNotify)
                track = kVideo;
            else if (msg->what() == kWhatTextNotify)
                track = kText;

            getTrackName(track,mTrackName);

            int32_t what;

            if(track == kText)
            {
              sp<AMessage> codecRequest;
              CHECK(msg->findMessage("codec-request", &codecRequest));

              CHECK(codecRequest->findInt32("what", &what));
            }
            else
            {
              CHECK(msg->findInt32("what", &what));
            }

            if (what == Decoder::kWhatFillThisBuffer) {
                DP_MSG_LOW("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++ (%s) kWhatFillThisBuffer",mTrackName);
                if ( (track == kText) && (mTextDecoder == NULL)) {
                    break; // no need to proceed further
                }

                status_t err = feedDecoderInputData(
                        track, msg);
                if (mSource == NULL)
                {
                  DP_MSG_ERROR("Source is null. Exit Notify\n");
                  break;
                }

                if (err == -EWOULDBLOCK) {
                    status_t nRet = mSource->feedMoreTSData();
                    if (nRet == OK) {
                           msg->post(10000ll);
                    }
                    else if(nRet == (status_t)UNKNOWN_ERROR ||
                            nRet == (status_t)ERROR_DRM_CANNOT_HANDLE) {
                      // reply back to codec if there is an error
                      DP_MSG_ERROR("FeedMoreTSData error on track %d ",track);
                      if (track == kText) {
                        sendTextPacket(NULL, (status_t)UNKNOWN_ERROR);
                      } else {
                        sp<AMessage> reply;
                        CHECK(msg->findMessage("reply", &reply));
                        reply->setInt32("err", (status_t)UNKNOWN_ERROR);
                        reply->post();
                      }
                    }
                }

            } else if (what == Decoder::kWhatEOS) {
                DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatEOS");
                int32_t err;
                CHECK(msg->findInt32("err", &err));

                if (err == ERROR_END_OF_STREAM) {
                    DP_MSG_HIGH("got %s decoder EOS", mTrackName);
                } else {
                    DP_MSG_ERROR("got %s decoder EOS w/ error %d",
                         mTrackName,
                         err);
                }

                if(track == kVideo && mTimedTextCEAPresent)
                {
                  sendTextPacket(NULL, ERROR_END_OF_STREAM, TIMED_TEXT_CEA);
                }

                if(mRenderer != NULL)
                {
                  if((track == kAudio && !IsFlushingState(mFlushingAudio)) || (track == kVideo && !IsFlushingState(mFlushingVideo))) {
                    mRenderer->queueEOS(track, err);
                  }
                  else{
                    DP_MSG_ERROR("FlushingState for %s. Decoder EOS not queued to renderer", mTrackName);
                  }
                }
            } else if (what == Decoder::kWhatFlushCompleted) {
                DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatFlushCompleted");

                Mutex::Autolock autoLock(mLock);
                bool needShutdown = false;

                if (track == kAudio) {
                    if(IsFlushingState(mFlushingAudio, &needShutdown)) {
                        mFlushingAudio = FLUSHED;
                    }
                } else if (track == kVideo){
                    if(IsFlushingState(mFlushingVideo, &needShutdown)) {
                        mFlushingVideo = FLUSHED;
                    }

                    mVideoLateByUs = 0;
                }

                DP_MSG_MEDIUM("decoder %s flush completed", mTrackName);

                if (needShutdown) {
                    DP_MSG_HIGH("initiating %s decoder shutdown",
                           mTrackName);

                    if (track == kAudio) {
                        mAudioDecoder->initiateShutdown();
                        mFlushingAudio = SHUTTING_DOWN_DECODER;
                    } else if (track == kVideo) {
                        mVideoDecoder->initiateShutdown();
                        mFlushingVideo = SHUTTING_DOWN_DECODER;
                    }
                }

                finishFlushIfPossible();
            } else if (what == Decoder::kWhatOutputFormatChanged) {
                sp<AMessage> format;
                CHECK(msg->findMessage("format", &format));

                if (track == kAudio) {
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatOutputFormatChanged:: audio");
                    int32_t numChannels;
                    CHECK(format->findInt32("channel-count", &numChannels));

                    int32_t sampleRate;
                    CHECK(format->findInt32("sample-rate", &sampleRate));

                    DP_MSG_HIGH("Audio output format changed to %d Hz, %d channels",
                         sampleRate, numChannels);
                    if (mAudioSink != NULL)
                    {
                      mAudioSink->close();
                    }

                    audio_output_flags_t flags;
                    int64_t durationUs;
                    // FIXME: we should handle the case where the video decoder is created after
                    // we receive the format change indication. Current code will just make that
                    // we select deep buffer with video which should not be a problem as it should
                    // not prevent from keeping A/V sync.
                    if (mSource == NULL)
                    {
                      DP_MSG_ERROR("Source is null. Exit outputFormatChanged\n");
                      break;
                    }
                    if (mVideoDecoder == NULL &&
                            mSource->getDuration(&durationUs) == OK &&
                            durationUs > AUDIO_SINK_MIN_DEEP_BUFFER_DURATION_US) {
                        flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
                    } else {
                        flags = AUDIO_OUTPUT_FLAG_NONE;
                    }

                    int32_t channelMask;
                    if (!format->findInt32("channel-mask", &channelMask)) {
                        channelMask = CHANNEL_MASK_USE_CHANNEL_ORDER;
                    }

                    CHECK_EQ(mAudioSink->open(
                                sampleRate,
                                numChannels,
                                (audio_channel_mask_t)channelMask,
                                AUDIO_FORMAT_PCM_16_BIT,
                                8 /* bufferCount */,
                                NULL,
                                NULL,
                                flags),
                             (status_t)OK);
                    mAudioSink->start();

                    if(mRenderer != NULL) {
                        mRenderer->signalAudioSinkChanged();
                    }
                } else if (track == kVideo) {
                    // video
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatOutputFormatChanged:: video");

                    sp<AMessage> format;
                    CHECK(msg->findMessage("format", &format));
                    CHECK(format->findInt32("width", &mCurrentWidth));
                    CHECK(format->findInt32("height", &mCurrentHeight));
                    CHECK(format->findInt32("color-format", &mColorFormat));
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatOutputFormatChanged:: video new height:%d width%d", mCurrentWidth, mCurrentHeight);
                    CHECK(msg->findInt32("dpb-size", &mDPBSize));
                    // Port settings change. Reset below flag to check if
                    // decoderSetupTime < renderingtime#dpbframes in renderBuffer() and introduce rendering delay
                    mDPBCheckToDelayRendering = true;
                    mFirstVideoSampleUs = -1;
                }
            } else if (what == Decoder::kWhatShutdownCompleted) {
                DP_MSG_ERROR("%s shutdown completed", mTrackName);

                if((track == kAudio && mFlushingAudio == SHUT_DOWN)
                  || (track == kVideo && mFlushingVideo == SHUT_DOWN))
                {
                  return;
                }

                if (track == kAudio) {
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatShutdownCompleted:: audio");
                    if (mAudioDecoder != NULL) {
                        looper()->unregisterHandler(mAudioDecoder->id());
                    }
                    mAudioDecoder.clear();

                    mFlushingAudio = SHUT_DOWN;
                } else if (track == kVideo) {
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ kWhatShutdownCompleted:: Video");
                    if (mVideoDecoder != NULL) {
                        looper()->unregisterHandler(mVideoDecoder->id());
                    }
                    mVideoDecoder.clear();

                    mFlushingVideo = SHUT_DOWN;
                }

                finishFlushIfPossible();
            } else if (what == Decoder::kWhatError) {
                DP_MSG_ERROR("Received error from %s decoder, aborting playback.",
                       mTrackName);

                if(track == kVideo && mTimedTextCEAPresent)
                {
                  sendTextPacket(NULL, (status_t)UNKNOWN_ERROR, TIMED_TEXT_CEA);
                }

                if(mRenderer != NULL)
                {
                  if((track == kAudio && !IsFlushingState(mFlushingAudio)) ||
                     (track == kVideo && !IsFlushingState(mFlushingVideo)))
                  {
                    DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ Codec::kWhatError:: %s",track == kAudio ? "audio" : "video");
                    mRenderer->queueEOS(track, (status_t)UNKNOWN_ERROR);
                }
                  else{
                    DP_MSG_ERROR("EOS not queued for %d track", track);
                  }
                }
            } else if (what == Decoder::kWhatDrainThisBuffer) {
                if(track == kAudio || track == kVideo) {
                    DP_MSG_LOW("@@@@:: Dashplayer :: MESSAGE FROM CODEC +++++++++++++++++++++++++++++++ Codec::kWhatRenderBuffer:: %s",track == kAudio ? "audio" : "video");

                    // Compute video decoder setup time whenever new decoder is instantiated
                    // Used to compute startup delay for livestreams when high dpbSize
                    if (track == kVideo && mVideoDecoderSetupTimeUs == 0) {
                        mVideoDecoderSetupTimeUs = ALooper::GetNowUs() - mVideoDecoderStartTimeUs;
                    }
                    renderBuffer(track, msg);
                }
            } else {
                DP_MSG_LOW("Unhandled codec notification %d.", what);
            }

            break;
        }

        case kWhatRendererNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == Renderer::kWhatEOS) {
                int32_t audio;
                CHECK(msg->findInt32("audio", &audio));

                int32_t finalResult;
                CHECK(msg->findInt32("finalResult", &finalResult));
                DP_MSG_LOW("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatRendererNotify:: %s",audio ? "audio" : "video");
                if (audio) {
                    mAudioEOS = true;
                } else {
                    mVideoEOS = true;
                }

                if (finalResult == ERROR_END_OF_STREAM) {
                    DP_MSG_ERROR("reached %s EOS", audio ? "audio" : "video");
                } else {
                    DP_MSG_ERROR("%s track encountered an error (%d)",
                         audio ? "audio" : "video", finalResult);

                    notifyListener(
                            MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, finalResult);
                }

                if ((mAudioEOS || mAudioDecoder == NULL)
                        && (mVideoEOS || mVideoDecoder == NULL)) {
                      if (finalResult == ERROR_END_OF_STREAM) {
                           notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                      }
                }
            } else if (what == Renderer::kWhatPosition) {
                int64_t positionUs;
                CHECK(msg->findInt64("positionUs", &positionUs));

                CHECK(msg->findInt64("videoLateByUs", &mVideoLateByUs));
                DP_MSG_LOW("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatPosition:: position(%lld) VideoLateBy(%lld)",positionUs,mVideoLateByUs);
                if (mSource == NULL)
                {
                  DP_MSG_ERROR("Source is null. Exit Notifyposition\n");
                  break;
                }
                if (mDriver != NULL) {
                    sp<DashPlayerDriver> driver = mDriver.promote();
                    if (driver != NULL) {
                        driver->notifyPosition(positionUs);
                        mSource->notifyRenderingPosition(positionUs);
                    }
                }
            } else if (what == Renderer::kWhatFlushComplete) {
                CHECK_EQ(what, (int32_t)Renderer::kWhatFlushComplete);

                int32_t audio;
                CHECK(msg->findInt32("audio", &audio));
                DP_MSG_ERROR("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatFlushComplete:: %s",audio ? "audio" : "video");

            }
            break;
        }

        case kWhatReset:
        {
            DP_MSG_ERROR("kWhatReset");
            Mutex::Autolock autoLock(mLock);

            if (mRenderer != NULL) {
                // There's an edge case where the renderer owns all output
                // buffers and is paused, therefore the decoder will not read
                // more input data and will never encounter the matching
                // discontinuity. To avoid this, we resume the renderer.

                if (mFlushingAudio == AWAITING_DISCONTINUITY
                        || mFlushingVideo == AWAITING_DISCONTINUITY) {
                    mRenderer->resume();
                }
            }
            if ( (mAudioDecoder != NULL && IsFlushingState(mFlushingAudio)) ||
                 (mVideoDecoder != NULL && IsFlushingState(mFlushingVideo)) ) {

                // We're currently flushing, postpone the reset until that's
                // completed.

                DP_MSG_MEDIUM("postponing reset mFlushingAudio=%d, mFlushingVideo=%d",
                      mFlushingAudio, mFlushingVideo);

                mResetPostponed = true;
                break;
            }

            if (mAudioDecoder == NULL && mVideoDecoder == NULL) {
                finishReset();
                break;
            }

            mTimeDiscontinuityPending = true;

            if (mAudioDecoder != NULL) {
                flushDecoder(true /* audio */, true /* needShutdown */);
            }

            if (mVideoDecoder != NULL) {
                flushDecoder(false /* audio */, true /* needShutdown */);
            }

            mResetInProgress = true;
            break;
        }

        case kWhatSeek:
        {
            if(mStats != NULL) {
                mStats->notifySeek();
            }

            Mutex::Autolock autoLock(mLock);
            int64_t seekTimeUs = -1, newSeekTime = -1;
            status_t nRet = OK;
            CHECK(msg->findInt64("seekTimeUs", &seekTimeUs));
            if (mSource == NULL)
            {
              DP_MSG_ERROR("Source is null. Exit Seek\n");
              break;
            }

            DP_MSG_ERROR("kWhatSeek seekTimeUs=%lld us (%.2f secs)",
                 seekTimeUs, (double)seekTimeUs / 1E6);
            nRet = mSource->seekTo(seekTimeUs);

            if (nRet == OK) { // if seek success then flush the audio,video decoder and renderer
                mTimeDiscontinuityPending = true;
                bool audPresence = false;
                bool vidPresence = false;
                bool textPresence = false;
                mSource->getMediaPresence(audPresence,vidPresence,textPresence);
                mRenderer->setMediaPresence(true,audPresence); // audio
                mRenderer->setMediaPresence(false,vidPresence); // video
                if( (mVideoDecoder != NULL) &&
                    (mFlushingVideo == NONE || mFlushingVideo == AWAITING_DISCONTINUITY) ) {
                    flushDecoder( false, true ); // flush video, shutdown
                }

                if( (mAudioDecoder != NULL) &&
                    (mFlushingAudio == NONE|| mFlushingAudio == AWAITING_DISCONTINUITY) )
                {
                    flushDecoder( true, true );  // flush audio,  shutdown
                }
                if( mAudioDecoder == NULL ) {
                    DP_MSG_LOW("Audio is not there, set it to shutdown");
                    mFlushingAudio = SHUT_DOWN;
                }
                if( mVideoDecoder == NULL ) {
                    DP_MSG_LOW("Video is not there, set it to shutdown");
                    mFlushingVideo = SHUT_DOWN;
                }
            }
            else if (nRet != PERMISSION_DENIED) {
                mTimeDiscontinuityPending = true;
            }

            // get the new seeked position
            newSeekTime = seekTimeUs;
            DP_MSG_LOW("newSeekTime %lld", newSeekTime);
            mTimedTextCEASamplesDisc = true;

            if(mStats != NULL) {
                mStats->logSeek(seekTimeUs);
            }

            if (mDriver != NULL) {
                sp<DashPlayerDriver> driver = mDriver.promote();
                if (driver != NULL) {
                    if( newSeekTime >= 0 ) {
                        mRenderer->notifySeekPosition(newSeekTime);
                        driver->notifyPosition( newSeekTime );
                        mSource->notifyRenderingPosition(newSeekTime);
                        driver->notifySeekComplete();
                     }
                }
            }

            break;
        }

        case kWhatPause:
        {
            DP_MSG_ERROR("kWhatPause");
            CHECK(mRenderer != NULL);
            mRenderer->pause();

            mPauseIndication = true;

            Mutex::Autolock autoLock(mLock);
            if (mSource != NULL)
            {
              status_t nRet = mSource->pause();
            }

            break;
        }

        case kWhatResume:
        {
            DP_MSG_ERROR("kWhatResume");
            if (mSource == NULL)
            {
              DP_MSG_ERROR("Source is null. Exit Resume\n");
              break;
            }
            bool disc = mSource->isPlaybackDiscontinued();
            status_t status = OK;

            if (disc == true)
            {
              uint64_t nMin = 0, nMax = 0, nMaxDepth = 0;
              status = mSource->getRepositionRange(&nMin, &nMax, &nMaxDepth);
              if (status == OK)
              {
                int64_t seekTimeUs = (int64_t)nMin * 1000ll;
                DP_MSG_ERROR("kWhatSeek seekTimeUs=%lld us (%.2f secs)", seekTimeUs, (double)seekTimeUs / 1E6);
                status = mSource->seekTo(seekTimeUs);
                if (status == OK)
                {
                  // if seek success then flush the audio,video decoder and renderer
                  mTimeDiscontinuityPending = true;
                  bool audPresence = false;
                  bool vidPresence = false;
                  bool textPresence = false;
                  (void)mSource->getMediaPresence(audPresence,vidPresence,textPresence);
                  mRenderer->setMediaPresence(true,audPresence); // audio
                  mRenderer->setMediaPresence(false,vidPresence); // video
                  if( (mVideoDecoder != NULL) &&
                    (mFlushingVideo == NONE || mFlushingVideo == AWAITING_DISCONTINUITY) ) {
                      flushDecoder( false, true ); // flush video, shutdown
                  }

                  if( (mAudioDecoder != NULL) &&
                    (mFlushingAudio == NONE|| mFlushingAudio == AWAITING_DISCONTINUITY) )
                  {
                    flushDecoder( true, true );  // flush audio,  shutdown
                  }
                  if( mAudioDecoder == NULL ) {
                    DP_MSG_MEDIUM("Audio is not there, set it to shutdown");
                    mFlushingAudio = SHUT_DOWN;
                  }
                  if( mVideoDecoder == NULL ) {
                    DP_MSG_MEDIUM("Video is not there, set it to shutdown");
                    mFlushingVideo = SHUT_DOWN;
                  }

                  if (mDriver != NULL)
                  {
                    sp<DashPlayerDriver> driver = mDriver.promote();
                    if (driver != NULL)
                    {
                      if( seekTimeUs >= 0 ) {
                        mRenderer->notifySeekPosition(seekTimeUs);
                        driver->notifyPosition( seekTimeUs );
                      }
                    }
                  }

                  mTimedTextCEASamplesDisc = true;
                }
              }
            }

            if (status != OK && status != ERROR_END_OF_STREAM)
            {
              //Notify error?
              DP_MSG_ERROR(" Dash Source playback discontinuity check failure");
              notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, status);
            }

            Mutex::Autolock autoLock(mLock);
            if (mSource != NULL) {
              status_t nRet = mSource->resume();
            }

            if (mAudioDecoder == NULL || mVideoDecoder == NULL || mTextDecoder == NULL) {
                mScanSourcesPending = false;
                postScanSources();
            }

            CHECK(mRenderer != NULL);
            mRenderer->resume();

            mPauseIndication = false;

            break;
        }

        case kWhatPrepareAsync:
            if (mSource == NULL)
            {
                DP_MSG_ERROR("Source is null in prepareAsync\n");
                break;
            }

            DP_MSG_ERROR("kWhatPrepareAsync");
            mSource->prepareAsync();
            postIsPrepareDone();
            break;

        case kWhatIsPrepareDone:
            if (mSource == NULL)
            {
                DP_MSG_ERROR("Source is null when checking for prepare done\n");
                break;
            }

            status_t err;
            err = mSource->isPrepareDone();
            if(err == OK) {
                int64_t durationUs;
                if (mDriver != NULL && mSource->getDuration(&durationUs) == OK) {
                    sp<DashPlayerDriver> driver = mDriver.promote();
                    if (driver != NULL) {
                        driver->notifyDuration(durationUs);
                    }
                }
                DP_MSG_ERROR("PrepareDone complete\n");
                notifyListener(MEDIA_PREPARED, 0, 0);
            } else if(err == -EWOULDBLOCK) {
                msg->post(100000ll);
            } else {
              DP_MSG_ERROR("Prepareasync failed\n");
              notifyListener(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
            }
            break;
        case kWhatSourceNotify:
        {
            Mutex::Autolock autoLock(mLock);
            DP_MSG_ERROR("kWhatSourceNotify");

            if(mSource != NULL) {
                int64_t track;

                sp<AMessage> sourceRequest;
                DP_MSG_MEDIUM("kWhatSourceNotify - looking for source-request");

                // attempt to find message by different names
                bool msgFound = msg->findMessage("source-request", &sourceRequest);
                int32_t handled;
                if (!msgFound){
                    DP_MSG_MEDIUM("kWhatSourceNotify source-request not found, trying using sourceRequestID");
                    char srName[] = "source-request00";
                    (void)snprintf(srName, sizeof(srName), "source-request%d%d", mSRid/10, mSRid%10);
                    msgFound = msg->findMessage(srName, &sourceRequest);
                    if(msgFound)
                        mSRid = (mSRid+1)%SRMax;
                }

                if(msgFound)
                {
                    int32_t what;
                    CHECK(sourceRequest->findInt32("what", &what));

                    if (what == kWhatBufferingStart) {
                    sourceRequest->findInt64("track", &track);
                    getTrackName((int)track,mTrackName);
                      DP_MSG_ERROR("Source Notified Buffering Start for %s ",mTrackName);
                      if (mBufferingNotification == false) {
                          if (track == kVideo && mNativeWindow == NULL)
                          {
                               DP_MSG_ERROR("video decoder not instantiated, no buffering for video(%d)",
                                     mBufferingNotification);
                          }
                          else
                          {
                              mBufferingNotification = true;
                              notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_START, 0);
                          }
                      }
                      else {
                         DP_MSG_MEDIUM("Buffering Start Event Already Notified mBufferingNotification(%d)",
                               mBufferingNotification);
                      }
                    }
                    else if(what == kWhatBufferingEnd) {
                    sourceRequest->findInt64("track", &track);
                    getTrackName((int)track,mTrackName);
                        if (mBufferingNotification) {
                          DP_MSG_ERROR("Source Notified Buffering End for %s ",mTrackName);
                                mBufferingNotification = false;
                          notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_END, 0);
                          if(mStats != NULL) {
                            mStats->notifyBufferingEvent();
                          }
                        }
                        else {
                          DP_MSG_MEDIUM("No need to notify Buffering end as mBufferingNotification is (%d) "
                                ,mBufferingNotification);
                        }
                    }
                }
            }
            else {
                 DP_MSG_ERROR("kWhatSourceNotify - Source object does not exist anymore");
            }
            break;
       }
       case kWhatQOE:
           {
               sp<AMessage> dataQOE;
               Parcel notifyDataQOE;
               int64_t timeofday;
               bool msgFound = msg->findMessage("QOEData", &dataQOE);
               if (msgFound)
               {
                 int32_t what;
                 CHECK(dataQOE->findInt32("what", &what));
                 if (what == kWhatQOEPlay)
                 {
                   dataQOE->findInt64("timeofday",&timeofday);

                   notifyDataQOE.writeInt64(timeofday);
                 }
                 else if (what == kWhatQOEStop)
                 {
                   int32_t bandwidth = 0;
                   int32_t reBufCount = 0;
                   int32_t stopSize = 0;
                   int32_t videoSize = 0;
                   AString stopPhrase;
                   AString videoUrl;

                   dataQOE->findInt64("timeofday",&timeofday);
                   dataQOE->findInt32("bandwidth",&bandwidth);
                   dataQOE->findInt32("rebufct",&reBufCount);
                   dataQOE->findInt32("sizestopphrase",&stopSize);
                   dataQOE->findString("stopphrase",&stopPhrase);
                   dataQOE->findInt32("sizevideo",&videoSize);
                   dataQOE->findString("videourl",&videoUrl);

                   notifyDataQOE.writeInt32(bandwidth);
                   notifyDataQOE.writeInt32(reBufCount);
                   notifyDataQOE.writeInt64(timeofday);
                   notifyDataQOE.writeInt32(stopSize);
                   notifyDataQOE.writeInt32(stopSize);
                   stopPhrase.append('\0');
                   notifyDataQOE.write((const uint8_t *)stopPhrase.c_str(), stopSize+1);
                   notifyDataQOE.writeInt32(videoSize);
                   notifyDataQOE.writeInt32(videoSize);
                   videoUrl.append('\0');
                   notifyDataQOE.write((const uint8_t *)videoUrl.c_str(), videoSize+1);

                 }
                 else if (what == kWhatQOESwitch)
                 {
                   int32_t bandwidth = 0;
                   int32_t reBufCount = 0;

                   dataQOE->findInt64("timeofday",&timeofday);
                   dataQOE->findInt32("bandwidth",&bandwidth);
                   dataQOE->findInt32("rebufct",&reBufCount);

                   notifyDataQOE.writeInt32(bandwidth);
                   notifyDataQOE.writeInt32(reBufCount);
                   notifyDataQOE.writeInt64(timeofday);
                 }
                 notifyListener(MEDIA_QOE,kWhatQOE,what,&notifyDataQOE);
               }
           break;
           }

        default:
            TRESPASS();
            break;
    }
}

void DashPlayer::finishFlushIfPossible() {

    /* check if Audio Decoder has been shutdown for handling audio discontinuity
       ,in that case Audio decoder has to be reinstaniated*/
    if (mAudioDecoder == NULL && (mFlushingAudio == SHUT_DOWN) &&
        !mResetInProgress && !mResetPostponed &&
        ((mVideoDecoder != NULL) && (mFlushingVideo == NONE || mFlushingVideo == AWAITING_DISCONTINUITY)))
    {
        DP_MSG_LOW("Resuming Audio after Shutdown(Discontinuity)");
        mFlushingAudio = NONE;
        postScanSources();
        return;
    }
    //If reset was postponed after one of the streams is flushed, complete it now
    if (mResetPostponed) {
        DP_MSG_LOW("finishFlushIfPossible Handle reset postpone ");
        if ((mAudioDecoder != NULL) &&
            (mFlushingAudio == NONE || mFlushingAudio == AWAITING_DISCONTINUITY )) {
           flushDecoder( true, true );
        }
        if ((mVideoDecoder != NULL) &&
            (mFlushingVideo == NONE || mFlushingVideo == AWAITING_DISCONTINUITY )) {
           flushDecoder( false, true );
        }
    }

    //Check if both audio & video are flushed
    if (mFlushingAudio != FLUSHED && mFlushingAudio != SHUT_DOWN) {
        DP_MSG_LOW("Dont finish flush, audio is in state %d ", mFlushingAudio);
        return;
    }

    if (mFlushingVideo != FLUSHED && mFlushingVideo != SHUT_DOWN) {
        DP_MSG_LOW("Dont finish flush, video is in state %d ", mFlushingVideo);
        return;
    }

    DP_MSG_HIGH("both audio and video are flushed now.");

    if ((mRenderer != NULL) && (mTimeDiscontinuityPending) &&
         !isSetSurfaceTexturePending) {
        mRenderer->signalTimeDiscontinuity();
        mTimeDiscontinuityPending = false;
    }

    if (mAudioDecoder != NULL) {
        DP_MSG_LOW("Resume Audio after flush");
        mAudioDecoder->signalResume();
    }

    if (mVideoDecoder != NULL) {
        DP_MSG_LOW("Resume Video after flush");
        mVideoDecoder->signalResume();
    }

    mFlushingAudio = NONE;
    mFlushingVideo = NONE;

    if (mResetInProgress) {
        DP_MSG_ERROR("reset completed");

        mResetInProgress = false;
        finishReset();
    } else if (mResetPostponed) {
        (new AMessage(kWhatReset, this))->post();
        mResetPostponed = false;
        DP_MSG_LOW("Handle reset postpone");
    }else if(isSetSurfaceTexturePending){
        processDeferredActions();
        DP_MSG_ERROR("DashPlayer::finishFlushIfPossible() setsurfacetexturepending=true");
    } else if (mAudioDecoder == NULL || mVideoDecoder == NULL) {
        DP_MSG_LOW("Start scanning for sources after shutdown");
        if (mTextDecoder != NULL)
        {
          if (mSource != NULL) {
           DP_MSG_LOW("finishFlushIfPossible calling mSource->stop");
           mSource->stop();
          }
          sp<AMessage> codecRequest;
          mTextNotify->findMessage("codec-request", &codecRequest);
          codecRequest = NULL;
          mTextNotify = NULL;
          looper()->unregisterHandler(mTextDecoder->id());
          mTextDecoder.clear();
        }
        postScanSources();
    }
}

void DashPlayer::finishReset() {
    CHECK(mAudioDecoder == NULL);
    CHECK(mVideoDecoder == NULL);

    ++mScanSourcesGeneration;
    mScanSourcesPending = false;

    if (mRenderer != NULL) {
        looper()->unregisterHandler(mRenderer->id());
        mRenderer.clear();
    }

    if (mSource != NULL) {
        DP_MSG_ERROR("finishReset calling mSource->stop");
        mSource->stop();
        mSource.clear();
    }

    if ( (mTextDecoder != NULL) && (mTextNotify != NULL))
    {
      sp<AMessage> codecRequest;
      mTextNotify->findMessage("codec-request", &codecRequest);
      codecRequest = NULL;
      mTextNotify = NULL;
      looper()->unregisterHandler(mTextDecoder->id());
      mTextDecoder.clear();
      DP_MSG_ERROR("Text Dummy Decoder Deleted");
    }
    if (mSourceNotify != NULL)
    {
       sp<AMessage> sourceRequest;
       mSourceNotify->findMessage("source-request", &sourceRequest);
       sourceRequest = NULL;
       for (int id = 0; id < SRMax; id++){
           char srName[] = "source-request00";
           (void)snprintf(srName, sizeof(srName), "source-request%d%d", id/10, id%10);
           mSourceNotify->findMessage(srName, &sourceRequest);
           sourceRequest = NULL;
       }
       mSourceNotify = NULL;
    }

    if (mDriver != NULL) {
        sp<DashPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifyResetComplete();
        }
    }
}

void DashPlayer::postScanSources() {
    if (mScanSourcesPending) {
        return;
    }

    sp<AMessage> msg = new AMessage(kWhatScanSources, this);
    msg->setInt32("generation", mScanSourcesGeneration);
    msg->post();

    mScanSourcesPending = true;
}

status_t DashPlayer::instantiateDecoder(int track, sp<Decoder> *decoder) {
    DP_MSG_LOW("@@@@:: instantiateDecoder Called ");
    if (*decoder != NULL) {
        return OK;
    }
    if (mSource == NULL)
    {
      DP_MSG_ERROR("Source is null. Exit instantiateDecoder\n");
      return -EWOULDBLOCK;
    }

    sp<MetaData> meta = mSource->getFormat(track);

    if (meta == NULL) {
        return -EWOULDBLOCK;
    }

    if (track == kVideo) {
        const char *mime = NULL;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        mVideoIsAVC = !strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime);
        if(mStats != NULL) {
            mStats->setMime(mime);
        }

        if (mSetVideoSize) {
            int32_t width = 0;
            meta->findInt32(kKeyWidth, &width);
            int32_t height = 0;
            meta->findInt32(kKeyHeight, &height);
            DP_MSG_HIGH("instantiate video decoder, send wxh = %dx%d",width,height);
            notifyListener(MEDIA_SET_VIDEO_SIZE, width, height);
            mSetVideoSize = false;
        }
    }

    sp<AMessage> notify;
    if (track == kAudio) {
        notify = new AMessage(kWhatAudioNotify ,this);
        DP_MSG_HIGH("Creating Audio Decoder ");
        *decoder = new Decoder(notify);
        DP_MSG_LOW("@@@@:: setting Sink/Renderer pointer to decoder");
         if (mRenderer != NULL) {
            mRenderer->setMediaPresence(true,true);
        }

        mLastReadAudioMediaTimeUs = -1;

    } else if (track == kVideo) {
        notify = new AMessage(kWhatVideoNotify ,this);
        *decoder = new Decoder(notify, mNativeWindow);
        DP_MSG_HIGH("Creating Video Decoder ");
        if (mRenderer != NULL) {
            mRenderer->setMediaPresence(false,true);
        }

        mVideoDecoderSetupTimeUs = 0;
        mVideoDecoderStartTimeUs = ALooper::GetNowUs();
        mDelayRenderingUs = 0;

    } else if (track == kText) {
        mTextNotify = new AMessage(kWhatTextNotify ,this);
        *decoder = new Decoder(mTextNotify);
        sp<AMessage> codecRequest = new AMessage;
        codecRequest->setInt32("what", Decoder::kWhatFillThisBuffer);
        mTextNotify->setMessage("codec-request", codecRequest);
        DP_MSG_HIGH("Creating Dummy Text Decoder ");
        if (mSource != NULL) {
           mSource->setupSourceData(mTextNotify, track);
        }
    }

    if(track != kAudio && track != kVideo)
    {
      looper()->registerHandler(*decoder);
    }

    char value[PROPERTY_VALUE_MAX] = {0};
    //Set flushing state to none
    Mutex::Autolock autoLock(mLock);
    if(track == kAudio) {
        mFlushingAudio = NONE;
    } else if (track == kVideo) {
        mFlushingVideo = NONE;
    }

    if( (track == kAudio || track == kVideo) && ((*decoder) != NULL)) {
        (*decoder)->init();
        (*decoder)->configure(meta);
    }

    int64_t durationUs;
    if (mDriver != NULL && mSource->getDuration(&durationUs) == OK) {
        sp<DashPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifyDuration(durationUs);
        }
    }

    return OK;
}

status_t DashPlayer::feedDecoderInputData(int track, const sp<AMessage> &msg) {
    sp<AMessage> reply;

    if ( (track != kText) && !(msg->findMessage("reply", &reply)))
    {
       CHECK(msg->findMessage("reply", &reply));
    }

    {
        Mutex::Autolock autoLock(mLock);

        if (reply != NULL && (((track == kAudio) && IsFlushingState(mFlushingAudio))
            || ((track == kVideo) && IsFlushingState(mFlushingVideo))
            || mSource == NULL)) {
            reply->setInt32("err", INFO_DISCONTINUITY);
            reply->post();
            return OK;
        }
    }

    getTrackName(track,mTrackName);

    sp<ABuffer> accessUnit;

    bool dropAccessUnit;
    do {

        status_t err = (status_t)UNKNOWN_ERROR;

        err = mSource->dequeueAccessUnit(track, &accessUnit);

        if (err == -EWOULDBLOCK) {
            return err;
        } else if (err != OK) {
            if (err == INFO_DISCONTINUITY) {
                int32_t type;
                CHECK(accessUnit->meta()->findInt32("discontinuity", &type));

                bool formatChange =
                    ((track == kAudio) &&
                     (type & ATSParser::DISCONTINUITY_AUDIO_FORMAT))
                    || ((track == kVideo) &&
                            (type & ATSParser::DISCONTINUITY_VIDEO_FORMAT));

                bool timeChange = (type & ATSParser::DISCONTINUITY_TIME) != 0;

                DP_MSG_HIGH("%s discontinuity (formatChange=%d, time=%d)",
                     mTrackName, formatChange, timeChange);

                if (track == kAudio) {
                    mSkipRenderingAudioUntilMediaTimeUs = -1;
                } else if (track == kVideo) {
                    mSkipRenderingVideoUntilMediaTimeUs = -1;
                }

                if (timeChange) {
                    sp<AMessage> extra;
                    if (accessUnit->meta()->findMessage("extra", &extra)
                            && extra != NULL) {
                        int64_t resumeAtMediaTimeUs;
                        if (extra->findInt64(
                                    "resume-at-mediatimeUs", &resumeAtMediaTimeUs)) {
                            DP_MSG_HIGH("suppressing rendering of %s until %lld us",
                                    mTrackName, resumeAtMediaTimeUs);

                            if (track == kAudio) {
                                mSkipRenderingAudioUntilMediaTimeUs =
                                    resumeAtMediaTimeUs;
                            } else if (track == kVideo) {
                                mSkipRenderingVideoUntilMediaTimeUs =
                                    resumeAtMediaTimeUs;
                            }
                        }
                    }
                }

                mTimeDiscontinuityPending =
                    mTimeDiscontinuityPending || timeChange;

                if (formatChange || timeChange) {
                    flushDecoder(track, formatChange);
                } else {
                    // This stream is unaffected by the discontinuity

                    if (track == kAudio) {
                        mFlushingAudio = FLUSHED;
                    } else if (track == kVideo) {
                        mFlushingVideo = FLUSHED;
                    }

                    finishFlushIfPossible();

                    return -EWOULDBLOCK;
                }
            }

            if ( (track == kAudio) ||
                 (track == kVideo))
            {
               reply->setInt32("err", err);
               reply->post();
               return OK;
            }
            else if ((track == kText) &&
                     (err == ERROR_END_OF_STREAM || err == (status_t)UNKNOWN_ERROR)) {
               DP_MSG_ERROR("Text track has encountered error %d", err );
               sendTextPacket(NULL, err);
               return err;
            }
        }

        if (mSource != NULL && mSource->isLiveStream() &&
                      track == kAudio && err == OK) {
            int64_t timeUs;
            CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

            if (timeUs >=0 && mLastReadAudioMediaTimeUs >= 0 &&
                    ((timeUs - mLastReadAudioMediaTimeUs) > AUDIO_TS_DISCONTINUITY_THRESHOLD) &&
                    ((ALooper::GetNowUs() - mLastReadAudioRealTimeUs) > AUDIO_DISCONTINUITY_THRESHOLD)) {
                if (mRenderer != NULL && mDPBSize > 0 && mVideoSampleDurationUs > 0) {
                    mRenderer->queueDelay(mDPBSize * mVideoSampleDurationUs);
                }
            }
            mLastReadAudioMediaTimeUs = timeUs;
            mLastReadAudioRealTimeUs = ALooper::GetNowUs();
        }

        dropAccessUnit = false;
        if (track == kVideo) {

            if(mStats != NULL) {
                mStats->incrementTotalFrames();
            }

            if (mVideoLateByUs > 100000ll
                    && mVideoIsAVC
                    && !IsAVCReferenceFrame(accessUnit)) {
                dropAccessUnit = true;
                if(mStats != NULL) {
                    mStats->incrementDroppedFrames();
                }
            }
        }
    } while (dropAccessUnit);

    // DP_MSG_LOW("returned a valid buffer of %s data", mTrackName);

    if (track == kVideo || track == kAudio) {
        reply->setBuffer("buffer", accessUnit);
        reply->post();
    } else if (track == kText) {
        sendTextPacket(accessUnit,OK);
        if (mSource != NULL) {
          mSource->postNextTextSample(accessUnit,mTextNotify,track);
        }
    }
    return OK;
}

void DashPlayer::renderBuffer(bool audio, const sp<AMessage> &msg) {
    // DP_MSG_LOW("renderBuffer %s", audio ? "audio" : "video");

    sp<AMessage> reply;
    CHECK(msg->findMessage("reply", &reply));

    Mutex::Autolock autoLock(mLock);
    if (IsFlushingState(audio ? mFlushingAudio : mFlushingVideo)) {
        // We're currently attempting to flush the decoder, in order
        // to complete this, the decoder wants all its buffers back,
        // so we don't want any output buffers it sent us (from before
        // we initiated the flush) to be stuck in the renderer's queue.

        DP_MSG_MEDIUM("we're still flushing the %s decoder, sending its output buffer"
             " right back.", audio ? "audio" : "video");

        reply->post();
        return;
    }

    sp<ABuffer> buffer;
    CHECK(msg->findBuffer("buffer", &buffer));

    int64_t &skipUntilMediaTimeUs =
        audio
            ? mSkipRenderingAudioUntilMediaTimeUs
            : mSkipRenderingVideoUntilMediaTimeUs;

    if (skipUntilMediaTimeUs >= 0) {
        int64_t mediaTimeUs;
        CHECK(buffer->meta()->findInt64("timeUs", &mediaTimeUs));

        if (mediaTimeUs < skipUntilMediaTimeUs) {
            DP_MSG_HIGH("dropping %s buffer at time %lld as requested.",
                 audio ? "audio" : "video",
                 mediaTimeUs);

            reply->post();
            return;
        }

        skipUntilMediaTimeUs = -1;
    }

    if(mRenderer != NULL)
    {
      if(!audio)
      {
        int32_t extradata = 0;

        if (buffer->meta()->findInt32("extradata", &extradata) && 1 == extradata)
        {
          DP_MSG_HIGH("kwhatdrainthisbuffer: Decoded sample contains SEI. Parse for CEA encoded cc extradata");

          sp<RefBase> obj;

          if (buffer->meta()->findObject("graphic-buffer", &obj))
          {
            sp<GraphicBuffer> graphicBuffer = static_cast<GraphicBuffer*>(obj.get());
            if (graphicBuffer != NULL)
            {
              DP_MSG_LOW("kwhatdrainthisbuffer: Extradata present",
                  "graphicBuffer = %p, width=%d height=%d color-format=%d",
                  graphicBuffer.get(), mCurrentWidth, mCurrentHeight, mColorFormat);

              if (mColorFormat == 0x7FA30C04 /*OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m*/)
              {
                size_t filledLen = (VENUS_Y_STRIDE(COLOR_FMT_NV12, mCurrentWidth)
                                    * VENUS_Y_SCANLINES(COLOR_FMT_NV12, mCurrentHeight))
                                          +  (VENUS_UV_STRIDE(COLOR_FMT_NV12, mCurrentWidth)
                                              * VENUS_UV_SCANLINES(COLOR_FMT_NV12, mCurrentHeight));
                size_t allocLen = VENUS_BUFFER_SIZE(COLOR_FMT_NV12, mCurrentWidth, mCurrentHeight);
                size_t offset = buffer->offset();

                DP_MSG_LOW("kwhatdrainthisbuffer: decoded buffer ranges "
                  "filledLen = %lu, allocLen = %lu, startOffset = %lu",
                  filledLen, allocLen, offset);

                // 'lock' returns mapped virtual address that can be read from or written into
                //  Use GRALLOC_USAGE_SW_READ_MASK / WRITE_MASK to indicate access type
                // 'unlock' will unmap the mapped address

                void *yuvData = NULL;
                graphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, &yuvData);

                if (yuvData)
                {
                  OMX_OTHER_EXTRADATATYPE *pExtra;
                  pExtra = (OMX_OTHER_EXTRADATATYPE *)((unsigned long)((OMX_U8*)yuvData + offset + filledLen + 3)&(~3));

                  while (pExtra &&
                    ((OMX_U8*)pExtra + pExtra->nSize) <= ((OMX_U8*)yuvData + allocLen) &&
                    pExtra->eType != OMX_ExtraDataNone )
                  {
                    DP_MSG_LOW(
                      "============== Extra Data ==============\n"
                      "           Size: %lu\n"
                      "        Version: %lu\n"
                      "      PortIndex: %lu\n"
                      "           Type: %x\n"
                      "       DataSize: %lu",
                      pExtra->nSize, pExtra->nVersion.nVersion,
                      pExtra->nPortIndex, pExtra->eType, pExtra->nDataSize);

                    if(pExtra->eType == (OMX_EXTRADATATYPE) OMX_ExtraDataMP2UserData)
                    {
                      OMX_QCOM_EXTRADATA_USERDATA *userdata = (OMX_QCOM_EXTRADATA_USERDATA *)pExtra->data;
                      OMX_U8 *data_ptr = (OMX_U8 *)userdata->data;
                      OMX_U32 userdata_size = pExtra->nDataSize - sizeof(userdata->type);

                      DP_MSG_LOW(
                        "--------------  OMX_ExtraDataMP2UserData Userdata  -------------\n"
                        "    Stream userdata type: %lu\n"
                        "           userdata size: %lu\n"
                        "         STREAM_USERDATA:",
                        userdata->type, userdata_size);

                      for (uint32_t i = 0; i < userdata_size; i+=4) {
                        DP_MSG_LOW("        %x %x %x %x",
                          data_ptr[i], data_ptr[i+1],
                          data_ptr[i+2], data_ptr[i+3]);
                      }

                      DP_MSG_LOW(
                        "-------------- End of OMX_ExtraDataMP2UserData Userdata -----------");

                      /*
                      SEI Syntax

                      user_data_registered_itu_t_t35 ( ) {
                      itu_t_t35_country_code (8 bits)
                      itu_t_t35_provider_code (16 bits)
                      user_identifier (32 bits)
                      user_structure( )
                      }

                      cc_data parsing logic
                      1. itu_t_t35_country_code - A fixed 8-bit field, the value of which shall be 0xB5.3
                      itu_t_35_provider_code - A fixed 16-bit field, the value of which shall be 0x0031.
                      2. user_identifier should match 0x47413934 ('GA94') ATSC_user_data( )

                      ATSC_user_data Syntax
                      ATSC_user_data() {
                      user_data_type_code (8 bits)
                      user_data_type_structure()
                      }

                      3. user_data_type_code should match 0x03 MPEG_cc_data()

                      */

                      if(0xB5 == data_ptr[0] && 0x00 == data_ptr[1] && 0x31 == data_ptr[2]
                      && 0x47 == data_ptr[3] && 0x41 == data_ptr[4] && 0x39 == data_ptr[5] && 0x34 == data_ptr[6]
                      && 0x03 == data_ptr[7])
                      {
                        DP_MSG_HIGH("SEI payload user_data_type_code is CEA encoded MPEG_cc_data()");

                        OMX_U32 cc_data_size = 0;
                        for(int i = 8; data_ptr[i] != 0xFF /*each cc_data ends with marker bits*/; i++)
                        {
                          cc_data_size++;
                        }

                        if(cc_data_size > 0)
                        {
                          DP_MSG_LOW(
                            "--------------  MPEG_cc_data()  -------------\n"
                            "    cc_data ptr: %p cc_data_size: %lu\n",
                            &data_ptr[8], cc_data_size);

                          for (uint32_t i = 8; i < 8 + cc_data_size; i+=4) {
                            DP_MSG_LOW("        %x %x %x %x",
                              data_ptr[i], data_ptr[i+1],
                              data_ptr[i+2], data_ptr[i+3]);
                          }

                          DP_MSG_LOW(
                            "--------------  End of MPEG_cc_data()  -------------\n");

                          sp<ABuffer> accessUnit = new ABuffer((OMX_U8*)&data_ptr[8], cc_data_size);

                          int64_t mediaTimeUs;

                          sp<ABuffer> buffer;
                          CHECK(msg->findBuffer("buffer", &buffer));
                          CHECK(buffer->meta()->findInt64("timeUs", &mediaTimeUs));
                          accessUnit->meta()->setInt64("timeUs",mediaTimeUs);

                          //To signal discontinuity in samples during seek and resume-out-of-tsb(internal seek) operations
                          if(mTimedTextCEASamplesDisc)
                          {
                            accessUnit->meta()->setInt32("disc", 1);
                            mTimedTextCEASamplesDisc = false;
                          }

                          //Indicate timedtext CEA present in stream. Used to signal EOS in Codec::kWhatEOS
                          if(!mTimedTextCEAPresent)
                          {
                            mTimedTextCEAPresent = true;
                          }

                          sendTextPacket(accessUnit, OK, TIMED_TEXT_CEA);

                          accessUnit = NULL;
                          break;
                        }
                      }
                    }

                    pExtra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) pExtra) + pExtra->nSize);
                  }
                  graphicBuffer->unlock();
                }
              }
            }
          }
        }
      }

      /* This code handles freezes in foll. live embms use case for stream needing high DPB size.
         Firmware will only return FBD's if the dpb (decoded picutre buffer) list is full. i.e.
         if number of decoded frames in the dpb list falls below the dpb capacity fw will not
         issue FBD to output buffers.

         In a typical live scenario where we play current segment and next segment is only available
         for download in the next available segment time, if the dpb size is high a lot of
         frames are held by the fw toward the end of current segment before the next segment download
         completes and sends samples on input port for decoding. This will cause momentary freezes
         at the boundary of each segment for clips with high DPB.

         Fix here is to adds a delay before rendering starts so that the media samples rendering cycle
         is pushed ahead. This ensures the dpb queue is not running dry toward the end of current segment
         and by then next segment has become available, downloaded and samples are sent.

         Below condition adds this initial delay only for live content type where dpb size is high such that
         mDecoderSetupTime < rendering time of #dpbSize number of frames

         Equation:
         if(decoderSetupTime < renderingTime for #dpb frames(i.e. dpbSize x sample duration))
         {
             introduce start up delay in rendering =
                 renderingTime for #dpb frames - decoderSetupTime
         } */

      if (mSource != NULL && mSource->isLiveStream()
              && !audio && mDPBCheckToDelayRendering) {

          int64_t mediaTimeUs = 0;
          CHECK(buffer->meta()->findInt64("timeUs", &mediaTimeUs));

          if (mFirstVideoSampleUs < 0) {
              mFirstVideoSampleUs = mediaTimeUs;
          } else {
              mVideoSampleDurationUs = mediaTimeUs - mFirstVideoSampleUs;

              if (mDPBSize > 0 && mVideoSampleDurationUs > 0 &&
                            (mVideoDecoderSetupTimeUs < (mDPBSize * mVideoSampleDurationUs))) {
                  int delayRenderingUs = ((mDPBSize * mVideoSampleDurationUs) - mVideoDecoderSetupTimeUs);

                  if (mDelayRenderingUs < delayRenderingUs) {
                      DP_MSG_ERROR("videoDecoderSetupTime(%lld msec) < rendering time(%lld msec) of #dpbSize(%d) frames with sample duration(%llu msec)."
                                   "rendering delay queued up to now = %lld msec,  queue additional delay = %lld msec",
                                      (int64_t)mVideoDecoderSetupTimeUs/1000,
                                      (int64_t)(double)(mDPBSize*mVideoSampleDurationUs/1000),
                                      mDPBSize,
                                      (int64_t)mVideoSampleDurationUs/1000,
                                      (int64_t)mDelayRenderingUs/1000,
                                      (int64_t)((delayRenderingUs - mDelayRenderingUs)/1000));
                      mRenderer->queueDelay((delayRenderingUs - mDelayRenderingUs));
                      mDelayRenderingUs = delayRenderingUs;
                  }
              }
              mDPBCheckToDelayRendering = false;
          }
      }

      mRenderer->queueBuffer(audio, buffer, reply);
    }
}

void DashPlayer::notifyListener(int msg, int ext1, int ext2, const Parcel *obj) {
    if (mDriver == NULL) {
        return;
    }

    sp<DashPlayerDriver> driver = mDriver.promote();

    if (driver == NULL) {
        return;
    }

        driver->notifyListener(msg, ext1, ext2, obj);
}

void DashPlayer::flushDecoder(bool audio, bool needShutdown) {
    if ((audio && mAudioDecoder == NULL) || (!audio && mVideoDecoder == NULL)) {
        DP_MSG_HIGH("flushDecoder %s without decoder present",
             audio ? "audio" : "video");
    }

    // Make sure we don't continue to scan sources until we finish flushing.
    ++mScanSourcesGeneration;
    mScanSourcesPending = false;

    (audio ? mAudioDecoder : mVideoDecoder)->signalFlush();

    if(mRenderer != NULL) {
        mRenderer->flush(audio);
    }

    FlushStatus newStatus =
        needShutdown ? FLUSHING_DECODER_SHUTDOWN : FLUSHING_DECODER;

    if (audio) {
        CHECK(mFlushingAudio == NONE
                || mFlushingAudio == AWAITING_DISCONTINUITY);

        mFlushingAudio = newStatus;

        if (mFlushingVideo == NONE) {
            mFlushingVideo = (mVideoDecoder != NULL)
                ? AWAITING_DISCONTINUITY
                : FLUSHED;
        }
    } else {
        CHECK(mFlushingVideo == NONE
                || mFlushingVideo == AWAITING_DISCONTINUITY);

        mFlushingVideo = newStatus;

        if (mFlushingAudio == NONE) {
            mFlushingAudio = (mAudioDecoder != NULL)
                ? AWAITING_DISCONTINUITY
                : FLUSHED;
        }
    }
}

sp<DashPlayer::Source>
    DashPlayer::LoadCreateSource(const char * uri, const KeyedVector<String8,String8> *headers,
                               bool uidValid, uid_t uid)
{
   const char* STREAMING_SOURCE_LIB = "libmmipstreamaal.so";
   const char* DASH_HTTP_LIVE_CREATE_SOURCE = "CreateDashHttpLiveSource";
   void* pStreamingSourceLib = NULL;

   typedef DashPlayer::Source* (*SourceFactory)(const char * uri, const KeyedVector<String8, String8> *headers, bool uidValid, uid_t uid);

   /* Open librery */
   pStreamingSourceLib = ::dlopen(STREAMING_SOURCE_LIB, RTLD_LAZY);

   if (pStreamingSourceLib == NULL) {
       DP_MSG_ERROR("@@@@:: STREAMING  Source Library (libmmipstreamaal.so) Load Failed  Error : %s ",::dlerror());
       return NULL;
   }

   SourceFactory StreamingSourcePtr = NULL;

   StreamingSourcePtr = (SourceFactory) dlsym(pStreamingSourceLib, DASH_HTTP_LIVE_CREATE_SOURCE);

   if (StreamingSourcePtr == NULL) {
       DP_MSG_ERROR("@@@@:: CreateDashHttpLiveSource symbol not found in libmmipstreamaal.so, return NULL ");
       return NULL;
   }

    /*Get the Streaming (DASH) Source object, which will be used to communicate with Source (DASH) */
    sp<DashPlayer::Source> StreamingSource = StreamingSourcePtr(uri, headers, uidValid, uid);

    if(StreamingSource==NULL) {
        DP_MSG_ERROR("@@@@:: StreamingSource failed to instantiate Source ");
        return NULL;
    }

    return StreamingSource;
}

status_t DashPlayer::prepareAsync() // only for DASH
{
    sp<AMessage> msg = new AMessage(kWhatPrepareAsync, this);
    if (msg == NULL)
    {
        DP_MSG_ERROR("Out of memory, AMessage is null for kWhatPrepareAsync\n");
        return NO_MEMORY;
    }
    msg->post();
    return -EWOULDBLOCK;

    return OK;
}

status_t DashPlayer::getParameter(int key, Parcel *reply)
{
    void * data_8;
    void * data_16;
    size_t data_8_Size;
    size_t data_16_Size;

    status_t err = OK;

    if (mSource == NULL)
    {
      DP_MSG_ERROR("Source is NULL in getParameter\n");
      return ((status_t)UNKNOWN_ERROR);
    }
    if (key == KEY_DASH_REPOSITION_RANGE)
    {
       uint64_t nMin = 0, nMax = 0, nMaxDepth = 0;
       err = mSource->getRepositionRange(&nMin, &nMax, &nMaxDepth);
       if(err == OK || err  == ERROR_END_OF_STREAM)
       {
         reply->setDataPosition(0);
         reply->writeInt64(nMin);
         reply->writeInt64(nMax);
         reply->writeInt64(nMaxDepth);
         err = OK;
         DP_MSG_LOW("DashPlayer::getParameter KEY_DASH_REPOSITION_RANGE %lld, %lld", nMin, nMax);
       }
       else
       {
         DP_MSG_ERROR("DashPlayer::getParameter KEY_DASH_REPOSITION_RANGE err in NOT OK");
       }
    }
    else if(key == INVOKE_ID_GET_TRACK_INFO)
    {
      size_t numInbandTracks = (mSource != NULL) ? mSource->getTrackCount() : 0;
      DP_MSG_HIGH("DashPlayer::getParameter #InbandTracks %d ", numInbandTracks);
      // total track count
      reply->writeInt32(numInbandTracks);
      // write inband tracks
      for (size_t i = 0; i < numInbandTracks; ++i) {
          writeTrackInfo(reply, mSource->getTrackInfo(i));
      }
    }
    else
    {
    err = mSource->getParameter(key, &data_8, &data_8_Size);
    if (key == KEY_DASH_QOE_PERIODIC_EVENT)
    {
      if (err == OK)
      {
        if(data_8)
        {
          sp<AMessage> dataQOE;
          dataQOE = (AMessage*)(data_8);
          int32_t bandwidth = 0;
          int32_t ipaddSize = 0;
          int32_t videoSize = 0;
          int64_t timeofday = 0;
          AString ipAdd;
          AString videoUrl;

          dataQOE->findInt64("timeofday",&timeofday);
          dataQOE->findInt32("bandwidth",&bandwidth);
          dataQOE->findInt32("sizeipadd",&ipaddSize);
          dataQOE->findString("ipaddress",&ipAdd);
          dataQOE->findInt32("sizevideo",&videoSize);
          dataQOE->findString("videourl",&videoUrl);

          reply->setDataPosition(0);
          reply->writeInt32(bandwidth);
          reply->writeInt64(timeofday);
          reply->writeInt32(ipaddSize);
          reply->writeInt32(ipaddSize);
          reply->write((const uint8_t *)ipAdd.c_str(), ipaddSize);
          reply->writeInt32(videoSize);
          reply->writeInt32(videoSize);
          videoUrl.append('\0');
          reply->write((const uint8_t *)videoUrl.c_str(), videoSize+1);
        }else
        {
          DP_MSG_ERROR("DashPlayerStats::getParameter : data_8 is null");
        }
      }
    }
    else
    {
    if (err != OK)
    {
      DP_MSG_ERROR("source getParameter returned error: %d\n",err);
      return err;
    }

    data_16_Size = data_8_Size * sizeof(char16_t);
    data_16 = malloc(data_16_Size);
    if (data_16 == NULL)
    {
      DP_MSG_ERROR("Out of memory in getParameter\n");
      return NO_MEMORY;
    }

    utf8_to_utf16_no_null_terminator((uint8_t *)data_8, data_8_Size, (char16_t *) data_16);
    err = reply->writeString16((char16_t *)data_16, data_8_Size);
    free(data_16);
    }
    }
    return err;
}

void DashPlayer::writeTrackInfo(
  Parcel* reply, const sp<AMessage> format) const
{
  int32_t trackType;
  AString lang;
  AString mime;
  CHECK(format->findInt32("type", &trackType));
  CHECK(format->findString("language", &lang));
  CHECK(format->findString("mime", &mime));
  reply->writeInt32(2);
  reply->writeInt32(trackType);
  reply->writeString16(String16(mime.c_str()));
  reply->writeString16(String16(lang.c_str()));
}



status_t DashPlayer::setParameter(int key, const Parcel &request)
{
    status_t err = (status_t)UNKNOWN_ERROR;;
    if (KEY_DASH_ADAPTION_PROPERTIES == key ||
        KEY_DASH_SET_ADAPTION_PROPERTIES == key)
    {
        size_t len = 0;
        const char16_t* str = request.readString16Inplace(&len);
        void * data = malloc(len + 1);
        if (data == NULL)
        {
            DP_MSG_ERROR("Out of memory in setParameter\n");
            return NO_MEMORY;
        }

        utf16_to_utf8(str, len, (char*) data);
        if (mSource != NULL)
        {
          err = mSource->setParameter(key, data, len);
        }
        free(data);
    }else if(key == KEY_DASH_QOE_EVENT)
    {
      int value  = request.readInt32();
      if (mSource != NULL)
      {
        err = mSource->setParameter(key, &value, sizeof(value));
      }
    }
    return err;
}

void DashPlayer::postIsPrepareDone()
{
    sp<AMessage> msg = new AMessage(kWhatIsPrepareDone, this);
    if (msg == NULL)
    {
        DP_MSG_ERROR("Out of memory, AMessage is null for kWhatIsPrepareDone\n");
        return;
    }
    msg->post();
}
void DashPlayer::sendTextPacket(sp<ABuffer> accessUnit,status_t err, TimedTextType eTimedTextType)
{
    if(!mQCTimedTextListenerPresent)
    {
      return;
    }

    Parcel parcel;
    int mFrameType = TIMED_TEXT_FLAG_FRAME;

    //Local setting
    parcel.writeInt32(KEY_LOCAL_SETTING);

    parcel.writeInt32(KEY_TEXT_FORMAT);
    // UPDATE TIMEDTEXT SAMPLE TYPE
    //Currently dash only support SMPTE-TT and CEA formats. No support for other timedtext types (like WebVTT, SRT)
    if(eTimedTextType == TIMED_TEXT_SMPTE)
    {
      parcel.writeString16((String16)"smptett");
    }
    else if(eTimedTextType == TIMED_TEXT_CEA)
    {
      parcel.writeString16((String16)"cea");
    }
    else
    {
      parcel.writeString16((String16)"unknown");
    }

    // UPDATE TIMEDTEXT SAMPLE FLAGS
    parcel.writeInt32(KEY_TEXT_FLAG_TYPE);
    if (err == ERROR_END_OF_STREAM ||
        err == (status_t)UNKNOWN_ERROR)
    {
       parcel.writeInt32(TIMED_TEXT_FLAG_EOS);
       // write size of sample
       DP_MSG_ERROR("sendTextPacket Error End Of Stream EOS");
       mFrameType = TIMED_TEXT_FLAG_EOS;
       notifyListener(MEDIA_TIMED_TEXT, 0, mFrameType, &parcel);
       return;
    }

    int32_t tCodecConfig = 0;
    accessUnit->meta()->findInt32("conf", &tCodecConfig);
    if(tCodecConfig)
    {
       DP_MSG_HIGH("Timed text codec config frame");
       parcel.writeInt32(TIMED_TEXT_FLAG_CODEC_CONFIG);
       mFrameType = TIMED_TEXT_FLAG_CODEC_CONFIG;
    }
    else
    {
       parcel.writeInt32(TIMED_TEXT_FLAG_FRAME);
       mFrameType = TIMED_TEXT_FLAG_FRAME;
    }

    int32_t bDisc = 0;
    accessUnit->meta()->findInt32("disc", &bDisc);
      if(bDisc == 1)
      {
        DP_MSG_HIGH("sendTextPacket signal discontinuity");
        parcel.writeInt32(KEY_TEXT_DISCONTINUITY);
      }

    // UPDATE TIMEDTEXT SAMPLE TEXT DATA
    parcel.writeInt32(KEY_STRUCT_TEXT);
    // write size of sample
    parcel.writeInt32((int32_t)accessUnit->size());
    parcel.writeInt32((int32_t)accessUnit->size());
    // write sample payload
    parcel.write((const uint8_t *)accessUnit->data(), accessUnit->size());

    // UPDATE TIMEDTEXT SAMPLE PROPERTIES
    int64_t mediaTimeUs = 0;
    CHECK(accessUnit->meta()->findInt64("timeUs", &mediaTimeUs));
    parcel.writeInt32(KEY_START_TIME);
    parcel.writeInt32((int32_t)(mediaTimeUs / 1000));  // convert micro sec to milli sec

    DP_MSG_HIGH("sendTextPacket Text Track Timestamp (%0.2f) sec",(double)mediaTimeUs / 1E6);

    int32_t height = 0;
    if (accessUnit->meta()->findInt32("height", &height)) {
        DP_MSG_LOW("sendTextPacket Height (%d)",height);
        parcel.writeInt32(KEY_HEIGHT);
        parcel.writeInt32(height);
    }

    // width
    int32_t width = 0;
    if (accessUnit->meta()->findInt32("width", &width)) {
        DP_MSG_LOW("sendTextPacket width (%d)",width);
        parcel.writeInt32(KEY_WIDTH);
        parcel.writeInt32(width);
    }

    // Duration
    int32_t duration = 0;
    if (accessUnit->meta()->findInt32("duration", &duration)) {
        DP_MSG_LOW("sendTextPacket duration (%d)",duration);
        parcel.writeInt32(KEY_DURATION);
        parcel.writeInt32(duration);
    }

    // start offset
    int32_t startOffset = 0;
    if (accessUnit->meta()->findInt32("startoffset", &startOffset)) {
        DP_MSG_LOW("sendTextPacket startOffset (%d)",startOffset);
        parcel.writeInt32(KEY_START_OFFSET);
        parcel.writeInt32(startOffset);
    }

    // SubInfoSize
    int32_t subInfoSize = 0;
    if (accessUnit->meta()->findInt32("subSz", &subInfoSize)) {
        DP_MSG_LOW("sendTextPacket subInfoSize (%d)",subInfoSize);
    }

    // SubInfo
    AString subInfo;
    if (accessUnit->meta()->findString("subSi", &subInfo)) {
        parcel.writeInt32(KEY_SUB_ATOM);
        parcel.writeInt32(subInfoSize);
        parcel.writeInt32(subInfoSize);
        parcel.write((const uint8_t *)subInfo.c_str(), subInfoSize);
    }

    notifyListener(MEDIA_TIMED_TEXT, 0, mFrameType, &parcel);
}

void DashPlayer::getTrackName(int track, char* name)
{
    if( track == kAudio)
    {
      memset(name,0x00,6);
      strlcpy(name, "audio",6);
    }
    else if( track == kVideo)
    {
      memset(name,0x00,6);
      strlcpy(name, "video",6);
    }
    else if( track == kText)
    {
      memset(name,0x00,6);
      strlcpy(name, "text",5);
    }
    else if (track == kTrackAll)
    {
      memset(name,0x00,6);
      strlcpy(name, "all",4);
    }
}

void DashPlayer::prepareSource()
{
    mSourceNotify = new AMessage(kWhatSourceNotify ,this);
    mQOENotify = new AMessage(kWhatQOE,this);
    if (mSource != NULL)
    {
      mSource->setupSourceData(mSourceNotify,kTrackAll);
      mSource->setupSourceData(mQOENotify,-1);
    }
}

status_t DashPlayer::dump(int fd, const Vector<String16> &/*args*/)
{
    if(mStats != NULL) {
      mStats->setFileDescAndOutputStream(fd);
    }

    return OK;
}

void DashPlayer::setQCTimedTextListener(const bool val)
{
  mQCTimedTextListenerPresent = val;
  DP_MSG_HIGH("QCTimedtextlistener turned %s", mQCTimedTextListenerPresent ? "ON" : "OFF");
}

void DashPlayer::processDeferredActions() {
    while (!mDeferredActions.empty()) {
        // We won't execute any deferred actions until we're no longer in
        // an intermediate state, i.e. one more more decoders are currently
        // flushing or shutting down.

        if (mRenderer != NULL) {
            // There's an edge case where the renderer owns all output
            // buffers and is paused, therefore the decoder will not read
            // more input data and will never encounter the matching
            // discontinuity. To avoid this, we resume the renderer.

            if (mFlushingAudio == AWAITING_DISCONTINUITY
                    || mFlushingVideo == AWAITING_DISCONTINUITY) {
                mRenderer->resume();
            }
        }

        if ((mFlushingAudio != NONE && mFlushingAudio != SHUT_DOWN)
              || (mFlushingVideo != NONE && mFlushingVideo != SHUT_DOWN)) {
            // We're currently flushing, postpone the reset until that's
            // completed.

            DP_MSG_ERROR("postponing action mFlushingAudio=%d, mFlushingVideo=%d",
                  mFlushingAudio, mFlushingVideo);

            break;
        }

        sp<Action> action = *mDeferredActions.begin();
        mDeferredActions.erase(mDeferredActions.begin());

        action->execute(this);
    }
}

void DashPlayer::performSetSurface(const sp<Surface> &wrapper) {
    DP_MSG_HIGH("performSetSurface");

    mNativeWindow = wrapper;

    // XXX - ignore error from setVideoScalingMode for now
    //setVideoScalingMode(mVideoScalingMode);

    if (mDriver != NULL) {
        sp<DashPlayerDriver> driver = mDriver.promote();
        if (driver != NULL) {
            driver->notifySetSurfaceComplete();
        }
    }

    isSetSurfaceTexturePending = false;
}

void DashPlayer::performScanSources() {
    DP_MSG_ERROR("performScanSources");

    //if (!mStarted) {
      //  return;
    //}

    if (mAudioDecoder == NULL || mVideoDecoder == NULL) {
        postScanSources();
    }
}

void DashPlayer::performDecoderShutdown(bool audio, bool video) {
    DP_MSG_ERROR("performDecoderShutdown audio=%d, video=%d", audio, video);

    if ((!audio || mAudioDecoder == NULL)
            && (!video || mVideoDecoder == NULL)) {
        return;
    }

    //mTimeDiscontinuityPending = true;

    if (mFlushingAudio == NONE && (!audio || mAudioDecoder == NULL)) {
        mFlushingAudio = FLUSHED;
    }

    if (mFlushingVideo == NONE && (!video || mVideoDecoder == NULL)) {
        mFlushingVideo = FLUSHED;
    }

    if (audio && mAudioDecoder != NULL) {
        flushDecoder(true /* audio */, true /* needShutdown */);
    }

    if (video && mVideoDecoder != NULL) {
        flushDecoder(false /* audio */, true /* needShutdown */);
    }
}


/** @brief: Pushes blank frame to native window
 *
 *  @return: NO_ERROR if frame pushed successfully to native window
 *
 */
status_t DashPlayer::PushBlankBuffersToNativeWindow(sp<ANativeWindow> nativeWindow) {
    status_t err = NO_ERROR;
    ANativeWindowBuffer* anb = NULL;
    int numBufs = 0;
    int minUndequeuedBufs = 0;

    // We need to reconnect to the ANativeWindow as a CPU client to ensure that
    // no frames get dropped by SurfaceFlinger assuming that these are video
    // frames.
    err = native_window_api_disconnect(nativeWindow.get(),
            NATIVE_WINDOW_API_MEDIA);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: api_disconnect failed: %s (%d)",
                strerror(-err), -err);
        return err;
    }

    err = native_window_api_connect(nativeWindow.get(),
            NATIVE_WINDOW_API_CPU);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: api_connect failed: %s (%d)",
                strerror(-err), -err);
        return err;
    }

    err = native_window_set_buffers_dimensions(nativeWindow.get(),
            1, 1);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_buffers_dimensions failed: %s (%d)",
                strerror(-err), -err);
        goto error;
    }

    err = native_window_set_buffers_format(nativeWindow.get(),
          HAL_PIXEL_FORMAT_RGBX_8888);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_buffers_format failed: %s (%d)",
               strerror(-err), -err);
         goto error;
    }

    err = native_window_set_scaling_mode(nativeWindow.get(),
                NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank_frames: set_scaling_mode failed: %s (%d)",
              strerror(-err), -err);
        goto error;
    }

    err = native_window_set_usage(nativeWindow.get(),
            GRALLOC_USAGE_SW_WRITE_OFTEN);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_usage failed: %s (%d)",
                strerror(-err), -err);
        goto error;
    }

    err = nativeWindow->query(nativeWindow.get(),
            NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &minUndequeuedBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: MIN_UNDEQUEUED_BUFFERS query "
                "failed: %s (%d)", strerror(-err), -err);
        goto error;
    }

    numBufs = minUndequeuedBufs + 1;
    err = native_window_set_buffer_count(nativeWindow.get(), numBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_buffer_count failed: %s (%d)",
                strerror(-err), -err);
        goto error;
    }

    // We  push numBufs + 1 buffers to ensure that we've drawn into the same
    // buffer twice.  This should guarantee that the buffer has been displayed
    // on the screen and then been replaced, so an previous video frames are
    // guaranteed NOT to be currently displayed.
    for (int i = 0; i < numBufs + 1; i++) {
        int fenceFd = -1;
        err = native_window_dequeue_buffer_and_wait(nativeWindow.get(), &anb);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: dequeueBuffer failed: %s (%d)",
                    strerror(-err), -err);
            goto error;
        }

        sp<GraphicBuffer> buf(new GraphicBuffer(anb, false));

        // Fill the buffer with the a 1x1 checkerboard pattern ;)
        uint32_t* img = NULL;
        err = buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: lock failed: %s (%d)",
                    strerror(-err), -err);
            goto error;
        }

        *img = 0;

        err = buf->unlock();
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: unlock failed: %s (%d)",
                    strerror(-err), -err);
            goto error;
        }

        err = nativeWindow->queueBuffer(nativeWindow.get(),
                buf->getNativeBuffer(), -1);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: queueBuffer failed: %s (%d)",
                    strerror(-err), -err);
            goto error;
        }

        anb = NULL;
    }

error:

    if (err != NO_ERROR) {
        // Clean up after an error.
        if (anb != NULL) {
            nativeWindow->cancelBuffer(nativeWindow.get(), anb, -1);
        }

        native_window_api_disconnect(nativeWindow.get(),
                NATIVE_WINDOW_API_CPU);
        native_window_api_connect(nativeWindow.get(),
                NATIVE_WINDOW_API_MEDIA);

        return err;
    } else {
        // Clean up after success.
        err = native_window_api_disconnect(nativeWindow.get(),
                NATIVE_WINDOW_API_CPU);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: api_disconnect failed: %s (%d)",
                    strerror(-err), -err);
            return err;
        }

        err = native_window_api_connect(nativeWindow.get(),
                NATIVE_WINDOW_API_MEDIA);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: api_connect failed: %s (%d)",
                    strerror(-err), -err);
            return err;
        }

        return NO_ERROR;
    }
}

}  // namespace android
