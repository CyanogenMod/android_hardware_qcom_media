AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
    include $(AUDIO_HW_ROOT)/msm8660/Android.mk
endif

ifeq ($(call is-board-platform,msm7630_surf),true)
    include $(AUDIO_HW_ROOT)/msm7630/Android.mk
endif
