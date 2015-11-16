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
#define LOG_TAG "DashPlayerDecoder"

#include "DashPlayerDecoder.h"
#include <media/ICrypto.h>
#include "ESDS.h"
#include "QCMediaDefs.h"
#include "QCMetaData.h"
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <cutils/properties.h>
#include <utils/Log.h>

//Smooth streaming settings,
//Max resolution 1080p
#define MAX_WIDTH 1920
#define MAX_HEIGHT 1080

#define DPD_MSG_ERROR(...) ALOGE(__VA_ARGS__)
#define DPD_MSG_HIGH(...) if(mLogLevel >= 1){ALOGE(__VA_ARGS__);}
#define DPD_MSG_MEDIUM(...) if(mLogLevel >= 2){ALOGE(__VA_ARGS__);}
#define DPD_MSG_LOW(...) if(mLogLevel >= 3){ALOGE(__VA_ARGS__);}

namespace android {

DashPlayer::Decoder::Decoder(
        const sp<AMessage> &notify,
        const sp<Surface> &nativeWindow)
    : mNotify(notify),
      mNativeWindow(nativeWindow),
      mLogLevel(0),
      mBufferGeneration(0),
      mComponentName("decoder") {
    // Every decoder has its own looper because MediaCodec operations
    // are blocking, but DashPlayer needs asynchronous operations.
    mDecoderLooper = new ALooper;
    mDecoderLooper->setName("DashPlayerDecoder");
    mDecoderLooper->start(false, false, ANDROID_PRIORITY_AUDIO);

    mCodecLooper = new ALooper;
    mCodecLooper->setName("DashPlayerDecoder-MC");
    mCodecLooper->start(false, false, ANDROID_PRIORITY_AUDIO);

      char property_value[PROPERTY_VALUE_MAX] = {0};
      property_get("persist.dash.debug.level", property_value, NULL);

      if(*property_value) {
          mLogLevel = atoi(property_value);
      }
}

DashPlayer::Decoder::~Decoder() {
}

/** @brief: configure mediacodec
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::onConfigure(const sp<AMessage> &format) {
    CHECK(mCodec == NULL);

    ++mBufferGeneration;

    AString mime;
    CHECK(format->findString("mime", &mime));

    /*
    sp<Surface> surface = NULL;
    if (mNativeWindow != NULL) {
        surface = mNativeWindow->getSurfaceTextureClient();
    }
    */

    mComponentName = mime;
    mComponentName.append(" decoder");
    DPD_MSG_HIGH("[%s] onConfigure (surface=%p)", mComponentName.c_str(), mNativeWindow.get());

    mCodec = MediaCodec::CreateByType(mCodecLooper, mime.c_str(), false /* encoder */);
    if (mCodec == NULL) {
        DPD_MSG_ERROR("Failed to create %s decoder", mime.c_str());
        handleError(UNKNOWN_ERROR);
        return;
    }

    mCodec->getName(&mComponentName);

    status_t err;
    if (mNativeWindow != NULL) {
        // disconnect from surface as MediaCodec will reconnect
        err = native_window_api_disconnect(
                mNativeWindow.get(), NATIVE_WINDOW_API_MEDIA);
        // We treat this as a warning, as this is a preparatory step.
        // Codec will try to connect to the surface, which is where
        // any error signaling will occur.
        ALOGW_IF(err != OK, "failed to disconnect from surface: %d", err);
    }
    err = mCodec->configure(
            format, mNativeWindow, NULL /* crypto */, 0 /* flags */);
    if (err != OK) {
        DPD_MSG_ERROR("Failed to configure %s decoder (err=%d)", mComponentName.c_str(), err);
        handleError(err);
        return;
    }
    // the following should work in configured state
    CHECK_EQ((status_t)OK, mCodec->getOutputFormat(&mOutputFormat));
    CHECK_EQ((status_t)OK, mCodec->getInputFormat(&mInputFormat));

    err = mCodec->start();
    if (err != OK) {
        DPD_MSG_ERROR("Failed to start %s decoder (err=%d)", mComponentName.c_str(), err);
        handleError(err);
        return;
    }

    // the following should work after start
    CHECK_EQ((status_t)OK, mCodec->getInputBuffers(&mInputBuffers));
    CHECK_EQ((status_t)OK, mCodec->getOutputBuffers(&mOutputBuffers));
    DPD_MSG_HIGH("[%s] got %zu input and %zu output buffers",
            mComponentName.c_str(),
            mInputBuffers.size(),
            mOutputBuffers.size());

    requestCodecNotification();
}

/** @brief:  Register activity notification to mediacodec
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::requestCodecNotification() {
    if (mCodec != NULL) {
        sp<AMessage> reply = new AMessage(kWhatCodecNotify, this);
        reply->setInt32("generation", mBufferGeneration);
        mCodec->requestActivityNotification(reply);
    }
        }

bool DashPlayer::Decoder::isStaleReply(const sp<AMessage> &msg) {
    int32_t generation;
    CHECK(msg->findInt32("generation", &generation));
    return generation != mBufferGeneration;
    }

void DashPlayer::Decoder::init() {
    mDecoderLooper->registerHandler(this);
}

/** @brief: configure decoder
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::configure(const sp<MetaData> &meta) {
    sp<AMessage> msg = new AMessage(kWhatConfigure, this);
    sp<AMessage> format = makeFormat(meta);
    msg->setMessage("format", format);
    msg->post();
}

/** @brief: notify decoder error to dashplayer
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::handleError(int32_t err)
{
    DPD_MSG_HIGH("[%s] handleError : %d", mComponentName.c_str() , err);
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

/** @brief: send input buffer from codec to  dashplayer
 *
 *  @return: true if valid buffer found
 *
 */
bool DashPlayer::Decoder::handleAnInputBuffer() {
    size_t bufferIx = -1;
    status_t res = mCodec->dequeueInputBuffer(&bufferIx);
    DPD_MSG_HIGH("[%s] dequeued input: %d",
            mComponentName.c_str(), res == OK ? (int)bufferIx : res);
    if (res != OK) {
        if (res != -EAGAIN) {
            handleError(res);
        }
        return false;
    }

    CHECK_LT(bufferIx, mInputBuffers.size());

    sp<AMessage> reply = new AMessage(kWhatInputBufferFilled, this);
    reply->setSize("buffer-ix", bufferIx);
    reply->setInt32("generation", mBufferGeneration);

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFillThisBuffer);
    notify->setBuffer("buffer", mInputBuffers[bufferIx]);
    notify->setMessage("reply", reply);
    notify->post();
    return true;
}

/** @brief: Send input buffer to  decoder
 *
 *  @return: void
 *
 */
void android::DashPlayer::Decoder::onInputBufferFilled(const sp<AMessage> &msg) {
    size_t bufferIx;
    CHECK(msg->findSize("buffer-ix", &bufferIx));
    CHECK_LT(bufferIx, mInputBuffers.size());
    sp<ABuffer> codecBuffer = mInputBuffers[bufferIx];

    sp<ABuffer> buffer;
    bool hasBuffer = msg->findBuffer("buffer", &buffer);
    if (buffer == NULL /* includes !hasBuffer */) {
        int32_t streamErr = ERROR_END_OF_STREAM;
        CHECK(msg->findInt32("err", &streamErr) || !hasBuffer);

        if (streamErr == OK) {
            /* buffers are returned to hold on to */
            return;
        }

        // attempt to queue EOS
        status_t err = mCodec->queueInputBuffer(
                bufferIx,
                0,
                0,
                0,
                MediaCodec::BUFFER_FLAG_EOS);
        if (streamErr == ERROR_END_OF_STREAM && err != OK) {
            streamErr = err;
            // err will not be ERROR_END_OF_STREAM
        }

        if (streamErr != ERROR_END_OF_STREAM) {
            handleError(streamErr);
        }
    } else {
        int64_t timeUs = 0;
        uint32_t flags = 0;
        CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

        int32_t eos;
        // we do not expect CODECCONFIG or SYNCFRAME for decoder
        if (buffer->meta()->findInt32("eos", &eos) && eos) {
            flags |= MediaCodec::BUFFER_FLAG_EOS;
        }

        DPD_MSG_MEDIUM("Input buffer:[%s]: %p", mComponentName.c_str(),  buffer->data());

        // copy into codec buffer
        if (buffer != codecBuffer) {
            CHECK_LE(buffer->size(), codecBuffer->capacity());
            codecBuffer->setRange(0, buffer->size());
            memcpy(codecBuffer->data(), buffer->data(), buffer->size());
        }

        status_t err = mCodec->queueInputBuffer(
                        bufferIx,
                        codecBuffer->offset(),
                        codecBuffer->size(),
                        timeUs,
                        flags);
        if (err != OK) {
            DPD_MSG_ERROR("Failed to queue input buffer for %s (err=%d)",
                    mComponentName.c_str(), err);
            handleError(err);
        }
    }
}

/** @brief: dequeue out buffer from mediacodec and send it to renderer
 *
 *  @return: void
 *
 */
bool DashPlayer::Decoder::handleAnOutputBuffer() {
    size_t bufferIx = -1;
    size_t offset;
    size_t size;
    int64_t timeUs;
    uint32_t flags;
    status_t res = mCodec->dequeueOutputBuffer(
            &bufferIx, &offset, &size, &timeUs, &flags);

    if (res != OK) {
        DPD_MSG_HIGH("[%s] dequeued output: %d", mComponentName.c_str(), res);
    } else {
        DPD_MSG_HIGH("[%s] dequeued output: %d (time=%lld flags=%u)",
                mComponentName.c_str(), (int)bufferIx, timeUs, flags);
    }

    if (res == INFO_OUTPUT_BUFFERS_CHANGED) {
        res = mCodec->getOutputBuffers(&mOutputBuffers);
        if (res != OK) {
            DPD_MSG_ERROR("Failed to get output buffers for %s after INFO event (err=%d)",
                    mComponentName.c_str(), res);
            handleError(res);
            return false;
        }
        // DashPlayer ignores this
        return true;
    } else if (res == INFO_FORMAT_CHANGED) {
        sp<AMessage> format = new AMessage();
        res = mCodec->getOutputFormat(&format);
        if (res != OK) {
            DPD_MSG_ERROR("Failed to get output format for %s after INFO event (err=%d)",
                    mComponentName.c_str(), res);
            handleError(res);
            return false;
        }

        /* Computation of dpbSize

           #dpbSize = #output buffers
                      - 2 extrabuffers allocated by firmware
                      - minUndequeuedBufs (query from native window)
                      - 3 extrabuffers allocated by codec
           If extrabuffers allocated by firmware or ACodec changes,
           above eq. needs to be updated
        */

        int dpbSize = 0;
        if (mNativeWindow != NULL) {
            sp<ANativeWindow> nativeWindow = mNativeWindow.get();
            if (nativeWindow != NULL) {
                int minUndequeuedBufs = 0;
                status_t err = nativeWindow->query(nativeWindow.get(),
                    NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &minUndequeuedBufs);
                if (err == NO_ERROR) {
                    dpbSize = (mOutputBuffers.size() - minUndequeuedBufs - 5) > 0 ?
                        (mOutputBuffers.size() - minUndequeuedBufs - 5) : 0;
                    DPD_MSG_ERROR("[%s] computed DPB size of video stream = %d",
                        mComponentName.c_str(), dpbSize);
                }
            }
        }

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatOutputFormatChanged);
        notify->setMessage("format", format);
        notify->setInt32("dpb-size", dpbSize);
        notify->post();
        return true;
    } else if (res == INFO_DISCONTINUITY) {
        // nothing to do
        return true;
    } else if (res != OK) {
        if (res != -EAGAIN) {
            handleError(res);
        }
        return false;
    }

    // FIXME: This should be handled after rendering is complete,
    // but Renderer needs it now
    if (flags & MediaCodec::BUFFER_FLAG_EOS) {
        DPD_MSG_ERROR("queueing eos [%s]", mComponentName.c_str());

        status_t err;
        err = mCodec->releaseOutputBuffer(bufferIx);
        if (err != OK) {
            DPD_MSG_ERROR("failed to release output buffer for %s (err=%d)",
                 mComponentName.c_str(), err);
          handleError(err);
        }

        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatEOS);
        notify->setInt32("err", ERROR_END_OF_STREAM);
        notify->post();
        return true;
    }

    CHECK_LT(bufferIx, mOutputBuffers.size());
    sp<ABuffer> buffer = mOutputBuffers[bufferIx];
    buffer->setRange(offset, size);

    sp<RefBase> obj;
    sp<GraphicBuffer> graphicBuffer;
    if (buffer->meta()->findObject("graphic-buffer", &obj)) {
        graphicBuffer = static_cast<GraphicBuffer*>(obj.get());
    }

    buffer->meta()->clear();
    buffer->meta()->setInt64("timeUs", timeUs);
    if (flags & MediaCodec::BUFFER_FLAG_EOS) {
        buffer->meta()->setInt32("eos", true);
    }
    // we do not expect CODECCONFIG or SYNCFRAME for decoder

    sp<AMessage> reply = new AMessage(kWhatRenderBuffer, this);
    reply->setSize("buffer-ix", bufferIx);
    reply->setInt32("generation", mBufferGeneration);

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatDrainThisBuffer);

    if(flags & MediaCodec::BUFFER_FLAG_EXTRADATA) {
       buffer->meta()->setInt32("extradata", 1);
    }

    buffer->meta()->setObject("graphic-buffer", graphicBuffer);
    notify->setBuffer("buffer", buffer);
    notify->setMessage("reply", reply);
    notify->post();

    return true;
}

/** @brief: Give buffer to mediacodec for rendering
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::onRenderBuffer(const sp<AMessage> &msg) {
    status_t err;
    int32_t render;
    size_t bufferIx;
    CHECK(msg->findSize("buffer-ix", &bufferIx));
    if (msg->findInt32("render", &render) && render) {
        err = mCodec->renderOutputBufferAndRelease(bufferIx);
    } else {
        err = mCodec->releaseOutputBuffer(bufferIx);
    }
    if (err != OK) {
        DPD_MSG_ERROR("failed to release output buffer for %s (err=%d)",
                mComponentName.c_str(), err);
        handleError(err);
    }
    }

/** @brief: notify decoder flush complete to dashplayer
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::onFlush() {
    status_t err = OK;
    if (mCodec != NULL) {
        err = mCodec->flush();
        ++mBufferGeneration;
    }

    if (err != OK) {
        DPD_MSG_ERROR("failed to flush %s (err=%d)", mComponentName.c_str(), err);
        handleError(err);
        // finish with posting kWhatFlushCompleted.
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushCompleted);
    notify->post();
}

/** @brief: notify decoder shutdown complete to dashplayer
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::onShutdown() {
    status_t err = OK;
    if (mCodec != NULL) {
        err = mCodec->release();
        mCodec = NULL;
        ++mBufferGeneration;

        if (mNativeWindow != NULL) {
            // reconnect to surface as MediaCodec disconnected from it
            status_t error =
                    native_window_api_connect(
                            mNativeWindow.get(),
                            NATIVE_WINDOW_API_MEDIA);
            ALOGW_IF(error != NO_ERROR,
                    "[%s] failed to connect to native window, error=%d",
                    mComponentName.c_str(), error);
        }
        mComponentName = "decoder";
    }

    if (err != OK) {
        DPD_MSG_ERROR("failed to release %s (err=%d)", mComponentName.c_str(), err);
        handleError(err);
        // finish with posting kWhatShutdownCompleted.
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatShutdownCompleted);
    notify->post();
}

/** @brief: message handler to handle dashplayer/mediacodec messages
 *
 *  @return: void
 *
 */
void DashPlayer::Decoder::onMessageReceived(const sp<AMessage> &msg) {
    DPD_MSG_HIGH("[%s] onMessage: %s", mComponentName.c_str(), msg->debugString().c_str());

    switch (msg->what()) {
        case kWhatConfigure:
        {
            sp<AMessage> format;
            CHECK(msg->findMessage("format", &format));
            onConfigure(format);
            break;
        }

        case kWhatCodecNotify:
        {
            if (!isStaleReply(msg)) {
                while (handleAnInputBuffer()) {
                }

                while (handleAnOutputBuffer()) {
                }
            }

            requestCodecNotification();
            break;
        }

        case kWhatInputBufferFilled:
        {
            if (!isStaleReply(msg)) {
                onInputBufferFilled(msg);
            }
            break;
        }

        case kWhatRenderBuffer:
        {
            if (!isStaleReply(msg)) {
                onRenderBuffer(msg);
            }
            break;
        }

        case kWhatFlush:
        {
            onFlush();
            break;
            }

        case kWhatShutdown:
        {
            onShutdown();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void DashPlayer::Decoder::signalFlush() {
    (new AMessage(kWhatFlush, this))->post();
}

void DashPlayer::Decoder::signalResume() {
    // nothing to do
}

void DashPlayer::Decoder::initiateShutdown() {
    (new AMessage(kWhatShutdown, this))->post();
}


/** @brief: convert input metadat into AMessage format
 *
 *  @return: input format value in AMessage
 *
 */
sp<AMessage> DashPlayer::Decoder::makeFormat(const sp<MetaData> &meta) {
    sp<AMessage> msg;
    CHECK_EQ(convertMetaDataToMessage(meta, &msg), (status_t)OK);
    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if(!strncasecmp(mime, "video/", strlen("video/"))){
       msg->setInt32("max-height", MAX_HEIGHT);
       msg->setInt32("max-width", MAX_WIDTH);
       msg->setInt32("enable-extradata-user", 1);

       // Below property requie to set to prefer adaptive playback
       // msg->setInt32("prefer-adaptive-playback", 1);
    }

    return msg;
}

}  // namespace android

