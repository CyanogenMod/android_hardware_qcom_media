AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),msm8x60)
    include $(AUDIO_HW_ROOT)/msm8x60/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    include $(AUDIO_HW_ROOT)/msm7x30/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm7x27a)
    include $(AUDIO_HW_ROOT)/msm7x27a/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),qsd8)
    include $(AUDIO_HW_ROOT)/qsd8/Android.mk
endif