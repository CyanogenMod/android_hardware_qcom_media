#
# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    QComOMXPlugin.cpp                      \

LOCAL_CFLAGS := $(PV_CFLAGS_MINUS_VISIBILITY)

ifneq ($(call is-platform-sdk-version-at-least,19),true)
LOCAL_CFLAGS += -DMETADATA_FOR_DYNAMIC_MODE
endif

ifeq ($(TARGET_USES_MEDIA_EXTENSIONS),true)
LOCAL_CFLAGS += -DUSE_NATIVE_HANDLE_SOURCE
endif

LOCAL_C_INCLUDES:= \
        frameworks/native/include/media/openmax \
        frameworks/native/include/media/hardware

LOCAL_SHARED_LIBRARIES :=       \
        libbinder               \
        libutils                \
        libcutils               \
        libdl                   \
        libui                   \

LOCAL_MODULE := libstagefrighthw

include $(BUILD_SHARED_LIBRARY)

