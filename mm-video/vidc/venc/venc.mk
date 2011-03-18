# ---------------------------------------------------------------------------------
#				MM-CORE-OSS-OMXCORE
# ---------------------------------------------------------------------------------

# Source Path
VENC_SRC := $(SRCDIR)/vidc/venc

# cross-compiler flags
CFLAGS := -Wall 
CFLAGS += -Wundef 
CFLAGS += -Wstrict-prototypes 
CFLAGS += -Wno-trigraphs 

# cross-compile flags specific to shared objects
CFLAGS_SO := $(QCT_CFLAGS_SO)

# Preproc flags
CPPFLAGS := $(QCT_CPPFLAGS)

# linker flags for shared objects
LDFLAGS_SO += -shared

# linker flags
LDFLAGS := -L$(SYSROOTLIB_DIR)

# hard coding target for 7630
TARGET := 7630

# ---------------------------------------------------------------------------------
#					BUILD
# ---------------------------------------------------------------------------------

all: libOmxVenc.so mm-venc-omx-test720p mm-video-encdrv-test

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------

SRCS := $(VENC_SRC)/src/omx_video_base.cpp
SRCS += $(VENC_SRC)/src/omx_video_encoder.cpp
SRCS += $(VENC_SRC)/src/video_encoder_device.cpp

CPPFLAGS += -I$(VENC_SRC)/inc
CPPFLAGS += -I$(SYSROOTINC_DIR)/mm-core
CPPFLAGS += -I$(KERNEL_DIR)/include
CPPFLAGS += -I$(KERNEL_DIR)/arch/arm/include

CPPFLAGS += -UENABLE_DEBUG_LOW
CPPFLAGS += -DENABLE_DEBUG_HIGH
CPPFLAGS += -DENABLE_DEBUG_ERROR
CPPFLAGS += -UINPUT_BUFFER_LOG
CPPFLAGS += -UOUTPUT_BUFFER_LOG
CPPFLAGS += -USINGLE_ENCODER_INSTANCE

LDLIBS := -lrt
LDLIBS += -lpthread

libOmxVenc.so:$(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxVenc.so -o $@ $^ $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------

TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lstdc++
TEST_LDLIBS += -lOmxCore

SRCS := $(VENC_SRC)/test/venc_test.cpp
SRCS += $(VENC_SRC)/test/camera_test.cpp
SRCS += $(VENC_SRC)/test/venc_util.c
SRCS += $(VENC_SRC)/test/fb_test.c

mm-venc-omx-test720p: libOmxVenc.so $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------

TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lstdc++
TEST_LDLIBS += -lOmxCore

SRCS := $(VENC_SRC)/test/video_encoder_test.c
SRCS += $(VENC_SRC)/test/queue.c

mm-video-encdrv-test: libOmxVenc.so $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
