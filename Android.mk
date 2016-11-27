ifeq ($(call my-dir),$(call project-path-for,qcom-media))

QCOM_MEDIA_ROOT := $(call my-dir)

include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk

include $(QCOM_MEDIA_ROOT)/mm-video-v4l2/Android.mk
include $(QCOM_MEDIA_ROOT)/libc2dcolorconvert/Android.mk

endif
