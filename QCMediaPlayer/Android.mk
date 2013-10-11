#/******************************************************************************
#*@file Android.mk
#*brief Rules for copiling the source files
#*  ******************************************************************************/

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := eng

#LOCAL_SRC_FILES := $(call all-subdir-java-files)
ifeq ($(PLATFORM_VERSION),4.3)
LOCAL_SRC_FILES :=com/qualcomm/qcmedia/QCMediaPlayer.java
else
LOCAL_SRC_FILES :=NonJB/com/qualcomm/qcmedia/QCMediaPlayer.java
endif

LOCAL_SRC_FILES += com/qualcomm/qcmedia/QCTimedText.java

LOCAL_MODULE := qcmediaplayer
LOCAL_MODULE_PATH := $(TARGET_OUT_JAVA_LIBRARIES)

ifeq ($(TARGET_ENABLE_QC_AV_ENHANCEMENTS), true)
ifndef TARGET_DISABLE_DASH
include $(BUILD_JAVA_LIBRARY)
endif
endif
