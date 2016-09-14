ifeq ($(call my-dir),$(call project-path-for,qcom-media))
QCOM_MEDIA_ROOT := $(call my-dir)

#Compile these for all targets under QCOM_BOARD_PLATFORMS list.
ifeq ($(call is-board-platform-in-list, $(QCOM_BOARD_PLATFORMS)),true)
include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk
endif

ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
ifneq ($(filter msm8909 msm8916 msm8939,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/mm-video-v4l2/Android.mk
include $(QCOM_MEDIA_ROOT)/libc2dcolorconvert/Android.mk

endif
endif
endif
