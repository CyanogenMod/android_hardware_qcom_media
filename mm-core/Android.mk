OMX_CORE_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifneq ($(BUILD_TINY_ANDROID),true)

ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    include $(OMX_CORE_PATH)/omxcore/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),qsd8k)
    include $(OMX_CORE_PATH)/omxcore/Android.mk
endif

endif #BUILD_TINY_ANDROID
