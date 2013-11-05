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
#include <utils/Log.h>

#include "DashPlayerDecoder.h"
#include "DashCodec.h"
#include "ESDS.h"
#include "QCMediaDefs.h"
#include "QCMetaData.h"
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

namespace android {

DashPlayer::Decoder::Decoder(
        const sp<AMessage> &notify,
        const sp<NativeWindowWrapper> &nativeWindow)
    : mNotify(notify),
      mNativeWindow(nativeWindow) {
      mAudioSink = NULL;
}

DashPlayer::Decoder::~Decoder() {
    ALooper::handler_id id = 0;
    if (mCodec != NULL) {
        id = mCodec->id();
    }
    if (id != 0) {
        if (mCodecLooper != NULL) {
            mCodecLooper->stop();
            mCodecLooper->unregisterHandler(id);
        }
        looper()->unregisterHandler(id);
    }
}

void DashPlayer::Decoder::configure(const sp<MetaData> &meta) {
    CHECK(mCodec == NULL);

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    ALOGV("@@@@:: Decoder::configure :: mime is --- %s ---",mime);

    sp<AMessage> notifyMsg =
        new AMessage(kWhatCodecNotify, id());

    sp<AMessage> format = makeFormat(meta);

    if (mNativeWindow != NULL) {
        format->setObject("native-window", mNativeWindow);
    }

    // Current video decoders do not return from OMX_FillThisBuffer
    // quickly, violating the OpenMAX specs, until that is remedied
    // we need to invest in an extra looper to free the main event
    // queue.
    bool isVideo = !strncasecmp(mime, "video/", 6);

    if(!isVideo) {
        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
    }

    ALOGV("@@@@:: DashCodec created ");
    mCodec = new DashCodec;

    bool needDedicatedLooper = false;

    if (isVideo){
        needDedicatedLooper = true;
        if(mCodecLooper == NULL) {
            ALOGV("@@@@:: Creating Looper for %s",(isVideo?"Video":"Audio"));
            mCodecLooper = new ALooper;
            mCodecLooper->setName("DashPlayerDecoder");
            mCodecLooper->start(false, false, ANDROID_PRIORITY_AUDIO);
        }
    }

    (needDedicatedLooper ? mCodecLooper : looper())->registerHandler(mCodec);
     mCodec->setNotificationMessage(notifyMsg);
     mCodec->initiateSetup(format);

}

void DashPlayer::Decoder::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatCodecNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == DashCodec::kWhatFillThisBuffer) {
                onFillThisBuffer(msg);
            }else {
                sp<AMessage> notify = mNotify->dup();
                notify->setMessage("codec-request", msg);
                notify->post();
            }
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void DashPlayer::Decoder::setSink(const sp<MediaPlayerBase::AudioSink> &sink, sp<Renderer> Renderer) {
    mAudioSink = sink;
    mRenderer  = Renderer;
}


sp<AMessage> DashPlayer::Decoder::makeFormat(const sp<MetaData> &meta) {
    CHECK(mCSD.isEmpty());

    sp<AMessage> msg;
    uint32_t type;
    const void *data;
    size_t size;

    CHECK_EQ(convertMetaDataToMessage(meta, &msg), (status_t)OK);

    int32_t value;
    if (meta->findInt32(kKeySmoothStreaming, &value)) {
        msg->setInt32("smooth-streaming", value);
    }

    if (meta->findInt32(kKeyIsDRM, &value)) {
        msg->setInt32("secure-op", 1);
    }

    if (meta->findInt32(kKeyRequiresSecureBuffers, &value)) {
        msg->setInt32("requires-secure-buffers", 1);
    }

    if (meta->findInt32(kKeyEnableDecodeOrder, &value)) {
        msg->setInt32("decodeOrderEnable", value);
    }
    if (meta->findData(kKeyAacCodecSpecificData, &type, &data, &size)) {
          if (size > 0 && data != NULL) {
              sp<ABuffer> buffer = new ABuffer(size);
              if (buffer != NULL) {
                memcpy(buffer->data(), data, size);
                buffer->meta()->setInt32("csd", true);
                buffer->meta()->setInt64("timeUs", 0);
                msg->setBuffer("csd-0", buffer);
              }
              else {
                ALOGE("kKeyAacCodecSpecificData ABuffer Allocation failed");
              }
          }
          else {
              ALOGE("Not a valid data pointer or size == 0");
          }
    }


    mCSDIndex = 0;
    for (size_t i = 0;; ++i) {
        sp<ABuffer> csd;
        if (!msg->findBuffer(StringPrintf("csd-%d", i).c_str(), &csd)) {
            break;
        }

        mCSD.push(csd);
    }

    return msg;
}

void DashPlayer::Decoder::onFillThisBuffer(const sp<AMessage> &msg) {
    sp<AMessage> reply;
    CHECK(msg->findMessage("reply", &reply));

#if 0
    sp<ABuffer> outBuffer;
    CHECK(msg->findBuffer("buffer", &outBuffer));
#else
    sp<ABuffer> outBuffer;
#endif

    if (mCSDIndex < mCSD.size()) {
        outBuffer = mCSD.editItemAt(mCSDIndex++);
        outBuffer->meta()->setInt64("timeUs", 0);

        reply->setBuffer("buffer", outBuffer);
        reply->post();
        return;
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setMessage("codec-request", msg);
    notify->post();
}

void DashPlayer::Decoder::signalFlush() {
    if (mCodec != NULL) {
        mCodec->signalFlush();
    }
}

void DashPlayer::Decoder::signalResume() {
    if(mCodec != NULL) {
        mCodec->signalResume();
    }
}

void DashPlayer::Decoder::initiateShutdown() {
    if (mCodec != NULL) {
        mCodec->initiateShutdown();
   }
}

}  // namespace android

