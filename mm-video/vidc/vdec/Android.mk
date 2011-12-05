ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

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
libOmxVdec-def += -DENABLE_DEBUG_HIGH
libOmxVdec-def += -DENABLE_DEBUG_ERROR
libOmxVdec-def += -UMULTI_DEC_INST
libOmxVdec-def += -DMAX_RES_720P

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVdec)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

libmm-vdec-inc	        := $(LOCAL_PATH)/inc
libmm-vdec-inc	        += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE		:= libOmxVdec
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES	:= $(libmm-vdec-inc)

LOCAL_SHARED_LIBRARIES	:= liblog libutils libbinder

LOCAL_SRC_FILES         := src/frameparser.cpp
LOCAL_SRC_FILES         += src/h264_utils.cpp
LOCAL_SRC_FILES         += src/omx_vdec.cpp

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
# 			Make the apps-test (mm-vdec-omx-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-test-inc		:= $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-vdec-test-inc		+= $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE			:= mm-vdec-omx-test
LOCAL_CFLAGS	  		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES  		:= $(mm-vdec-test-inc)
LOCAL_SHARED_LIBRARIES		:= libutils libOmxCore libOmxVdec libbinder

LOCAL_SRC_FILES                 := src/queue.c
LOCAL_SRC_FILES                 += test/omx_vdec_test.cpp

include $(BUILD_EXECUTABLE)

# ---------------------------------------------------------------------------------
# 			Make the driver-test (mm-video-driver-test)
# ---------------------------------------------------------------------------------
include $(CLEAR_VARS)

mm-vdec-drv-test-inc		:= $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-vdec-drv-test-inc		+= $(LOCAL_PATH)/inc

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE			:= mm-video-driver-test
LOCAL_CFLAGS	  		:= $(libOmxVdec-def)
LOCAL_C_INCLUDES  		:= $(mm-vdec-drv-test-inc)

LOCAL_SRC_FILES                 := src/message_queue.c
LOCAL_SRC_FILES                 += test/decoder_driver_test.c

include $(BUILD_EXECUTABLE)
