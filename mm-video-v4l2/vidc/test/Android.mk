LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := msm-vidc-test
LOCAL_PRELINK_MODULE          := false
LOCAL_C_INCLUDES              := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_SRC_FILES               := msm_vidc_test.c
LOCAL_SRC_FILES               += queue.c
LOCAL_SRC_FILES               += ring_queue.c
LOCAL_CFLAGS                  := -DLOG_TAG=\"MSM-VIDC-TEST\"
LOCAL_MODULE_TAGS             := optional
LOCAL_32_BIT_ONLY             := true
include $(BUILD_EXECUTABLE)
