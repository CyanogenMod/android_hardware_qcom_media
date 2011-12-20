
ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE:= audio_policy.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_STATIC_LIBRARIES := libmedia_helper
LOCAL_WHOLE_STATIC_LIBRARIES := libaudiopolicy_legacy
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia

LOCAL_SRC_FILES:= AudioPolicyManager.cpp

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_STATIC_LIBRARIES += libmedia_helper
LOCAL_WHOLE_STATIC_LIBRARIES := libaudiohw_legacy
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libhardware_legacy \
    libdl

LOCAL_SRC_FILES += AudioHardware.cpp

LOCAL_CFLAGS += -fno-short-enums

#ifeq ($(BOARD_HAVE_BLUETOOTH),true)
#  LOCAL_SHARED_LIBRARIES += audio.a2dp.default
#endif

include $(BUILD_SHARED_LIBRARY)

endif # not BUILD_TINY_ANDROID

