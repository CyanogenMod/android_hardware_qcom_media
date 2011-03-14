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

libOmxVdec-def := -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libOmxVdec-def += -D__align=__alignx
libOmxVdec-def += -Dinline=__inline
libOmxVdec-def += -g -O3
libOmxVdec-def += -DIMAGE_APPS_PROC
libOmxVdec-def += -D_ANDROID_
libOmxVdec-def += -DCDECL
libOmxVdec-def += -DT_ARM
libOmxVdec-def += -DNO_ARM_CLZ
libOmxVdec-def += -UENABLE_DEBUG_LOW
libOmxVdec-def += -DENABLE_DEBUG_HIGH
libOmxVdec-def += -DENABLE_DEBUG_ERROR
libOmxVdec-def += -UMULTI_DEC_INST
libOmxVdec-def += -DMAX_RES_720P

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVdec)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

libmm-vdec-inc	        := $(LOCAL_PATH)/inc
libmm-vdec-inc	        += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE		:= libOmxVdec
LOCAL_CFLAGS		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES	:= $(libmm-vdec-inc)

LOCAL_SHARED_LIBRARIES	:= liblog libutils libbinder

LOCAL_SRC_FILES         := src/frameparser.cpp
LOCAL_SRC_FILES         += src/h264_utils.cpp
LOCAL_SRC_FILES         += src/omx_vdec.cpp

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
# 			Make the apps-test (mm-vdec-omx-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-test-inc		:= $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-vdec-test-inc		+= $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE			:= mm-vdec-omx-test
LOCAL_CFLAGS	  		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES  		:= $(mm-vdec-test-inc)
LOCAL_SHARED_LIBRARIES		:= libutils libOmxCore libOmxVdec libbinder

LOCAL_SRC_FILES                 := src/queue.c
LOCAL_SRC_FILES                 += test/omx_vdec_test.cpp

include $(BUILD_EXECUTABLE)

# ---------------------------------------------------------------------------------
# 			Make the driver-test (mm-video-driver-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-drv-test-inc		:= $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-vdec-drv-test-inc		+= $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE			:= mm-video-driver-test
LOCAL_CFLAGS	  		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES  		:= $(mm-vdec-drv-test-inc)

LOCAL_SRC_FILES                 := src/message_queue.c
LOCAL_SRC_FILES                 += test/decoder_driver_test.c

include $(BUILD_EXECUTABLE)
