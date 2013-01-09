ifneq ($(filter msm8960 msm7x30,$(TARGET_BOARD_PLATFORM)),)

include $(call all-subdir-makefiles)

endif
