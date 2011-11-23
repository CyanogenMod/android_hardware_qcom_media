# ---------------------------------------------------------------------------------
#				MM-VIDEO-OSS-VENC
# ---------------------------------------------------------------------------------

# cross-compiler flags
CFLAGS += -Wall
CFLAGS += -Wundef
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wno-trigraphs
#CFLAGS += -Werror

# cross-compile flags specific to shared objects
CFLAGS_SO += -fpic

# required pre-processor flags
CPPFLAGS += -D__packed__=
CPPFLAGS += -DIMAGE_APPS_PROC
CPPFLAGS += -DFEATURE_Q_SINGLE_LINK
CPPFLAGS += -DFEATURE_Q_NO_SELF_QPTR
CPPFLAGS += -DFEATURE_LINUX
CPPFLAGS += -DFEATURE_NATIVELINUX
CPPFLAGS += -DFEATURE_DSM_DUP_ITEMS
CPPFLAGS += -UENABLE_DEBUG_LOW
CPPFLAGS += -DENABLE_DEBUG_HIGH
CPPFLAGS += -DENABLE_DEBUG_ERROR
CPPFLAGS += -UMULTI_DEC_INST
CPPFLAGS += -UINPUT_BUFFER_LOG
CPPFLAGS += -UOUTPUT_BUFFER_LOG
CPPFLAGS += -DMAX_RES_1080P
CPPFLAGS += -Du32="unsigned int"
CPPFLAGS += -Du8="unsigned char"
CPPFLAGS += -Iinc
CPPFLAGS += -I../common/inc
CPPFLAGS += -Isrc
CPPFLAGS += -I../common/src

# linker flags
LDFLAGS += -L$(SYSROOT)/usr/lib

# linker flags for shared objects
LDFLAGS_SO += -shared

# defintions
LIBMAJOR := $(basename $(basename $(LIBVER)))
LIBINSTALLDIR := $(DESTDIR)usr/lib
BININSTALLDIR := $(DESTDIR)usr/bin

# ---------------------------------------------------------------------------------
#					BUILD
# ---------------------------------------------------------------------------------
all: libOmxVenc.so.$(LIBVER) mm-venc-omx-test

install:
	echo "installing opensource video encoder in $(DESTDIR)"
	if [ ! -d $(LIBINSTALLDIR) ]; then mkdir -p $(LIBINSTALLDIR); fi
	if [ ! -d $(BININSTALLDIR) ]; then mkdir -p $(BININSTALLDIR); fi
	install -m 555 libOmxVenc.so.$(LIBVER) $(LIBINSTALLDIR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVenc.so.$(LIBVER) libOmxVenc.so.$(LIBMAJOR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVenc.so.$(LIBMAJOR) libOmxVenc.so
	install -m 555 mm-venc-omx-test $(BININSTALLDIR)

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------

SRCS := src/omx_video_base.cpp
SRCS += src/omx_video_encoder.cpp
SRCS += src/video_encoder_device.cpp
SRCS += ../common/src/extra_data_handler.cpp

LDLIBS := -lpthread
LDLIBS += -lrt
LDLIBS += -lstdc++
LDLIBS += -lOmxCore

libOmxVenc.so.$(LIBVER): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxVenc.so.$(LIBMAJOR) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------
TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lOmxCore

TEST_SRCS := test/venc_test.cpp
TEST_SRCS += test/camera_test.cpp
TEST_SRCS += test/venc_util.c
TEST_SRCS += test/fb_test.c

mm-venc-omx-test: libOmxVenc.so.$(LIBVER) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
