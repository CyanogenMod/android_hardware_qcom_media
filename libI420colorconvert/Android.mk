LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
ifeq ($(call is-chipset-in-board-platform,msm7627),true)
    LOCAL_CFLAGS += -DTARGET7x27
endif
ifeq ($(call is-board-platform,msm7627a),true)
    LOCAL_CFLAGS += -DTARGET7x27A
endif

LOCAL_SRC_FILES := \
    ColorConvert.cpp

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/native/include/media/editor \
        $(TOP)/frameworks/av/include/media/stagefright \
        $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_SHARED_LIBRARIES := libutils \
                          libdl \
                          libcutils

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := libI420colorconvert

include $(BUILD_SHARED_LIBRARY)
