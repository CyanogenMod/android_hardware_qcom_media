LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)


ifneq ($(TARGET_QCOM_DISPLAY_VARIANT),)
PLATFORM := .
else
ifneq ($(filter msm8610 msm8226 msm8974 apq8084 mpq8092,$(TARGET_BOARD_PLATFORM)),)
PLATFORM := msm8974
endif
ifneq ($(filter msm8660 msm8960,$(TARGET_BOARD_PLATFORM)),)
PLATFORM := msm8960
endif
endif

LOCAL_SRC_FILES := \
        C2DColorConverter.cpp

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/av/include/media/stagefright \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/$(call project-path-for,qcom-display)/$(PLATFORM)/libcopybit \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SHARED_LIBRARIES := liblog libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := libc2dcolorconvert

include $(BUILD_SHARED_LIBRARY)
