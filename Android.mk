QCOM_MEDIA_ROOT := $(call my-dir)

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
	include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk

ifneq ($(BOARD_PREBUILT_LIBAUDIO),true)
	include $(QCOM_MEDIA_ROOT)/audio/Android.mk
endif
ifeq ($(BOARD_USES_QCOM_LIBS),true)
	include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
	include $(QCOM_MEDIA_ROOT)/mm-video/Android.mk
endif
endif
