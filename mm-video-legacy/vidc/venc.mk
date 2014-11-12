ifneq ($(BUILD_TINY_ANDROID),true)

ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libmm-venc-def += -D__align=__alignx
libmm-venc-def += -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libmm-venc-def += -DT_ARM
libmm-venc-def += -Dinline=__inline
libmm-venc-def += -D_ANDROID_
libmm-venc-def += -UENABLE_DEBUG_LOW
libmm-venc-def += -DENABLE_DEBUG_HIGH
libmm-venc-def += -DENABLE_DEBUG_ERROR
libmm-venc-def += -UINPUT_BUFFER_LOG
libmm-venc-def += -UOUTPUT_BUFFER_LOG
libmm-venc-def += -USINGLE_ENCODER_INSTANCE
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
libmm-venc-def += -DMAX_RES_1080P
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
libmm-venc-def += -DMAX_RES_1080P
libmm-venc-def += -DMAX_RES_1080P_EBI
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
libmm-venc-def += -DMAX_RES_1080P
libmm-venc-def += -DMAX_RES_1080P_EBI
libmm-venc-def += -DBADGER
endif
ifeq ($(TARGET_USES_ION),true)
libmm-venc-def += -DUSE_ION
endif

venc-inc       := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

libmm-venc-def += -D_ANDROID_ICS_
# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVenc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libmm-venc-inc      := $(LOCAL_PATH)/venc/inc
libmm-venc-inc      += $(OMX_VIDEO_PATH)/vidc/common/inc
libmm-venc-inc      += $(call project-path-for,qcom-media)/mm-core/inc
libmm-venc-inc      += $(call project-path-for,qcom-media)/libstagefrighthw
libmm-venc-inc      += $(TARGET_OUT_HEADERS)/qcom/display
libmm-venc-inc      += $(TARGET_OUT_HEADERS)/adreno
libmm-venc-inc      += $(TARGET_OUT_HEADERS)/adreno200
libmm-venc-inc      += frameworks/native/include/media/hardware
libmm-venc-inc      += frameworks/native/include/media/openmax
libmm-venc-inc      += $(call project-path-for,qcom-media)/libc2dcolorconvert
libmm-venc-inc      += frameworks/av/include/media/stagefright
libmm-venc-inc      += $(venc-inc)

LOCAL_MODULE                    := libOmxVenc
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libmm-venc-def)
LOCAL_C_INCLUDES                := $(libmm-venc-inc)

LOCAL_SHARED_LIBRARIES    := liblog libutils libbinder libcutils \
                             libc2dcolorconvert libdl

LOCAL_SRC_FILES   := venc/src/omx_video_base.cpp
LOCAL_SRC_FILES   += venc/src/omx_video_encoder.cpp
ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
LOCAL_SRC_FILES   += venc/src/video_encoder_device_copper.cpp
else
LOCAL_SRC_FILES   += venc/src/video_encoder_device.cpp
endif

LOCAL_SRC_FILES   += common/src/extra_data_handler.cpp

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

# -----------------------------------------------------------------------------
#  #                       Make the apps-test (mm-venc-omx-test720p)
# -----------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-venc-test720p-inc            := $(TARGET_OUT_HEADERS)/mm-core
mm-venc-test720p-inc            += $(LOCAL_PATH)/venc/inc
mm-venc-test720p-inc            += $(OMX_VIDEO_PATH)/vidc/common/inc
mm-venc-test720p-inc            += $(call project-path-for,qcom-media)/mm-core/inc
mm-venc-test720p-inc            += $(TARGET_OUT_HEADERS)/qcom/display
mm-venc-test720p-inc            += $(venc-inc)

LOCAL_MODULE                    := mm-venc-omx-test720p
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libmm-venc-def)
LOCAL_C_INCLUDES                := $(mm-venc-test720p-inc)
LOCAL_SHARED_LIBRARIES          := libmm-omxcore libOmxVenc libbinder

LOCAL_SRC_FILES                 := venc/test/venc_test.cpp
LOCAL_SRC_FILES                 += venc/test/camera_test.cpp
LOCAL_SRC_FILES                 += venc/test/venc_util.c
LOCAL_SRC_FILES                 += venc/test/fb_test.c

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_EXECUTABLE)

# -----------------------------------------------------------------------------
# 			Make the apps-test (mm-video-driver-test)
# -----------------------------------------------------------------------------

include $(CLEAR_VARS)

venc-test-inc                   += $(LOCAL_PATH)/venc/inc
venc-test-inc                   += $(venc-inc)

LOCAL_MODULE                    := mm-video-encdrv-test
LOCAL_MODULE_TAGS               := optional
LOCAL_C_INCLUDES                := $(venc-test-inc)
LOCAL_C_INCLUDES                += $(call project-path-for,qcom-media)/mm-core/inc


LOCAL_SRC_FILES                 := venc/test/video_encoder_test.c
LOCAL_SRC_FILES                 += venc/test/queue.c

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_EXECUTABLE)

endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
# 					END
# ---------------------------------------------------------------------------------
