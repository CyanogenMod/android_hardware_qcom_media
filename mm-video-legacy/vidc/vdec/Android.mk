ifneq ($(BUILD_TINY_ANDROID),true)

ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libOmxVdec-def += -D__align=__alignx
libOmxVdec-def += -Dinline=__inline
libOmxVdec-def += -DIMAGE_APPS_PROC
libOmxVdec-def += -D_ANDROID_
libOmxVdec-def += -DCDECL
libOmxVdec-def += -DT_ARM
libOmxVdec-def += -DNO_ARM_CLZ
libOmxVdec-def += -UENABLE_DEBUG_LOW
libOmxVdec-def += -DENABLE_DEBUG_HIGH
libOmxVdec-def += -DENABLE_DEBUG_ERROR
libOmxVdec-def += -UINPUT_BUFFER_LOG
libOmxVdec-def += -UOUTPUT_BUFFER_LOG
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -DTEST_TS_FROM_SEI
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
libOmxVdec-def += -DMAX_RES_1080P
libOmxVdec-def += -DMAX_RES_1080P_EBI
libOmxVdec-def += -DPROCESS_EXTRADATA_IN_OUTPUT_PORT
libOmxVdec-def += -D_COPPER_
endif

libOmxVdec-def += -D_ANDROID_ICS_

#ifeq ($(TARGET_USES_ION),true)
libOmxVdec-def += -DUSE_ION
#endif

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVdec)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

ifneq ($(TARGET_QCOM_DISPLAY_VARIANT),)
DISPLAY := display-$(TARGET_QCOM_DISPLAY_VARIANT)
libOmxVdec-def += -DDISPLAYCAF
else
DISPLAY := display/$(TARGET_BOARD_PLATFORM)
endif

libmm-vdec-inc          := bionic/libc/include
libmm-vdec-inc          += bionic/libstdc++/include
libmm-vdec-inc          += $(LOCAL_PATH)/inc
libmm-vdec-inc          += $(OMX_VIDEO_PATH)/vidc/common/inc
libmm-vdec-inc          += hardware/qcom/media/mm-core/inc
libmm-vdec-inc          += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
#DRM include - Interface which loads the DRM library
libmm-vdec-inc	        += $(OMX_VIDEO_PATH)/DivxDrmDecrypt/inc
libmm-vdec-inc          += hardware/qcom/$(DISPLAY)/libgralloc
libmm-vdec-inc          += frameworks/native/include/media/openmax
libmm-vdec-inc          += frameworks/native/include/media/hardware
libmm-vdec-inc          += hardware/qcom/media/libc2dcolorconvert
libmm-vdec-inc          += hardware/qcom/$(DISPLAY)/libcopybit
libmm-vdec-inc          += frameworks/av/include/media/stagefright
libmm-vdec-inc          += hardware/qcom/$(DISPLAY)/libqservice
libmm-vdec-inc          += hardware/qcom/$(DISPLAY)/libqdutils

LOCAL_MODULE                    := libOmxVdec
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdec-def)
LOCAL_C_INCLUDES                += $(libmm-vdec-inc)

LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := liblog libutils libbinder libcutils libdl

LOCAL_SHARED_LIBRARIES  += libdivxdrmdecrypt
LOCAL_SHARED_LIBRARIES += libqservice
LOCAL_SHARED_LIBRARIES += libqdMetaData

LOCAL_SRC_FILES         := src/frameparser.cpp
LOCAL_SRC_FILES         += src/h264_utils.cpp
LOCAL_SRC_FILES         += src/ts_parser.cpp
LOCAL_SRC_FILES         += src/mp4_utils.cpp
LOCAL_SRC_FILES         += src/omx_vdec.cpp
LOCAL_SRC_FILES         += ../common/src/extra_data_handler.cpp
LOCAL_SRC_FILES         += ../common/src/vidc_color_converter.cpp

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
# 			Make the apps-test (mm-vdec-omx-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-test-inc    := hardware/qcom/media/mm-core/inc
mm-vdec-test-inc    += $(LOCAL_PATH)/inc
mm-vdec-test-inc    += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_MODULE                    := mm-vdec-omx-test
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdec-def)
LOCAL_C_INCLUDES                := $(mm-vdec-test-inc)

LOCAL_PRELINK_MODULE      := false
LOCAL_SHARED_LIBRARIES    := libutils liblog libOmxCore libOmxVdec libbinder

LOCAL_SRC_FILES           := src/queue.c
LOCAL_SRC_FILES           += test/omx_vdec_test.cpp

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_EXECUTABLE)

# ---------------------------------------------------------------------------------
# 			Make the driver-test (mm-video-driver-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-drv-test-inc    := hardware/qcom/media/mm-core/inc
mm-vdec-drv-test-inc    += $(LOCAL_PATH)/inc
mm-vdec-drv-test-inc    += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_MODULE                    := mm-video-driver-test
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdec-def)
LOCAL_C_INCLUDES                := $(mm-vdec-drv-test-inc)
LOCAL_PRELINK_MODULE            := false

LOCAL_SRC_FILES                 := src/message_queue.c
LOCAL_SRC_FILES                 += test/decoder_driver_test.c

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_EXECUTABLE)

endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                END
# ---------------------------------------------------------------------------------
