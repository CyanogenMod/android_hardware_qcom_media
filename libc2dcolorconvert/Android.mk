LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        C2DColorConverter.cpp

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/av/include/media/stagefright \
    $(TOP)/frameworks/native/include/media/openmax

ifneq ($(filter msm8974 msm8x74,$(TARGET_BOARD_PLATFORM)),)
LOCAL_C_INCLUDES += \
    $(TOP)/hardware/qcom/display/msm8974/libcopybit
else
LOCAL_C_INCLUDES += \
    $(TOP)/hardware/qcom/display/msm8960/libcopybit
endif

LOCAL_SHARED_LIBRARIES := liblog libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := libc2dcolorconvert

include $(BUILD_SHARED_LIBRARY)
