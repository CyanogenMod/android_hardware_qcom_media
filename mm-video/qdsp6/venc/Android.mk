ifneq ($(BUILD_TINY_ANDROID),true)

ROOT_DIR := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
# 				Common definitons
# ---------------------------------------------------------------------------------

libmm-venc-def := -DVENC_MSG_ERROR_ENABLE
libmm-venc-def += -DVENC_MSG_FATAL_ENABLE
libmm-venc-def += -DQCOM_OMX_VENC_EXT
libmm-venc-def += -O3
libmm-venc-def += -D_ANDROID_LOG_
libmm-venc-def += -D_ANDROID_LOG_ERROR
libmm-venc-def += -D_ANDROID_LOG_PROFILE
libmm-venc-def += -Du32="unsigned int"

# ---------------------------------------------------------------------------------
# 			Make the Shared library (libOmxVidEnc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libmm-venc-inc += $(LOCAL_PATH)/omx/inc
libmm-venc-inc += $(LOCAL_PATH)/device/inc
libmm-venc-inc += $(LOCAL_PATH)/common/inc
libmm-venc-inc += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE := libOmxVidEnc
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := $(libmm-venc-def)
LOCAL_C_INCLUDES := $(libmm-venc-inc)
LOCAL_PRELINK_MODULE := false
LOCAL_SHARED_LIBRARIES := liblog libutils

LOCAL_SRC_FILES	:= omx/src/OMX_Venc.cpp
LOCAL_SRC_FILES	+= omx/src/OMX_VencBufferManager.cpp
LOCAL_SRC_FILES	+= device/src/venc_device.c

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
# 			Make the apps-test (mm-venc-omx-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-venc-test-inc := $(LOCAL_PATH)/src
mm-venc-test-inc += $(LOCAL_PATH)/test
mm-venc-test-inc += $(LOCAL_PATH)/test/common/inc
mm-venc-test-inc += $(LOCAL_PATH)/common/inc
mm-venc-test-inc += $(LOCAL_PATH)/omx/inc
mm-venc-test-inc += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE := mm-venc-omx-test
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := $(libmm-venc-def)
LOCAL_C_INCLUDES := $(mm-venc-test-inc)
LOCAL_PRELINK_MODULE := false
LOCAL_SHARED_LIBRARIES := libdl libOmxCore libOmxVidEnc

LOCAL_SRC_FILES	:= test/app/src/venctest_App.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Config.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Pmem.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Encoder.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Script.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_File.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_FileSink.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Mutex.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Parser.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_FileSource.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Queue.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Signal.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_SignalQueue.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Sleeper.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Thread.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_Time.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_StatsThread.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestCaseFactory.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_ITestCase.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestEncode.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestIFrameRequest.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestGetSyntaxHdr.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestEOS.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestChangeQuality.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestChangeIntraPeriod.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestStateExecutingToIdle.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestStatePause.cpp
LOCAL_SRC_FILES	+= test/common/src/venctest_TestFlush.cpp
LOCAL_SRC_FILES	+= common/src/venc_queue.c
LOCAL_SRC_FILES	+= common/src/venc_semaphore.c
LOCAL_SRC_FILES	+= common/src/venc_mutex.c
LOCAL_SRC_FILES	+= common/src/venc_time.c
LOCAL_SRC_FILES	+= common/src/venc_sleep.c
LOCAL_SRC_FILES	+= common/src/venc_signal.c
LOCAL_SRC_FILES	+= common/src/venc_file.c
LOCAL_SRC_FILES	+= common/src/venc_thread.c

include $(BUILD_EXECUTABLE)

endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
# 					END
# ---------------------------------------------------------------------------------

