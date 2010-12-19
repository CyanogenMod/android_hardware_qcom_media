/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#ifndef QCOM_HARDWARE_OVERLAY_RENDERER_H_

#define QCOM_HARDWARE_OVERLAY_RENDERER_H_

#include <media/stagefright/VideoRenderer.h>
#include <utils/RefBase.h>
#include <ui/Overlay.h>
#include <sys/types.h>

namespace android {

class ISurface;
class MemoryHeapPmem;

class QComHardwareOverlayRenderer : public VideoRenderer {
public:
    QComHardwareOverlayRenderer(
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            int32_t rotationDegrees);

    virtual ~QComHardwareOverlayRenderer();

    virtual void render(
            const void *data, size_t size, void *platformPrivate);

private:
    sp<ISurface> mISurface;
    size_t mDisplayWidth, mDisplayHeight;
    size_t mDecodedWidth, mDecodedHeight;
    size_t mFrameSize;
    int32_t mRotationDegrees;
    sp<MemoryHeapPmem> mMemoryHeap;

    //Statistics profiling
    bool mStatistics;
    uint32_t mLastFrame;
    float mFpsSum;
    uint32_t mFrameNumber;
    uint32_t mNumFpsSamples;
    int64_t mLastFrameTime;
    void AverageFPSProfiling();
    void AverageFPSPrint();

    bool getOffset(void *platformPrivate, size_t *offset);
    void publishBuffers(uint32_t pmem_fd);

    QComHardwareOverlayRenderer(const QComHardwareOverlayRenderer &);
    QComHardwareOverlayRenderer &operator=(const QComHardwareOverlayRenderer &);

    sp<Overlay>                 mOverlay;
    uint32_t                      mFd;
};

}  // namespace android

#endif  // QCOM_HARDWARE_OVERLAY_RENDERER_H_
