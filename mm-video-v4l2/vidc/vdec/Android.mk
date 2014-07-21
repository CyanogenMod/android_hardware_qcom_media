ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH := $(call my-dir)
OMX_VIDEO_PATH := $(TOP)/hardware/qcom/media/mm-video-v4l2
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libOmxVdec-def := -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libOmxVdec-def += -D__align=__alignx
libOmxVdec-def += -Dinline=__inline
libOmxVdec-def += -g -O3
libOmxVdec-def += -DIMAGE_APPS_PROC
libOmxVdec-def += -D_ANDROID_
libOmxVdec-def += -DCDECL
libOmxVdec-def += -DT_ARM
libOmxVdec-def += -DNO_ARM_CLZ
libOmxVdec-def += -UENABLE_DEBUG_LOW
libOmxVdec-def += -UENABLE_DEBUG_HIGH
libOmxVdec-def += -DENABLE_DEBUG_ERROR
libOmxVdec-def += -UINPUT_BUFFER_LOG
libOmxVdec-def += -UOUTPUT_BUFFER_LOG
libOmxVdec-def += -Wno-parentheses
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -DTEST_TS_FROM_SEI
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8960_
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -D_HEVC_USE_ADSP_HEAP_
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm7627a)
libOmxVdec-def += -DMAX_RES_720P
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm7630_surf)
libOmxVdec-def += -DMAX_RES_720P
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8610)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -DSMOOTH_STREAMING_DISABLED
libOmxVdec-def += -DH264_PROFILE_LEVEL_CHECK
libOmxVdec-def += -D_MSM8974_
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8226)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -D_HEVC_USE_ADSP_HEAP_
endif
ifeq ($(TARGET_BOARD_PLATFORM),apq8084)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -DVENUS_HEVC
endif
ifeq ($(TARGET_BOARD_PLATFORM),mpq8092)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -DVENUS_HEVC
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm_bronze)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -D_HEVC_USE_ADSP_HEAP_
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8916)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
endif
ifeq ($(TARGET_BOARD_PLATFORM),ferrum)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8994)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_MSM8974_
libOmxVdec-def += -DVENUS_HEVC
endif

libOmxVdec-def += -D_ANDROID_ICS_

ifeq ($(TARGET_USES_ION),true)
libOmxVdec-def += -DUSE_ION
endif

ifneq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) >= 18 ))" )))
libOmxVdec-def += -DANDROID_JELLYBEAN_MR1=1
endif

vdec-inc          = $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVdec)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libmm-vdec-inc          += $(LOCAL_PATH)/inc 
libmm-vdec-inc          += $(OMX_VIDEO_PATH)/vidc/common/inc
libmm-vdec-inc          += $(TOP)/hardware/qcom/media/mm-core/inc
#DRM include - Interface which loads the DRM library
libmm-vdec-inc	        += $(OMX_VIDEO_PATH)/DivxDrmDecrypt/inc
libmm-vdec-inc          += $(TARGET_OUT_HEADERS)/qcom/display
libmm-vdec-inc          += $(TARGET_OUT_HEADERS)/adreno
libmm-vdec-inc          += $(TOP)/frameworks/native/include/media/openmax
libmm-vdec-inc          += $(TOP)/frameworks/native/include/media/hardware
libmm-vdec-inc          += $(vdec-inc)
libmm-vdec-inc      += $(TOP)/hardware/qcom/media/libc2dcolorconvert
libmm-vdec-inc      += $(TOP)/frameworks/av/include/media/stagefright
libmm-vdec-inc      += $(TARGET_OUT_HEADERS)/mm-video/SwVdec

ifeq ($(PLATFORM_SDK_VERSION), 18)  #JB_MR2
libOmxVdec-def += -DANDROID_JELLYBEAN_MR2=1
libmm-vdec-inc += $(TOP)/hardware/qcom/media/libstagefrighthw
endif

ifeq ($(call is-platform-sdk-version-at-least, 19),true)
# This feature is enabled for Android KK+
libOmxVdec-def += -DADAPTIVE_PLAYBACK_SUPPORTED
endif

LOCAL_MODULE                    := libOmxVdec
LOCAL_ADDITIONAL_DEPENDENCIES   := libOmxVdecHevc
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdec-def) -Werror
LOCAL_C_INCLUDES                += $(libmm-vdec-inc)

LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := liblog libutils libbinder libcutils libdl

LOCAL_SHARED_LIBRARIES  += libdivxdrmdecrypt
LOCAL_SHARED_LIBRARIES  += libqdMetaData

LOCAL_SRC_FILES         := src/frameparser.cpp
LOCAL_SRC_FILES         += src/h264_utils.cpp
LOCAL_SRC_FILES         += src/ts_parser.cpp
LOCAL_SRC_FILES         += src/mp4_utils.cpp
LOCAL_SRC_FILES         += src/hevc_utils.cpp
ifneq (,$(filter msm8974 msm8610 msm8226 apq8084 mpq8092 msm_bronze msm8916 msm8994 ferrum,$(TARGET_BOARD_PLATFORM)))
LOCAL_SRC_FILES         += src/omx_vdec_msm8974.cpp
endif

LOCAL_SRC_FILES         += ../common/src/extra_data_handler.cpp
LOCAL_SRC_FILES         += ../common/src/vidc_color_converter.cpp
LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)


# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVdecHevc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

# libOmxVdecHevc library is not built for OSS builds as QCPATH is null in OSS builds.

ifneq "$(wildcard $(QCPATH) )" ""
ifneq (,$(filter msm8974 msm8610 msm8226 msm_bronze msm8916,$(TARGET_BOARD_PLATFORM)))

LOCAL_MODULE                    := libOmxVdecHevc
LOCAL_ADDITIONAL_DEPENDENCIES   := libOmxVenc
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdec-def)
LOCAL_C_INCLUDES                += $(libmm-vdec-inc)

LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := liblog libutils libbinder libcutils libdl

LOCAL_SHARED_LIBRARIES  += libdivxdrmdecrypt
LOCAL_SHARED_LIBRARIES  += libqdMetaData

LOCAL_SRC_FILES         := src/frameparser.cpp
LOCAL_SRC_FILES         += src/h264_utils.cpp
LOCAL_SRC_FILES         += src/ts_parser.cpp
LOCAL_SRC_FILES         += src/mp4_utils.cpp

ifneq (,$(filter msm8974 msm8226 msm8916,$(TARGET_BOARD_PLATFORM)))
LOCAL_SHARED_LIBRARIES  += libHevcSwDecoder
LOCAL_SRC_FILES         += src/omx_vdec_hevc_swvdec.cpp
else
LOCAL_SRC_FILES         += src/omx_vdec_hevc.cpp
endif

LOCAL_SRC_FILES         += src/hevc_utils.cpp

LOCAL_SRC_FILES         += ../common/src/extra_data_handler.cpp
LOCAL_SRC_FILES         += ../common/src/vidc_color_converter.cpp
LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_32_BIT_ONLY         := true

include $(BUILD_SHARED_LIBRARY)

endif
endif


endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                END
# ---------------------------------------------------------------------------------
