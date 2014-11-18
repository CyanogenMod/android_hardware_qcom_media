ifeq ($(call my-dir),$(call project-path-for,qcom-media))

ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)

include $(call all-subdir-makefiles)

endif

endif
