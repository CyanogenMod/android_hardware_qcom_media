ifneq ($(filter msm8960 msm8974 msm8226,$(TARGET_BOARD_PLATFORM)),)

include $(call all-subdir-makefiles)

endif
