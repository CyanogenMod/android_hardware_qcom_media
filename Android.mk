QCOM_MEDIA_ROOT := $(call my-dir)

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
	media-hals := libstagefrighthw
	include $(call all-named-subdir-makefiles,$(media-hals))
	include $(QCOM_MEDIA_ROOT)/audio/Android.mk
ifeq ($(BOARD_USES_QCOM_LIBS),true)
	include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
	include $(QCOM_MEDIA_ROOT)/mm-video/Android.mk
endif
endif