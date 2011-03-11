# ---------------------------------------------------------------------------------
#				MM-CORE-OSS-OMXCORE
# ---------------------------------------------------------------------------------

# Source Path
VDEC_SRC := $(SRCDIR)/vidc/vdec

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

all: libOmxVdec.so mm-vdec-omx-test mm-video-driver-test

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------

SRCS := $(VDEC_SRC)/src/frameparser.cpp
SRCS += $(VDEC_SRC)/src/h264_utils.cpp
SRCS += $(VDEC_SRC)/src/mp4_utils.cpp
SRCS += $(VDEC_SRC)/src/omx_vdec.cpp

CPPFLAGS += -I$(VDEC_SRC)/inc
CPPFLAGS += -I$(SYSROOTINC_DIR)/mm-core
CPPFLAGS += -I$(KERNEL_DIR)/include
CPPFLAGS += -I$(KERNEL_DIR)/arch/arm/include

CPPFLAGS += -UENABLE_DEBUG_LOW
CPPFLAGS += -DENABLE_DEBUG_HIGH
CPPFLAGS += -DENABLE_DEBUG_ERROR
CPPFLAGS += -UINPUT_BUFFER_LOG
CPPFLAGS += -UOUTPUT_BUFFER_LOG
CPPFLAGS += -DMAX_RES_720P

LDLIBS := -lrt
LDLIBS += -lpthread

libOmxVdec.so:$(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxVdec.so -o $@ $^ $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------

TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lstdc++
TEST_LDLIBS += -lOmxCore

SRCS := $(VDEC_SRC)/src/queue.c
SRCS += $(VDEC_SRC)/test/omx_vdec_test.cpp

mm-vdec-omx-test: libOmxVdec.so $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------

TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lstdc++
TEST_LDLIBS += -lOmxCore

SRCS := $(VDEC_SRC)/src/message_queue.c
SRCS += $(VDEC_SRC)/test/decoder_driver_test.c

mm-video-driver-test: libOmxVdec.so $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
