#--------------------------------------------------------------------------
#Copyright (c) 2009, Code Aurora Forum. All rights reserved.

#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of Code Aurora nor
#      the names of its contributors may be used to endorse or promote
#      products derived from this software without specific prior written
#      permission.

#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
#OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
#WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
#OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#--------------------------------------------------------------------------

ifneq ($(BUILD_TINY_ANDROID),true)

ROOT_DIR := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PATH:= $(ROOT_DIR)

# ---------------------------------------------------------------------------------
#             Common definitons
# ---------------------------------------------------------------------------------

libOmxVdec-def := -g -O3
libOmxVdec-def += -DQC_MODIFIED
libOmxVdec-def += -DVERBOSE
libOmxVdec-def += -D_DEBUG
libOmxVdec-def += -DUSE_LOGE_FOR_DECODER
libOmxVdec-def += -D_ANDROID_
libOmxVdec-def += -DPROFILE_DECODER
libOmxVdec-def += -Du32="unsigned int"
libOmxVdec-def += -Du8="unsigned char"

# ---------------------------------------------------------------------------------
#          Make the Shared library (libOmxVdec)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxVdec-inc          := $(LOCAL_PATH)/src
libOmxVdec-inc          += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := libOmxVdec
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxVdec-def)
LOCAL_C_INCLUDES        := $(libOmxVdec-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libutils liblog libbinder libcutils

LOCAL_SRC_FILES         := src/adsp.c
LOCAL_SRC_FILES         += src/pmem.c
LOCAL_SRC_FILES         += src/qutility.c
LOCAL_SRC_FILES         += src/vdec.cpp
LOCAL_SRC_FILES         += src/omx_vdec.cpp
LOCAL_SRC_FILES         += src/omx_vdec_linux.cpp
LOCAL_SRC_FILES         += src/omx_vdec_inpbuf.cpp
LOCAL_SRC_FILES         += src/MP4_Utils.cpp
LOCAL_SRC_FILES         += src/H264_Utils.cpp

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
#          Make the apps-test (mm-vdec-omx-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-vdec-test-inc        := $(LOCAL_PATH)/src
mm-vdec-test-inc        += $(LOCAL_PATH)/test
mm-vdec-test-inc        += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := mm-vdec-omx-test
LOCAL_MODULE_TAGS       := tests
LOCAL_CFLAGS            := $(libOmxVdec-def)
LOCAL_C_INCLUDES        := $(mm-vdec-test-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libmm-omxcore libOmxVdec libbinder libutils

LOCAL_SRC_FILES         := test/omx_vdec_test.cpp
LOCAL_SRC_FILES         += test/queue.c

include $(BUILD_EXECUTABLE)

# ---------------------------------------------------------------------------------
#          Make the vdec-property-mgr (mm-vdec-omx-property-mgr)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-vdec-property-mgr-inc        := $(LOCAL_PATH)

LOCAL_MODULE            := mm-vdec-omx-property-mgr
LOCAL_MODULE_TAGS       := tests
LOCAL_CFLAGS            := $(libOmxVdec-def)
LOCAL_C_INCLUDES        := $(mm-vdec-property-mgr-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils

LOCAL_SRC_FILES         := test/omx_vdec_property_mgr.cpp

include $(BUILD_EXECUTABLE)

endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                END
# ---------------------------------------------------------------------------------

