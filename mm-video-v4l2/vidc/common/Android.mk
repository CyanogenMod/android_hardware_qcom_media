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

# Common Dependencies
libmm-vidc-add-dep  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr


# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVidcCommon)
# ---------------------------------------------------------------------------------

libmm-vidc-inc      := $(LOCAL_PATH)/inc
libmm-vidc-inc      += $(TOP)/$(call project-path-for,qcom-media)/mm-core/inc
libmm-vidc-inc      += $(TARGET_OUT_HEADERS)/qcom/display
libmm-vidc-inc      += $(TOP)/$(call project-path-for,qcom-media)/libc2dcolorconvert
libmm-vidc-inc      += $(TOP)/frameworks/av/include/media/stagefright
libmm-vidc-inc      += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_ADDITIONAL_DEPENDENCIES   := $(libmm-vidc-add-dep)

LOCAL_MODULE                    := libOmxVidcCommon
LOCAL_MODULE_TAGS               := optional
LOCAL_CFLAGS                    := $(libmm-vidc-def)
LOCAL_C_INCLUDES                := $(libmm-vidc-inc)

LOCAL_PRELINK_MODULE      := false
LOCAL_SHARED_LIBRARIES    := liblog libutils libcutils libdl

LOCAL_SRC_FILES   := src/extra_data_handler.cpp
LOCAL_SRC_FILES   += src/vidc_color_converter.cpp

include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------------------------------------------------
# 					END
# ---------------------------------------------------------------------------------
