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

#ifndef DASHPLAYER_DECODER_H_

#define DASHPLAYER_DECODER_H_

#include "DashPlayerRenderer.h"
#include "DashPlayer.h"
#include <media/stagefright/foundation/AHandler.h>

namespace android {

struct ABuffer;
struct MediaCodec;

struct DashPlayer::Decoder : public AHandler {
    Decoder(const sp<AMessage> &notify,
            const sp<Surface> &nativeWindow = NULL);

    void configure(const sp<MetaData> &meta);
    void init();

    void signalFlush();
    void signalResume();
    void initiateShutdown();


    enum {
        kWhatFillThisBuffer      = 'flTB',
        kWhatDrainThisBuffer     = 'drTB',
        kWhatOutputFormatChanged = 'fmtC',
        kWhatFlushCompleted      = 'flsC',
        kWhatShutdownCompleted   = 'shDC',
        kWhatEOS                 = 'eos ',
        kWhatError               = 'err ',
    };

protected:
    virtual ~Decoder();

    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kWhatCodecNotify        = 'cdcN',
        kWhatConfigure          = 'conf',
        kWhatInputBufferFilled  = 'inpF',
        kWhatRenderBuffer       = 'rndr',
        kWhatFlush              = 'flus',
        kWhatShutdown           = 'shuD',
    };

    sp<AMessage> mNotify;
    sp<Surface> mNativeWindow;

    sp<AMessage> mInputFormat;
    sp<AMessage> mOutputFormat;
    sp<MediaCodec> mCodec;
    sp<ALooper> mCodecLooper;
    sp<ALooper> mDecoderLooper;

    Vector<sp<ABuffer> > mInputBuffers;
    Vector<sp<ABuffer> > mOutputBuffers;

    void handleError(int32_t err);
    bool handleAnInputBuffer();
    bool handleAnOutputBuffer();

    void requestCodecNotification();
    bool isStaleReply(const sp<AMessage> &msg);

    int mLogLevel;

    sp<AMessage> makeFormat(const sp<MetaData> &meta);

    void onConfigure(const sp<AMessage> &format);
    void onFlush();
    void onInputBufferFilled(const sp<AMessage> &msg);
    void onRenderBuffer(const sp<AMessage> &msg);
    void onShutdown();

    int32_t mBufferGeneration;
    AString mComponentName;


    DISALLOW_EVIL_CONSTRUCTORS(Decoder);
};

}  // namespace android

#endif  // DASHPLAYER_DECODER_H_
