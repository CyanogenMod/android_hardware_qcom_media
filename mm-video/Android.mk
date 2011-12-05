OMX_VIDEO_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifneq ($(BUILD_TINY_ANDROID),true)

ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    include $(OMX_VIDEO_PATH)/vidc/vdec/Android.mk
    include $(OMX_VIDEO_PATH)/vidc/venc/Android.mk
endif

endif #BUILD_TINY_ANDROID
