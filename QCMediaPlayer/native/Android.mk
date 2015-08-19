LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#            Common definitons
# ---------------------------------------------------------------------------------
LOCAL_SRC_FILES:= \
    qcmediaplayer.cpp \

LOCAL_SHARED_LIBRARIES := \
    libbinder \
    libcutils \
    libmedia \
    libutils \

LOCAL_C_INCLUDES := \
        $(TOP)/frameworks/av/include/media \

LOCAL_MODULE:= libqcmediaplayer

LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_ENABLE_QC_AV_ENHANCEMENTS), true)
include $(BUILD_SHARED_LIBRARY)
endif
