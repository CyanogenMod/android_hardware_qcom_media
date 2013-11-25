ifneq ($(BUILD_TINY_ANDROID),true)

ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
#                Common definitons
# ---------------------------------------------------------------------------------

libOmxVdpp-def := -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libOmxVdpp-def += -D__align=__alignx
libOmxVdpp-def += -Dinline=__inline
libOmxVdpp-def += -g -O3
libOmxVdpp-def += -DIMAGE_APPS_PROC
libOmxVdpp-def += -D_ANDROID_
libOmxVdpp-def += -DCDECL
libOmxVdpp-def += -DT_ARM
libOmxVdpp-def += -DNO_ARM_CLZ
libOmxVdpp-def += -UENABLE_DEBUG_LOW
libOmxVdpp-def += -DENABLE_DEBUG_HIGH
libOmxVdpp-def += -DENABLE_DEBUG_ERROR
libOmxVdpp-def += -D_ANDROID_ICS_
libOmxVdpp-def += -UINPUT_BUFFER_LOG
libOmxVdpp-def += -UOUTPUT_BUFFER_LOG

ifeq ($(TARGET_BOARD_PLATFORM),apq8084)
libOmxVdpp-def += -DMAX_RES_1080P
libOmxVdpp-def += -DMAX_RES_1080P_EBI
endif

ifeq ($(TARGET_USES_ION),true)
libOmxVdpp-def += -DUSE_ION
endif

vidpp-inc          = $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

# ---------------------------------------------------------------------------------
#			Make the Shared library (libOmxVdpp)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

libmm-vidpp-inc          += $(LOCAL_PATH)/inc
libmm-vidpp-inc          += $(OMX_VIDEO_PATH)/vidc/common/inc
libmm-vidpp-inc          += hardware/qcom/media/mm-core/inc
libmm-vidpp-inc          += hardware/qcom/display/libgralloc
libmm-vidpp-inc          += frameworks/native/include/media/openmax
libmm-vidpp-inc          += frameworks/native/include/media/hardware
libmm-vidpp-inc          += $(vidpp-inc)
libmm-vidpp-inc          += hardware/qcom/display/libqdutils
libmm-vidpp-inc          += hardware/qcom/display/libcopybit
libmm-vidpp-inc          += frameworks/av/include/media/stagefright

LOCAL_MODULE                    := libOmxVdpp
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libOmxVdpp-def)
LOCAL_C_INCLUDES                += $(libmm-vidpp-inc)

LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := liblog libutils libbinder libcutils libdl libc

LOCAL_SRC_FILES         += src/omx_vdpp.cpp

LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                END
# ---------------------------------------------------------------------------------
