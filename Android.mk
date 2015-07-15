# TODO:  Find a better way to separate build configs for ADP vs non-ADP devices
ifneq ($(TARGET_BOARD_AUTO),true)
  QCOM_MEDIA_ROOT := $(call my-dir)
  ifneq ($(filter msm8610 msm8226 msm8974 msm8960 msm8660 msm7627a msm7630_surf msm8084 msm8992 msm8994,$(TARGET_BOARD_PLATFORM)),)
    include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
    include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk
  endif

  ifneq ($(filter msm8960 msm8660,$(TARGET_BOARD_PLATFORM)),)
    include $(QCOM_MEDIA_ROOT)/mm-video-legacy/Android.mk
  endif

  ifneq ($(filter msm8610 msm8226 msm8974 msm8084 msm8992 msm8994,$(TARGET_BOARD_PLATFORM)),)
    include $(QCOM_MEDIA_ROOT)/mm-video-v4l2/Android.mk
  endif

  ifneq ($(filter msm8610 msm8226 msm8974 msm8960 msm8084 msm8992 msm8994,$(TARGET_BOARD_PLATFORM)),)
    include $(QCOM_MEDIA_ROOT)/libc2dcolorconvert/Android.mk
  endif
endif
