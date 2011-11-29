QCOM_MEDIA_ROOT := $(call my-dir)

ifeq ($(call is-board-platform-in-list,$(MSM7K_BOARD_PLATFORMS)),true)
	include $(QCOM_MEDIA_ROOT)/audio/Android.mk
endif
