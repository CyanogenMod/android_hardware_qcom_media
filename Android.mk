QCOM_MEDIA_ROOT := $(call my-dir)

ifeq ($(BOARD_USES_QCOM_HARDWARE),true) 
ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
	=include $(QCOM_MEDIA_ROOT)/libstagefrighthw-7x30/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
	=include $(QCOM_MEDIA_ROOT)/libstagefrighthw-8x60/Android.mk
endif
ifneq ($(BOARD_PREBUILT_LIBAUDIO),true)
	include $(QCOM_MEDIA_ROOT)/audio/Android.mk
endif
ifeq ($(BOARD_USES_QCOM_LIBS),true)
	include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
	include $(QCOM_MEDIA_ROOT)/mm-video/Android.mk
endif
endif
