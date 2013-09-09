LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_QCOM_DISPLAY_VARIANT),caf)
DISPLAY := display-caf
else
DISPLAY := display
endif

LOCAL_COPY_HEADERS_TO         := linux
LOCAL_COPY_HEADERS            := ../../../../$(TARGET_KERNEL_SOURCE)/include/linux/msm_kgsl.h
#Copy the headers regardless of whether libc2dcolorconvert is built
include $(BUILD_COPY_HEADERS)

LOCAL_SRC_FILES := \
        C2DColorConverter.cpp

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/av/include/media/stagefright \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/hardware/qcom/$(DISPLAY)/libcopybit \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_SHARED_LIBRARIES := liblog libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := libc2dcolorconvert

include $(BUILD_SHARED_LIBRARY)
