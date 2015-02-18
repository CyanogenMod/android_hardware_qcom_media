/*
 *Copyright (c) 2013, The Linux Foundation. All rights reserved.
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
#define LOG_TAG "DASHFactory"
#include <media/IMediaPlayer.h>
#include <utils/Log.h>
#include "DashPlayerDriver.h"
#include "MediaPlayerFactory.h"

namespace android {

class DashPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (!strncasecmp("http://", url, 7)) {
            size_t len = strlen(url);
            if (len >= 5 && !strcasecmp(".mpd", &url[len - 4])) {
                ALOGI("Using DashPlayer for .mpd");
                return 1.0;
            }
        }
        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource> & /*source*/,
                               float /*curScore*/) {
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV("DashPlayerFactory::createPlayer");
        return new DashPlayerDriver;
    }
};

extern "C" MediaPlayerFactory::IFactory* CreateDASHFactory()
{
  return new DashPlayerFactory();
}

}  // namespace android
