ifeq ($(call my-dir),$(call project-path-for,qcom-media))

ifneq (, $(filter aarch64 arm64, $(TARGET_ARCH)))
    $(info TODOAArch64: $(LOCAL_PATH)/Android.mk: Enable compile for 64 bit)
else
QCOM_MEDIA_ROOT := $(call my-dir)
ifneq ($(filter msm8610 msm8226 msm8974 msm8960 msm8660 msm7627a msm7630_surf apq8084 mpq8092 msm_bronze msm8916,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk
endif

ifneq ($(filter msm8610 msm8226 msm8974 msm8960 msm8660 msm7630_surf apq8084 mpq8092 msm_bronze msm8916,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/mm-video-v4l2/Android.mk
endif

ifneq ($(filter msm8610 msm8226 msm8974 msm8960 apq8084 mpq8092 msm_bronze msm8916,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/libc2dcolorconvert/Android.mk
endif

ifneq ($(filter apq8084,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/videopp/Android.mk
endif
endif # TARGET_ARCH

endif
