/*
 *Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *Not a Contribution.
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
#ifdef QCOM_WFD_SINK
#include "WFDRenderer.h"
#endif //QCOM_WFD_SINK
//#include "HTTPLiveSource.h"
#include "DashPlayerDecoder.h"
#include "DashPlayerDriver.h"
#include "DashPlayerRenderer.h"
#include "DashPlayerSource.h"
#include "DashCodec.h"
//#include "RTSPSource.h"
//#include "StreamingSource.h"
//#include "GenericSource.h"

#include "ATSParser.h"
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <TextDescriptions.h>

#ifdef ANDROID_JB_MR2
#include <gui/IGraphicBufferProducer.h>
#else
#include <gui/ISurfaceTexture.h>
#endif

#include <cutils/properties.h>
#include "avc_utils.h"

namespace android {

////////////////////////////////////////////////////////////////////////////////

DashPlayer::DashPlayer()
    : mUIDValid(false),
      mVideoIsAVC(false),
      mAudioEOS(false),
      mVideoEOS(false),
      mScanSourcesPending(false),
      mScanSourcesGeneration(0),
      mTimeDiscontinuityPending(false),
      mFlushingAudio(NONE),
      mFlushingVideo(NONE),
      mResetInProgress(false),
      mResetPostponed(false),
      mSetVideoSize(true),
      mSkipRenderingAudioUntilMediaTimeUs(-1ll),
      mSkipRenderingVideoUntilMediaTimeUs(-1ll),
      mVideoLateByUs(0ll),
      mNumFramesTotal(0ll),
      mNumFramesDropped(0ll),
      mPauseIndication(false),
      mSourceType(kDefaultSource),
      mRenderer(NULL),
      mIsSecureInputBuffers(false),
      mStats(NULL),
      mBufferingNotification(false),
      mSRid(0) {
      mTrackName = new char[6];
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

void DashPlayer::setDataSource(const sp<IStreamSource> &source) {
    ALOGE("DashPlayer::setDataSource not Implemented...");
}

status_t DashPlayer::setDataSource(
        const char *url, const KeyedVector<String8, String8> *headers) {
    sp<AMessage> msg = new AMessage(kWhatSetDataSource, id());

    sp<Source> source;
    if (!strncasecmp(url, "http://", 7) &&
          (strlen(url) >= 4 && !strcasecmp(".mpd", &url[strlen(url) - 4]))) {
           /* Load the DASH HTTP Live source librery here */
           ALOGV("DashPlayer setDataSource url sting %s",url);
           source = LoadCreateSource(url, headers, mUIDValid, mUID,kHttpDashSource);
           if (source != NULL) {
              mSourceType = kHttpDashSource;
              msg->setObject("source", source);
              msg->post();
              return OK;
           } else {
             ALOGE("Error creating DASH source");
             return UNKNOWN_ERROR;
           }
    }
    else
    {
      ALOGE("Unsupported URL");
      return UNKNOWN_ERROR;
    }
}

void DashPlayer::setDataSource(int fd, int64_t offset, int64_t length) {
   ALOGE("DashPlayer::setDataSource not Implemented...");
}

#ifdef ANDROID_JB_MR2
void DashPlayer::setVideoSurfaceTexture(const sp<IGraphicBufferProducer> &bufferProducer) {
    sp<AMessage> msg = new AMessage(kWhatSetVideoNativeWindow, id());
    sp<Surface> surface(bufferProducer != NULL ?
                new Surface(bufferProducer) : NULL);
    msg->setObject("native-window", new NativeWindowWrapper(surface));
    msg->post();
}
#else
void DashPlayer::setVideoSurfaceTexture(const sp<ISurfaceTexture> &surfaceTexture) {
    mSetVideoSize = true;
    sp<AMessage> msg = new AMessage(kWhatSetVideoNativeWindow, id());
    sp<SurfaceTextureClient> surfaceTextureClient(surfaceTexture != NULL ?
                new SurfaceTextureClient(surfaceTexture) : NULL);
    msg->setObject("native-window", new NativeWindowWrapper(surfaceTextureClient));
    msg->post();
}
#endif

void DashPlayer::setAudioSink(const sp<MediaPlayerBase::AudioSink> &sink) {
    sp<AMessage> msg = new AMessage(kWhatSetAudioSink, id());
    msg->setObject("sink", sink);
    msg->post();
}

void DashPlayer::start() {
    (new AMessage(kWhatStart, id()))->post();
}

void DashPlayer::pause() {
    (new AMessage(kWhatPause, id()))->post();
}

void DashPlayer::resume() {
    (new AMessage(kWhatResume, id()))->post();
}

void DashPlayer::resetAsync() {
    (new AMessage(kWhatReset, id()))->post();
}

void DashPlayer::seekToAsync(int64_t seekTimeUs) {
    sp<AMessage> msg = new AMessage(kWhatSeek, id());
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
            ALOGV("kWhatSetDataSource");

            CHECK(mSource == NULL);

            sp<RefBase> obj;
            CHECK(msg->findObject("source", &obj));

            mSource = static_cast<Source *>(obj.get());
            if (mSourceType == kHttpDashSource) {
               prepareSource();
            }
            break;
        }

        case kWhatSetVideoNativeWindow:
        {
            ALOGV("kWhatSetVideoNativeWindow");

            sp<RefBase> obj;
            CHECK(msg->findObject("native-window", &obj));

            mNativeWindow = static_cast<NativeWindowWrapper *>(obj.get());
            break;
        }

        case kWhatSetAudioSink:
        {
            ALOGV("kWhatSetAudioSink");

            sp<RefBase> obj;
            CHECK(msg->findObject("sink", &obj));

            mAudioSink = static_cast<MediaPlayerBase::AudioSink *>(obj.get());
            break;
        }

        case kWhatStart:
        {
            ALOGV("kWhatStart");

            mVideoIsAVC = false;
            mAudioEOS = false;
            mVideoEOS = false;
            mSkipRenderingAudioUntilMediaTimeUs = -1;
            mSkipRenderingVideoUntilMediaTimeUs = -1;
            mVideoLateByUs = 0;
            mNumFramesTotal = 0;
            mNumFramesDropped = 0;
            if (mSource != NULL)
            {
              mSource->start();
            }

            // for qualcomm statistics profiling
            mStats = new DashPlayerStats();

#ifdef QCOM_WFD_SINK
            if (mSourceType == kWfdSource) {
                ALOGV("creating WFDRenderer in NU player");
                mRenderer = new WFDRenderer(
                        mAudioSink,
                        new AMessage(kWhatRendererNotify, id()));
            }
            else {
#endif /* QCOM_WFD_SINK */
                mRenderer = new Renderer(
                        mAudioSink,
                        new AMessage(kWhatRendererNotify, id()));
#ifdef QCOM_WFD_SINK
            }
#endif /* QCOM_WFD_SINK */
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
                if (mSourceType == kHttpDashSource) {
                    ALOGV("scanning sources haveAudio=%d, haveVideo=%d haveText=%d",
                         mAudioDecoder != NULL, mVideoDecoder != NULL, mTextDecoder!= NULL);
                } else {
                    ALOGV("scanning sources haveAudio=%d, haveVideo=%d",
                         mAudioDecoder != NULL, mVideoDecoder != NULL);
                }

                if(mNativeWindow != NULL) {
                    instantiateDecoder(kVideo, &mVideoDecoder);
                }

                if (mAudioSink != NULL) {
                    instantiateDecoder(kAudio, &mAudioDecoder);
                }
                if (mSourceType == kHttpDashSource) {
                    instantiateDecoder(kText, &mTextDecoder);
                }

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
                if (mSourceType == kHttpDashSource) {
                    if ((mAudioDecoder == NULL && mAudioSink != NULL)     ||
                        (mVideoDecoder == NULL && mNativeWindow != NULL)  ||
                        (mTextDecoder == NULL)) {
                          msg->post(100000ll);
                          mScanSourcesPending = true;
                    }
                } else {
                    if ((mAudioDecoder == NULL && mAudioSink != NULL) ||
                        (mVideoDecoder == NULL && mNativeWindow != NULL)) {
                           msg->post(100000ll);
                           mScanSourcesPending = true;
                    }
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
            int track;
            if (msg->what() == kWhatAudioNotify)
                track = kAudio;
            else if (msg->what() == kWhatVideoNotify)
                track = kVideo;
            else if (msg->what() == kWhatTextNotify)
                track = kText;

            getTrackName(track,mTrackName);

            sp<AMessage> codecRequest;
            CHECK(msg->findMessage("codec-request", &codecRequest));

            int32_t what;
            CHECK(codecRequest->findInt32("what", &what));

            if (what == DashCodec::kWhatFillThisBuffer) {
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++ (%s) kWhatFillThisBuffer",mTrackName);
                if ( (track == kText) && (mTextDecoder == NULL)) {
                    break; // no need to proceed further
                }

                //if Player is in pause state, for WFD use case ,store the fill Buffer events and return back
                if((mSourceType == kWfdSource) && (mPauseIndication)) {
                    QueueEntry entry;
                    entry.mMessageToBeConsumed = msg;
                    mDecoderMessageQueue.push_back(entry);
                    break;
                }

                status_t err = feedDecoderInputData(
                        track, codecRequest);

                if (err == -EWOULDBLOCK) {
                    status_t nRet = mSource->feedMoreTSData();
                    if (nRet == OK) {
                           msg->post(10000ll);
                    }
                    else if(nRet == (status_t)UNKNOWN_ERROR ||
                            nRet == (status_t)ERROR_DRM_CANNOT_HANDLE) {
                      // reply back to dashcodec if there is an error
                      ALOGE("FeedMoreTSData error on track %d ",track);
                      if (track == kText) {
                        sendTextPacket(NULL, (status_t)UNKNOWN_ERROR);
                      } else {
                        sp<AMessage> reply;
                        CHECK(codecRequest->findMessage("reply", &reply));
                        reply->setInt32("err", (status_t)UNKNOWN_ERROR);
                        reply->post();
                      }
                    }
                }

            } else if (what == DashCodec::kWhatEOS) {
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatEOS");
                int32_t err;
                CHECK(codecRequest->findInt32("err", &err));

                if (err == ERROR_END_OF_STREAM) {
                    ALOGW("got %s decoder EOS", mTrackName);
                } else {
                    ALOGE("got %s decoder EOS w/ error %d",
                         mTrackName,
                         err);
                }

                if(mRenderer != NULL)
                {
                  if((track == kAudio && !IsFlushingState(mFlushingAudio)) || (track == kVideo && !IsFlushingState(mFlushingVideo))) {
                    mRenderer->queueEOS(track, err);
                  }
                  else{
                    ALOGE("FlushingState for %s. Decoder EOS not queued to renderer", mTrackName);
                  }
                }
            } else if (what == DashCodec::kWhatFlushCompleted) {
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatFlushCompleted");

                Mutex::Autolock autoLock(mLock);
                bool needShutdown;

                if (track == kAudio) {
                    CHECK(IsFlushingState(mFlushingAudio, &needShutdown));
                    mFlushingAudio = FLUSHED;
                } else if (track == kVideo){
                    CHECK(IsFlushingState(mFlushingVideo, &needShutdown));
                    mFlushingVideo = FLUSHED;

                    mVideoLateByUs = 0;
                }

                ALOGV("decoder %s flush completed", mTrackName);

                if (needShutdown) {
                    ALOGV("initiating %s decoder shutdown",
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
            } else if (what == DashCodec::kWhatOutputFormatChanged) {
                if (track == kAudio) {
                    ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatOutputFormatChanged:: audio");
                    int32_t numChannels;
                    CHECK(codecRequest->findInt32("channel-count", &numChannels));

                    int32_t sampleRate;
                    CHECK(codecRequest->findInt32("sample-rate", &sampleRate));

                    ALOGW("Audio output format changed to %d Hz, %d channels",
                         sampleRate, numChannels);

                    mAudioSink->close();

                    audio_output_flags_t flags;
                    int64_t durationUs;
                    // FIXME: we should handle the case where the video decoder is created after
                    // we receive the format change indication. Current code will just make that
                    // we select deep buffer with video which should not be a problem as it should
                    // not prevent from keeping A/V sync.
                    if (mVideoDecoder == NULL &&
                            mSource->getDuration(&durationUs) == OK &&
                            durationUs > AUDIO_SINK_MIN_DEEP_BUFFER_DURATION_US) {
                        flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
                    } else {
                        flags = AUDIO_OUTPUT_FLAG_NONE;
                    }

                    int32_t channelMask;
                    if (!codecRequest->findInt32("channel-mask", &channelMask)) {
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
                    ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatOutputFormatChanged:: video");
                    // No need to notify JAVA layer the message of kWhatOutputFormatChanged which will cause a flicker while changing the resolution
#if 0
                        int32_t width, height;
                        CHECK(codecRequest->findInt32("width", &width));
                        CHECK(codecRequest->findInt32("height", &height));

                        int32_t cropLeft, cropTop, cropRight, cropBottom;
                        CHECK(codecRequest->findRect(
                                    "crop",
                                    &cropLeft, &cropTop, &cropRight, &cropBottom));

                        ALOGW("Video output format changed to %d x %d "
                             "(crop: %d x %d @ (%d, %d))",
                             width, height,
                             (cropRight - cropLeft + 1),
                             (cropBottom - cropTop + 1),
                             cropLeft, cropTop);

                        notifyListener(
                                MEDIA_SET_VIDEO_SIZE,
                                cropRight - cropLeft + 1,
                                cropBottom - cropTop + 1);
#endif
                }
            } else if (what == DashCodec::kWhatShutdownCompleted) {
                ALOGV("%s shutdown completed", mTrackName);
                if (track == kAudio) {
                    ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatShutdownCompleted:: audio");
                    if (mAudioDecoder != NULL) {
                        looper()->unregisterHandler(mAudioDecoder->id());
                    }
                    mAudioDecoder.clear();

                    CHECK_EQ((int)mFlushingAudio, (int)SHUTTING_DOWN_DECODER);
                    mFlushingAudio = SHUT_DOWN;
                } else if (track == kVideo) {
                    ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ kWhatShutdownCompleted:: Video");
                    if (mVideoDecoder != NULL) {
                        looper()->unregisterHandler(mVideoDecoder->id());
                    }
                    mVideoDecoder.clear();

                    CHECK_EQ((int)mFlushingVideo, (int)SHUTTING_DOWN_DECODER);
                    mFlushingVideo = SHUT_DOWN;
                }

                finishFlushIfPossible();
            } else if (what == DashCodec::kWhatError) {
                ALOGE("Received error from %s decoder, aborting playback.",
                       mTrackName);
                if(mRenderer != NULL)
                {
                  if((track == kAudio && !IsFlushingState(mFlushingAudio)) ||
                     (track == kVideo && !IsFlushingState(mFlushingVideo)))
                  {
                    ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ DashCodec::kWhatError:: %s",track == kAudio ? "audio" : "video");
                    mRenderer->queueEOS(track, UNKNOWN_ERROR);
                }
                  else{
                    ALOGE("EOS not queued for %s track", track);
                  }
                }
            } else if (what == DashCodec::kWhatDrainThisBuffer) {
                if(track == kAudio || track == kVideo) {
                   ALOGV("@@@@:: Dashplayer :: MESSAGE FROM DASHCODEC +++++++++++++++++++++++++++++++ DashCodec::kWhatRenderBuffer:: %s",track == kAudio ? "audio" : "video");
                        renderBuffer(track, codecRequest);
                    }
            } else {
                ALOGV("Unhandled codec notification %d.", what);
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
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatRendererNotify:: %s",audio ? "audio" : "video");
                if (audio) {
                    mAudioEOS = true;
                } else {
                    mVideoEOS = true;
                }

                if (finalResult == ERROR_END_OF_STREAM) {
                    ALOGW("reached %s EOS", audio ? "audio" : "video");
                } else {
                    ALOGE("%s track encountered an error (%d)",
                         audio ? "audio" : "video", finalResult);

                    notifyListener(
                            MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, finalResult);
                }

                if ((mAudioEOS || mAudioDecoder == NULL)
                        && (mVideoEOS || mVideoDecoder == NULL)) {
                     if ((mSourceType == kHttpDashSource) &&
                         (finalResult == ERROR_END_OF_STREAM)) {
                        notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                     } else if (mSourceType != kHttpDashSource) {
                       notifyListener(MEDIA_PLAYBACK_COMPLETE, 0, 0);
                     }
                }
            } else if (what == Renderer::kWhatPosition) {
                int64_t positionUs;
                CHECK(msg->findInt64("positionUs", &positionUs));

                CHECK(msg->findInt64("videoLateByUs", &mVideoLateByUs));
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatPosition:: position(%lld) VideoLateBy(%lld)",positionUs,mVideoLateByUs);

                if (mDriver != NULL) {
                    sp<DashPlayerDriver> driver = mDriver.promote();
                    if (driver != NULL) {
                        driver->notifyPosition(positionUs);
                        //Notify rendering position used for HLS
                        mSource->notifyRenderingPosition(positionUs);

                        driver->notifyFrameStats(
                                mNumFramesTotal, mNumFramesDropped);
                    }
                }
            } else if (what == Renderer::kWhatFlushComplete) {
                CHECK_EQ(what, (int32_t)Renderer::kWhatFlushComplete);

                int32_t audio;
                CHECK(msg->findInt32("audio", &audio));
                ALOGV("@@@@:: Dashplayer :: MESSAGE FROM RENDERER ***************** kWhatFlushComplete:: %s",audio ? "audio" : "video");

            }
            break;
        }

        case kWhatMoreDataQueued:
        {
            break;
        }

        case kWhatReset:
        {
            ALOGV("kWhatReset");
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

                ALOGV("postponing reset mFlushingAudio=%d, mFlushingVideo=%d",
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

            ALOGW("kWhatSeek seekTimeUs=%lld us (%.2f secs)",
                 seekTimeUs, seekTimeUs / 1E6);

            nRet = mSource->seekTo(seekTimeUs);

            if (mSourceType == kHttpLiveSource) {
                mSource->getNewSeekTime(&newSeekTime);
                ALOGV("newSeekTime %lld", newSeekTime);
            }
            else if (mSourceType == kHttpDashSource) {
                mTimeDiscontinuityPending = true;
                if (nRet == OK) { // if seek success then flush the audio,video decoder and renderer
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
                     ALOGV("Audio is not there, set it to shutdown");
                     mFlushingAudio = SHUT_DOWN;
                 }
                 if( mVideoDecoder == NULL ) {
                     ALOGV("Video is not there, set it to shutdown");
                     mFlushingVideo = SHUT_DOWN;
                 }
               }
               // get the new seeked position
               newSeekTime = seekTimeUs;
               ALOGV("newSeekTime %lld", newSeekTime);
            }
            if( (newSeekTime >= 0 ) && (mSourceType != kHttpDashSource)) {
               mTimeDiscontinuityPending = true;
               if( (mAudioDecoder != NULL) &&
                   (mFlushingAudio == NONE || mFlushingAudio == AWAITING_DISCONTINUITY) ) {
                  flushDecoder( true, true );
               }
               if( (mVideoDecoder != NULL) &&
                   (mFlushingVideo == NONE || mFlushingVideo == AWAITING_DISCONTINUITY) ) {
                  flushDecoder( false, true );
               }
               if( mAudioDecoder == NULL ) {
                   ALOGV("Audio is not there, set it to shutdown");
                   mFlushingAudio = SHUT_DOWN;

               }
               if( mVideoDecoder == NULL ) {
                   ALOGV("Video is not there, set it to shutdown");
                   mFlushingVideo = SHUT_DOWN;
               }
            }

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
#ifdef QCOM_WFD_SINK
            if (mSourceType == kWfdSource) {
                CHECK(mSource != NULL);
                mSource->pause();
            }
#endif //QCOM_WFD_SINK
                CHECK(mRenderer != NULL);
                mRenderer->pause();

            mPauseIndication = true;
            if (mSourceType == kHttpDashSource) {
                Mutex::Autolock autoLock(mLock);
                if (mSource != NULL)
                {
                   mSource->pause();
                }
            }
            break;
        }

        case kWhatResume:
        {
                CHECK(mRenderer != NULL);
                mRenderer->resume();

            mPauseIndication = false;

            if (mSourceType == kHttpDashSource) {
               Mutex::Autolock autoLock(mLock);
               if (mSource != NULL) {
                   mSource->resume();
               }
                if (mAudioDecoder == NULL || mVideoDecoder == NULL || mTextDecoder == NULL) {
                    mScanSourcesPending = false;
                    postScanSources();
                }
            }else if (mSourceType == kWfdSource) {
                CHECK(mSource != NULL);
                mSource->resume();
                int count = 0;

                //check if there are messages stored in the list, then repost them
                while(!mDecoderMessageQueue.empty()) {
                    (*mDecoderMessageQueue.begin()).mMessageToBeConsumed->post(); //self post
                    mDecoderMessageQueue.erase(mDecoderMessageQueue.begin());
                    ++count;
                }
                ALOGE("(%d) stored messages reposted ....",count);
            }else {
                if (mAudioDecoder == NULL || mVideoDecoder == NULL) {
                    mScanSourcesPending = false;
                    postScanSources();
                }
            }
            break;
        }

        case kWhatPrepareAsync:
            if (mSource == NULL)
            {
                ALOGE("Source is null in prepareAsync\n");
                break;
            }
            mSource->prepareAsync();
            postIsPrepareDone();
            break;

        case kWhatIsPrepareDone:
            if (mSource == NULL)
            {
                ALOGE("Source is null when checking for prepare done\n");
                break;
            }
            if (mSource->isPrepareDone()) {
                int64_t durationUs;
                if (mDriver != NULL && mSource->getDuration(&durationUs) == OK) {
                    sp<DashPlayerDriver> driver = mDriver.promote();
                    if (driver != NULL) {
                        driver->notifyDuration(durationUs);
                    }
                }
                notifyListener(MEDIA_PREPARED, 0, 0);
            } else {
                msg->post(100000ll);
            }
            break;
        case kWhatSourceNotify:
        {
            Mutex::Autolock autoLock(mLock);
            ALOGV("kWhatSourceNotify");

            if(mSource != NULL) {
                int64_t track;

                sp<AMessage> sourceRequest;
                ALOGD("kWhatSourceNotify - looking for source-request");

                // attempt to find message by different names
                bool msgFound = msg->findMessage("source-request", &sourceRequest);
                int32_t handled;
                if (!msgFound) {
                    ALOGD("kWhatSourceNotify source-request not found, trying using sourceRequestID");
                    char srName[] = "source-request00";
                    srName[strlen("source-request")] += mSRid/10;
                    srName[strlen("source-request")+sizeof(char)] += mSRid%10;
                    msgFound = msg->findMessage(srName, &sourceRequest);
                    if(msgFound)
                        mSRid = (mSRid+1)%SRMax;
                }

                if(msgFound) {
                    int32_t what;
                    CHECK(sourceRequest->findInt32("what", &what));
                    sourceRequest->findInt64("track", &track);
                    getTrackName((int)track,mTrackName);

                    if (what == kWhatBufferingStart) {
                      ALOGE("Source Notified Buffering Start for %s ",mTrackName);
                      if (mBufferingNotification == false) {
                          if (track == kVideo && mNativeWindow == NULL)
                          {
                               ALOGE("video decoder not instantiated, no buffering for video",
                                     mBufferingNotification);
                          }
                          else
                          {
                              mBufferingNotification = true;
                              notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_START, 0);
                          }
                      }
                      else {
                         ALOGE("Buffering Start Event Already Notified mBufferingNotification(%d)",
                               mBufferingNotification);
                      }
                    }
                    else if(what == kWhatBufferingEnd) {
                        if (mBufferingNotification) {
                          ALOGE("Source Notified Buffering End for %s ",mTrackName);
                                mBufferingNotification = false;
                          notifyListener(MEDIA_INFO, MEDIA_INFO_BUFFERING_END, 0);
                          if(mStats != NULL) {
                            mStats->notifyBufferingEvent();
                          }
                        }
                        else {
                          ALOGE("No need to notify Buffering end as mBufferingNotification is (%d) "
                                ,mBufferingNotification);
                        }
                    }
                }
            }
            else {
              ALOGE("kWhatSourceNotify - Source object does not exist anymore");
            }
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void DashPlayer::finishFlushIfPossible() {
    //If reset was postponed after one of the streams is flushed, complete it now
    if (mResetPostponed) {
        ALOGV("finishFlushIfPossible Handle reset postpone ");
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
        ALOGV("Dont finish flush, audio is in state %d ", mFlushingAudio);
        return;
    }

    if (mFlushingVideo != FLUSHED && mFlushingVideo != SHUT_DOWN) {
        ALOGV("Dont finish flush, video is in state %d ", mFlushingVideo);
        return;
    }

    ALOGV("both audio and video are flushed now.");

    if ((mRenderer != NULL) && (mTimeDiscontinuityPending)) {
        mRenderer->signalTimeDiscontinuity();
        mTimeDiscontinuityPending = false;
    }

    if (mAudioDecoder != NULL) {
        ALOGV("Resume Audio after flush");
        mAudioDecoder->signalResume();
    }

    if (mVideoDecoder != NULL) {
        ALOGV("Resume Video after flush");
        mVideoDecoder->signalResume();
    }

    mFlushingAudio = NONE;
    mFlushingVideo = NONE;

    if (mResetInProgress) {
        ALOGV("reset completed");

        mResetInProgress = false;
        finishReset();
    } else if (mResetPostponed) {
        (new AMessage(kWhatReset, id()))->post();
        mResetPostponed = false;
        ALOGV("Handle reset postpone");
    } else if (mAudioDecoder == NULL || mVideoDecoder == NULL) {
        ALOGV("Start scanning for sources after shutdown");
        if ( (mSourceType == kHttpDashSource) &&
             (mTextDecoder != NULL) )
        {
          if (mSource != NULL) {
           ALOGV("finishFlushIfPossible calling mSource->stop");
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
    }
    if(mRenderer != NULL) {
        mRenderer.clear();
    }

    if (mSource != NULL) {
        ALOGV("finishReset calling mSource->stop");
        mSource->stop();
        mSource.clear();
    }

    if ( (mSourceType == kHttpDashSource) && (mTextDecoder != NULL) && (mTextNotify != NULL))
    {
      sp<AMessage> codecRequest;
      mTextNotify->findMessage("codec-request", &codecRequest);
      codecRequest = NULL;
      mTextNotify = NULL;
      looper()->unregisterHandler(mTextDecoder->id());
      mTextDecoder.clear();
      ALOGE("Text Dummy Decoder Deleted");
    }
    if (mSourceNotify != NULL)
    {
       sp<AMessage> sourceRequest;
       mSourceNotify->findMessage("source-request", &sourceRequest);
       sourceRequest = NULL;
       for (int id = 0; id < SRMax; id++){
           char srName[] = "source-request00";
           srName[strlen("source-request")] += id/10;
           srName[strlen("source-request")+sizeof(char)] += id%10;
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

    sp<AMessage> msg = new AMessage(kWhatScanSources, id());
    msg->setInt32("generation", mScanSourcesGeneration);
    msg->post();

    mScanSourcesPending = true;
}

status_t DashPlayer::instantiateDecoder(int track, sp<Decoder> *decoder) {
    ALOGV("@@@@:: instantiateDecoder Called ");
    if (*decoder != NULL) {
        return OK;
    }

    sp<MetaData> meta = mSource->getFormat(track);

    if (meta == NULL) {
        return -EWOULDBLOCK;
    }

    if (track == kVideo) {
        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        mVideoIsAVC = !strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime);
        if(mStats != NULL) {
            mStats->setMime(mime);
        }

        //TO-DO:: Similarly set here for Decode order
        if (mVideoIsAVC &&
           ((mSourceType == kHttpLiveSource) || (mSourceType == kHttpDashSource) ||(mSourceType == kWfdSource))) {
            ALOGV("Set Enable smooth streaming in meta data ");
            meta->setInt32(kKeySmoothStreaming, 1);
        }

        int32_t isDRMSecBuf = 0;
        meta->findInt32(kKeyRequiresSecureBuffers, &isDRMSecBuf);
        if(isDRMSecBuf) {
            mIsSecureInputBuffers = true;
        }

        if (mSetVideoSize) {
            int32_t width = 0;
            meta->findInt32(kKeyWidth, &width);
            int32_t height = 0;
            meta->findInt32(kKeyHeight, &height);
            ALOGE("instantiate video decoder, send wxh = %dx%d",width,height);
            notifyListener(MEDIA_SET_VIDEO_SIZE, width, height);
            mSetVideoSize = false;
        }
    }

    sp<AMessage> notify;
    if (track == kAudio) {
        notify = new AMessage(kWhatAudioNotify ,id());
        ALOGV("Creating Audio Decoder ");
        *decoder = new Decoder(notify);
        ALOGV("@@@@:: setting Sink/Renderer pointer to decoder");
        (*decoder)->setSink(mAudioSink, mRenderer);
    } else if (track == kVideo) {
        notify = new AMessage(kWhatVideoNotify ,id());
        *decoder = new Decoder(notify, mNativeWindow);
        ALOGV("Creating Video Decoder ");
    } else if (track == kText) {
        mTextNotify = new AMessage(kWhatTextNotify ,id());
        *decoder = new Decoder(mTextNotify);
        sp<AMessage> codecRequest = new AMessage;
        codecRequest->setInt32("what", DashCodec::kWhatFillThisBuffer);
        mTextNotify->setMessage("codec-request", codecRequest);
        ALOGV("Creating Dummy Text Decoder ");
        if ((mSource != NULL) && (mSourceType == kHttpDashSource)) {
           mSource->setupSourceData(mTextNotify, track);
        }
    }

    looper()->registerHandler(*decoder);

    char value[PROPERTY_VALUE_MAX] = {0};
    if (mSourceType == kHttpLiveSource || mSourceType == kHttpDashSource){
        //Set flushing state to none
        Mutex::Autolock autoLock(mLock);
        if(track == kAudio) {
            mFlushingAudio = NONE;
        } else if (track == kVideo) {
            mFlushingVideo = NONE;

        }
    }

    if( track == kAudio || track == kVideo) {
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

        if (((track == kAudio) && IsFlushingState(mFlushingAudio))
            || ((track == kVideo) && IsFlushingState(mFlushingVideo))) {
            reply->setInt32("err", INFO_DISCONTINUITY);
            reply->post();
            return OK;
        }
    }

    getTrackName(track,mTrackName);

    sp<ABuffer> accessUnit;

    bool dropAccessUnit;
    do {

        status_t err = UNKNOWN_ERROR;

        if (mIsSecureInputBuffers && track == kVideo) {
            msg->findBuffer("buffer", &accessUnit);

            if (accessUnit == NULL) {
                ALOGE("Dashplayer NULL buffer in message");
                return err;
            } else {
                ALOGV("Dashplayer buffer in message %d %d",
                accessUnit->data(), accessUnit->capacity());
            }
        }

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

                ALOGW("%s discontinuity (formatChange=%d, time=%d)",
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
                            ALOGW("suppressing rendering of %s until %lld us",
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
               ALOGE("Text track has encountered error %d", err );
               sendTextPacket(NULL, err);
               return err;
            }
        }

        dropAccessUnit = false;
        if (track == kVideo) {
            ++mNumFramesTotal;

            if(mStats != NULL) {
                mStats->incrementTotalFrames();
            }

            if (mVideoLateByUs > 100000ll
                    && mVideoIsAVC
                    && !mIsSecureInputBuffers
                    && !IsAVCReferenceFrame(accessUnit)) {
                dropAccessUnit = true;
                ++mNumFramesDropped;
                if(mStats != NULL) {
                    mStats->incrementDroppedFrames();
                }
            }
        }
    } while (dropAccessUnit);

    // ALOGV("returned a valid buffer of %s data", mTrackName);

#if 0
    int64_t mediaTimeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &mediaTimeUs));
    ALOGV("feeding %s input buffer at media time %.2f secs",
         mTrackName,
         mediaTimeUs / 1E6);
#endif
    if (track == kVideo || track == kAudio) {
        reply->setBuffer("buffer", accessUnit);
        reply->post();
    } else if (mSourceType == kHttpDashSource && track == kText) {
        sendTextPacket(accessUnit,OK);
        if (mSource != NULL) {
          mSource->postNextTextSample(accessUnit,mTextNotify,track);
        }
    }
    return OK;
}

void DashPlayer::renderBuffer(bool audio, const sp<AMessage> &msg) {
    // ALOGV("renderBuffer %s", audio ? "audio" : "video");

    sp<AMessage> reply;
    CHECK(msg->findMessage("reply", &reply));

    Mutex::Autolock autoLock(mLock);
    if (IsFlushingState(audio ? mFlushingAudio : mFlushingVideo)) {
        // We're currently attempting to flush the decoder, in order
        // to complete this, the decoder wants all its buffers back,
        // so we don't want any output buffers it sent us (from before
        // we initiated the flush) to be stuck in the renderer's queue.

        ALOGV("we're still flushing the %s decoder, sending its output buffer"
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
            ALOGV("dropping %s buffer at time %lld as requested.",
                 audio ? "audio" : "video",
                 mediaTimeUs);

            reply->post();
            return;
        }

        skipUntilMediaTimeUs = -1;
    }

    if(mRenderer != NULL) {
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
        ALOGI("flushDecoder %s without decoder present",
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
                               bool uidValid, uid_t uid, NuSourceType srcTyp)
{
   const char* STREAMING_SOURCE_LIB = "libmmipstreamaal.so";
   const char* DASH_HTTP_LIVE_CREATE_SOURCE = "CreateDashHttpLiveSource";
   const char* WFD_CREATE_SOURCE = "CreateWFDSource";
   void* pStreamingSourceLib = NULL;

   typedef DashPlayer::Source* (*SourceFactory)(const char * uri, const KeyedVector<String8, String8> *headers, bool uidValid, uid_t uid);

   /* Open librery */
   pStreamingSourceLib = ::dlopen(STREAMING_SOURCE_LIB, RTLD_LAZY);

   if (pStreamingSourceLib == NULL) {
       ALOGV("@@@@:: STREAMING  Source Library (libmmipstreamaal.so) Load Failed  Error : %s ",::dlerror());
       return NULL;
   }

   SourceFactory StreamingSourcePtr;

   if(srcTyp == kHttpDashSource) {

       /* Get the entry level symbol which gets us the pointer to DASH HTTP Live Source object */
       StreamingSourcePtr = (SourceFactory) dlsym(pStreamingSourceLib, DASH_HTTP_LIVE_CREATE_SOURCE);
   } else if (srcTyp == kWfdSource){

       /* Get the entry level symbol which gets us the pointer to WFD Source object */
       StreamingSourcePtr = (SourceFactory) dlsym(pStreamingSourceLib, WFD_CREATE_SOURCE);

   }

   if (StreamingSourcePtr == NULL) {
       ALOGV("@@@@:: CreateDashHttpLiveSource symbol not found in libmmipstreamaal.so, return NULL ");
       return NULL;
   }

    /*Get the Streaming (DASH\WFD) Source object, which will be used to communicate with Source (DASH\WFD) */
    sp<DashPlayer::Source> StreamingSource = StreamingSourcePtr(uri, headers, uidValid, uid);

    if(StreamingSource==NULL) {
        ALOGV("@@@@:: StreamingSource failed to instantiate Source ");
        return NULL;
    }


    return StreamingSource;
}

status_t DashPlayer::prepareAsync() // only for DASH
{
    if (mSourceType == kHttpDashSource) {
        sp<AMessage> msg = new AMessage(kWhatPrepareAsync, id());
        if (msg == NULL)
        {
            ALOGE("Out of memory, AMessage is null for kWhatPrepareAsync\n");
            return NO_MEMORY;
        }
        msg->post();
        return -EWOULDBLOCK;
    }
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
      ALOGE("Source is NULL in getParameter\n");
      return UNKNOWN_ERROR;
    }
    err = mSource->getParameter(key, &data_8, &data_8_Size);
    if (err != OK)
    {
      ALOGE("source getParameter returned error: %d\n",err);
      return err;
    }

    data_16_Size = data_8_Size * sizeof(char16_t);
    data_16 = malloc(data_16_Size);
    if (data_16 == NULL)
    {
      ALOGE("Out of memory in getParameter\n");
      return NO_MEMORY;
    }

    utf8_to_utf16_no_null_terminator((uint8_t *)data_8, data_8_Size, (char16_t *) data_16);
    err = reply->writeString16((char16_t *)data_16, data_8_Size);
    free(data_16);
    return err;
}

status_t DashPlayer::setParameter(int key, const Parcel &request)
{
    status_t err = OK;
    if (key == 8004) {

        size_t len = 0;
        const char16_t* str = request.readString16Inplace(&len);
        void * data = malloc(len + 1);
        if (data == NULL)
        {
            ALOGE("Out of memory in setParameter\n");
            return NO_MEMORY;
        }

        utf16_to_utf8(str, len, (char*) data);
        err = mSource->setParameter(key, data, len);
        free(data);
    }
    return err;
}

void DashPlayer::postIsPrepareDone()
{
    sp<AMessage> msg = new AMessage(kWhatIsPrepareDone, id());
    if (msg == NULL)
    {
        ALOGE("Out of memory, AMessage is null for kWhatIsPrepareDone\n");
        return;
    }
    msg->post();
}
void DashPlayer::sendTextPacket(sp<ABuffer> accessUnit,status_t err)
{
    Parcel parcel;
    int mFrameType = TIMED_TEXT_FLAG_FRAME;

    //Local setting
    parcel.writeInt32(KEY_LOCAL_SETTING);
    if (err == ERROR_END_OF_STREAM ||
        err == (status_t)UNKNOWN_ERROR)
    {
       parcel.writeInt32(KEY_TEXT_EOS);
       // write size of sample
       ALOGE("Error End Of Stream EOS");
       mFrameType = TIMED_TEXT_FLAG_EOS;
       notifyListener(MEDIA_TIMED_TEXT, 0, mFrameType, &parcel);
       return;
    }
   // time stamp
    int64_t mediaTimeUs = 0;
    CHECK(accessUnit->meta()->findInt64("timeUs", &mediaTimeUs));
    parcel.writeInt32(KEY_START_TIME);
    parcel.writeInt32((int32_t)(mediaTimeUs / 1000));  // convert micro sec to milli sec

    ALOGE("sendTextPacket Text Track Timestamp (%0.2f) sec",mediaTimeUs / 1E6);

    // Text Sample
    parcel.writeInt32(KEY_STRUCT_TEXT);

    int32_t tCodecConfig;
    accessUnit->meta()->findInt32("conf", &tCodecConfig);
    if (tCodecConfig)
    {
       ALOGE("Timed text codec config frame");
       parcel.writeInt32(TIMED_TEXT_FLAG_CODEC_CONFIG_FRAME);
       mFrameType = TIMED_TEXT_FLAG_CODEC_CONFIG_FRAME;
    }
    else
    {
       parcel.writeInt32(TIMED_TEXT_FLAG_FRAME);
       mFrameType = TIMED_TEXT_FLAG_FRAME;
    }

    // write size of sample
    parcel.writeInt32(accessUnit->size());
    parcel.writeInt32(accessUnit->size());
    // write sample payload
    parcel.write((const uint8_t *)accessUnit->data(), accessUnit->size());

    int32_t height = 0;
    if (accessUnit->meta()->findInt32("height", &height)) {
        ALOGE("sendTextPacket Height (%d)",height);
        parcel.writeInt32(KEY_HEIGHT);
        parcel.writeInt32(height);
    }

    // width
    int32_t width = 0;
    if (accessUnit->meta()->findInt32("width", &width)) {
        ALOGE("sendTextPacket width (%d)",width);
        parcel.writeInt32(KEY_WIDTH);
        parcel.writeInt32(width);
    }

    // Duration
    int32_t duration = 0;
    if (accessUnit->meta()->findInt32("duration", &duration)) {
        ALOGE("sendTextPacket duration (%d)",duration);
        parcel.writeInt32(KEY_DURATION);
        parcel.writeInt32(duration);
    }

    // start offset
    int32_t startOffset = 0;
    if (accessUnit->meta()->findInt32("startoffset", &startOffset)) {
        ALOGE("sendTextPacket startOffset (%d)",startOffset);
        parcel.writeInt32(KEY_START_OFFSET);
        parcel.writeInt32(startOffset);
    }

    // SubInfoSize
    int32_t subInfoSize = 0;
    if (accessUnit->meta()->findInt32("subSz", &subInfoSize)) {
        ALOGE("sendTextPacket subInfoSize (%d)",subInfoSize);
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
    if (mSourceType = kHttpDashSource)
    {
       mSourceNotify = new AMessage(kWhatSourceNotify ,id());
       if (mSource != NULL)
       {
         mSource->setupSourceData(mSourceNotify,kTrackAll);
       }
    }
}

status_t DashPlayer::dump(int fd, const Vector<String16> &args)
{
    if(mStats != NULL) {
      mStats->setFileDescAndOutputStream(fd);
    }

    return OK;
}

}  // namespace android
