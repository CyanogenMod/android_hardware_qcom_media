AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
    LOCAL_CFLAGS += -DQCOM_HARDWARE
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8x60)
    include $(AUDIO_HW_ROOT)/msm8x60/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    include $(AUDIO_HW_ROOT)/msm7x30/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm7x27a)
    include $(AUDIO_HW_ROOT)/msm7x27a/Android.mk
endif

ifeq ($(TARGET_BOARD_PLATFORM),qsd8k)
    include $(AUDIO_HW_ROOT)/qsd8k/Android.mk
endif