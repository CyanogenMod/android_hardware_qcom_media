ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libmm-vidc-def := -g -O3 -Dlrintf=_ffix_r
libmm-vidc-def += -D__align=__alignx
libmm-vidc-def += -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
libmm-vidc-def += -DT_ARM
libmm-vidc-def += -Dinline=__inline
libmm-vidc-def += -D_ANDROID_
libmm-vidc-def += -Werror
libmm-vidc-def += -D_ANDROID_ICS_

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVidcCommon)
# ---------------------------------------------------------------------------------

libmm-vidc-inc      := $(LOCAL_PATH)/inc
libmm-vidc-inc      += $(QCOM_MEDIA_ROOT)/mm-core/inc
libmm-vidc-inc      += $(TARGET_OUT_HEADERS)/qcom/display
libmm-vidc-inc      += $(QCOM_MEDIA_ROOT)/libc2dcolorconvert
libmm-vidc-inc      += $(TOP)/frameworks/av/include/media/stagefright
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
libmm-vidc-inc      += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif

LOCAL_MODULE                    := libOmxVidcCommon
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libmm-vidc-def)
LOCAL_C_INCLUDES                := $(libmm-vidc-inc)

LOCAL_PRELINK_MODULE      := false
LOCAL_SHARED_LIBRARIES    := liblog libutils libcutils libdl

LOCAL_SRC_FILES   := src/extra_data_handler.cpp
LOCAL_SRC_FILES   += src/vidc_color_converter.cpp

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------------------------------------------------
# 					END
# ---------------------------------------------------------------------------------
