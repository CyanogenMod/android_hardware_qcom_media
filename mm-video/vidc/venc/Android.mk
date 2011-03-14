#--------------------------------------------------------------------------
#Copyright (c) 2010, Code Aurora Forum. All rights reserved.

#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of Code Aurora nor
#      the names of its contributors may be used to endorse or promote
#      products derived from this software without specific prior written
#      permission.

#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
#OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
#WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
#OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#--------------------------------------------------------------------------

ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libmm-venc-def := -g -O3 -Dlrintf=_ffix_r
libmm-venc-def += -D__align=__alignx
libmm-venc-def += -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libmm-venc-def += -DT_ARM
libmm-venc-def += -Dinline=__inline
libmm-venc-def += -D_ANDROID_
libmm-venc-def += -UENABLE_DEBUG_LOW
libmm-venc-def += -DENABLE_DEBUG_HIGH
libmm-venc-def += -DENABLE_DEBUG_ERROR

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVenc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libmm-venc-inc			:= $(LOCAL_PATH)/inc
libmm-venc-inc			+= $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE			:= libOmxVenc
LOCAL_CFLAGS	  		:= $(libmm-venc-def)
LOCAL_C_INCLUDES  		:= $(libmm-venc-inc)

LOCAL_SHARED_LIBRARIES		:= liblog libutils libbinder

LOCAL_SRC_FILES		:= src/omx_video_base.cpp
LOCAL_SRC_FILES		+= src/omx_video_encoder.cpp
LOCAL_SRC_FILES		+= src/video_encoder_device.cpp

include $(BUILD_SHARED_LIBRARY)

# -----------------------------------------------------------------------------
#  #                       Make the apps-test (mm-venc-omx-test720p)
# -----------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-venc-test720p-inc            := $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-venc-test720p-inc            += $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE                    := mm-venc-omx-test720p
LOCAL_CFLAGS                    := $(libmm-venc-def)
LOCAL_C_INCLUDES                := $(mm-venc-test720p-inc)
LOCAL_SHARED_LIBRARIES          := libmm-omxcore libOmxVenc libbinder

LOCAL_SRC_FILES                 := test/venc_test.cpp
LOCAL_SRC_FILES                 += test/camera_test.cpp
LOCAL_SRC_FILES                 += test/venc_util.c
LOCAL_SRC_FILES                 += test/fb_test.c

include $(BUILD_EXECUTABLE)

# -----------------------------------------------------------------------------
# 			Make the apps-test (mm-video-driver-test)
# -----------------------------------------------------------------------------

include $(CLEAR_VARS)

venc-test-inc                   += $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE                    := mm-video-encdrv-test
LOCAL_C_INCLUDES                := $(venc-test-inc)

LOCAL_SRC_FILES                 := test/video_encoder_test.c
LOCAL_SRC_FILES                 += test/queue.c

include $(BUILD_EXECUTABLE)
