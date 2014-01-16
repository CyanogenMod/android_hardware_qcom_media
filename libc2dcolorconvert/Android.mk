LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)


ifneq ($(TARGET_QCOM_DISPLAY_VARIANT),)
DISPLAY := display-$(TARGET_QCOM_DISPLAY_VARIANT)
else
DISPLAY := display/$(TARGET_BOARD_PLATFORM)
# Fix the header inclusions for platform variants without an explicit path
ifneq ($(filter msm8610 apq8084 mpq8092,$(TARGET_BOARD_PLATFORM)),)
    DISPLAY := display/msm8974
endif
ifneq ($(filter msm8660 ,$(TARGET_BOARD_PLATFORM)),)
    DISPLAY := display/msm8960
endif
endif

LOCAL_SRC_FILES := \
        C2DColorConverter.cpp

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/av/include/media/stagefright \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/hardware/qcom/$(DISPLAY)/libcopybit \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SHARED_LIBRARIES := liblog libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := libc2dcolorconvert

include $(BUILD_SHARED_LIBRARY)
