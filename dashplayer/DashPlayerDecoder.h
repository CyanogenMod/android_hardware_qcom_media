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

struct DashPlayer::Decoder : public AHandler {
    Decoder(const sp<AMessage> &notify,
            const sp<NativeWindowWrapper> &nativeWindow = NULL);

    void configure(const sp<MetaData> &meta);

    void signalFlush();
    void signalResume();
    void initiateShutdown();
    void setSink(const sp<MediaPlayerBase::AudioSink> &sink, sp<Renderer> Renderer);

protected:
    virtual ~Decoder();

    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kWhatCodecNotify        = 'cdcN',
    };

    sp<AMessage> mNotify;
    sp<NativeWindowWrapper> mNativeWindow;

    sp<DashCodec> mCodec;
    sp<ALooper> mCodecLooper;
    sp<MediaPlayerBase::AudioSink> mAudioSink;
    sp<Renderer> mRenderer;

    Vector<sp<ABuffer> > mCSD;
    size_t mCSDIndex;

    sp<AMessage> makeFormat(const sp<MetaData> &meta);

    void onFillThisBuffer(const sp<AMessage> &msg);

    DISALLOW_EVIL_CONSTRUCTORS(Decoder);
};

}  // namespace android

#endif  // DASHPLAYER_DECODER_H_
