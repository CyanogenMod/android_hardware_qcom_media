/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2012 Code Aurora Forum
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.
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

#include "II420ColorConverter.h"
#include <OMX_IVCommon.h>
#include <string.h>
#include <dlfcn.h>
#include <OMX_QCOMExtns.h>
#include <ColorConverter.h>
#include <utils/Log.h>
#include <cutils/atomic.h>
#include <media/stagefright/foundation/ADebug.h>

#define ALIGN( num, to ) (((num) + (to-1)) & (~(to-1)))

namespace android {

class I420ColorConverterWrapper {
public:
    I420ColorConverterWrapper();
    ~I420ColorConverterWrapper();

    static void* mLibHandle;
    static ConvertFn mConvert;
    static int mRefCount;

    static int getDecoderOutputFormat() {
        return QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka;
    }

    static int convertDecoderOutputToI420(
            void* srcBits, int srcWidth, int srcHeight,
            ARect srcRect, void* dstBits) {

        if (mConvert == NULL) {
            openColorConverterLib();
        }

        ColorConvertParams inputCP;
        inputCP.width = ALIGN(srcWidth, 128);
        inputCP.height = ALIGN(srcHeight, 32);
        inputCP.cropWidth = srcWidth;
        inputCP.cropHeight = srcHeight;
        inputCP.cropLeft = 0;
        inputCP.cropRight = srcWidth;
        inputCP.cropTop = 0;
        inputCP.cropBottom = srcHeight;
        inputCP.colorFormat = YCbCr420Tile;
        inputCP.data = (uint8_t *)srcBits;
        inputCP.flags = COLOR_CONVERT_ALIGN_NONE;
        inputCP.fd = -1;

        ColorConvertParams outputCP;
        outputCP.width = srcWidth;
        outputCP.height = srcHeight;
        outputCP.cropWidth = srcWidth;
        outputCP.cropHeight = srcHeight;
        outputCP.cropLeft = 0;
        outputCP.cropRight = srcWidth;
        outputCP.cropTop = 0;
        outputCP.cropBottom = srcHeight;
        outputCP.fd = -1;

        outputCP.flags = COLOR_CONVERT_ALIGN_NONE;
        outputCP.colorFormat = YCbCr420P;
        outputCP.data = (uint8_t *)dstBits;

        mConvert( inputCP, outputCP, NULL);

        return 0;
    }

    static int getEncoderInputFormat() {
        return OMX_COLOR_FormatYUV420SemiPlanar;
    }

    static int convertI420ToEncoderInput(
            void* srcBits, int srcWidth, int srcHeight,
            int dstWidth, int dstHeight, ARect dstRect,
            void* dstBits) {

        if (mConvert == NULL) {
            openColorConverterLib();
        }

        ColorConvertParams inputCP;
        inputCP.width = srcWidth;
        inputCP.height = srcHeight;
        inputCP.cropWidth = srcWidth;
        inputCP.cropHeight = srcHeight;
        inputCP.cropLeft = 0;
        inputCP.cropRight = srcWidth;
        inputCP.cropTop = 0;
        inputCP.cropBottom = srcHeight;
        inputCP.colorFormat = YCbCr420P;
        inputCP.flags = COLOR_CONVERT_ALIGN_NONE;
        inputCP.data = (uint8_t *)srcBits;
        inputCP.fd = -1;

        ColorConvertParams outputCP;
        outputCP.width = dstWidth;
        outputCP.height = dstHeight;
        outputCP.cropWidth = dstWidth;
        outputCP.cropHeight = dstHeight;
        outputCP.cropLeft = 0;
        outputCP.cropRight = dstWidth;
        outputCP.cropTop = 0;
        outputCP.cropBottom = dstHeight;
        outputCP.colorFormat = YCbCr420SP;
        outputCP.flags = COLOR_CONVERT_ALIGN_2048;
        outputCP.data = (uint8_t *)dstBits;
        outputCP.fd = -1;

        mConvert( inputCP, outputCP, NULL);

        return 0;
    }

    static int getEncoderInputBufferInfo(
            int actualWidth, int actualHeight,
            int* encoderWidth, int* encoderHeight,
            ARect* encoderRect, int* encoderBufferSize) {

        *encoderWidth = actualWidth;
        *encoderHeight = actualHeight;
        encoderRect->left = 0;
        encoderRect->top = 0;
        encoderRect->right = actualWidth - 1;
        encoderRect->bottom = actualHeight - 1;
        unsigned long sizeY = ALIGN(actualWidth*actualHeight,2048);
        unsigned long sizeU = ALIGN(actualWidth*actualHeight/2, 2048);
        unsigned long size  = sizeY + sizeU;

        *encoderBufferSize = (size);

        return 0;
    }

    static void openColorConverterLib() {

        int prevRefCount =  android_atomic_inc(&mRefCount);
        if (prevRefCount > 0) {
            return;
        }

        mLibHandle = dlopen("libmm-color-convertor.so", RTLD_NOW);
        mConvert = NULL;

        if (mLibHandle) {
            mConvert = (ConvertFn)dlsym(mLibHandle,
                    "_ZN7android7convertENS_18ColorConvertParamsES0_Ph");
            if(mConvert != NULL) {
                ALOGV("Successfully acquired mConvert symbol");
            }
        }
        else {
            ALOGE("Could not get yuvconversion lib handle");
            CHECK(0);
        }
    }

    static void closeColorConverterLib() {

        int prevRefCount = android_atomic_dec(&mRefCount);
        if (prevRefCount > 1) {
            return;
        }

        dlclose(mLibHandle);
        mLibHandle = NULL;
        mConvert = NULL;
    }
};

ConvertFn I420ColorConverterWrapper::mConvert = NULL;
void * I420ColorConverterWrapper::mLibHandle = NULL;
int I420ColorConverterWrapper::mRefCount = 0;

} // namespace - android

extern "C" void getI420ColorConverter(II420ColorConverter *converter) {
    converter->getDecoderOutputFormat = android::I420ColorConverterWrapper::getDecoderOutputFormat;
    converter->convertDecoderOutputToI420 = android::I420ColorConverterWrapper::convertDecoderOutputToI420;
    converter->getEncoderInputFormat = android::I420ColorConverterWrapper::getEncoderInputFormat;
    converter->convertI420ToEncoderInput = android::I420ColorConverterWrapper::convertI420ToEncoderInput;
    converter->getEncoderInputBufferInfo = android::I420ColorConverterWrapper::getEncoderInputBufferInfo;
    converter->openColorConverterLib = android::I420ColorConverterWrapper::openColorConverterLib;
    converter->closeColorConverterLib = android::I420ColorConverterWrapper::closeColorConverterLib;
}

