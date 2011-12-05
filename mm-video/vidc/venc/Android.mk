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
LOCAL_MODULE_TAGS := optional
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
