# ---------------------------------------------------------------------------------
#				MM-VIDEO-OSS-8K-VDEC
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
CPPFLAGS += -DPROFILE_DECODER
CPPFLAGS += -DUSE_PMEM_ADSP_CACHED
CPPFLAGS += -Du32="unsigned int"
CPPFLAGS += -Du8="unsigned char"
CPPFLAGS += -Iinc
CPPFLAGS += -Isrc

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
all: libOmxVdec.so.$(LIBVER) mm-vdec-omx-test

install:
	echo "installing opensource video decoder in $(DESTDIR)"
	if [ ! -d $(LIBINSTALLDIR) ]; then mkdir -p $(LIBINSTALLDIR); fi
	if [ ! -d $(BININSTALLDIR) ]; then mkdir -p $(BININSTALLDIR); fi
	install -m 555 libOmxVdec.so.$(LIBVER) $(LIBINSTALLDIR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVdec.so.$(LIBVER) libOmxVdec.so.$(LIBMAJOR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVdec.so.$(LIBMAJOR) libOmxVdec.so
	install -m 555 mm-vdec-omx-test $(BININSTALLDIR)

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------
SRCS := src/adsp.c
SRCS += src/pmem.c
SRCS += src/qutility.c
SRCS += src/vdec.cpp
SRCS += src/omx_vdec.cpp
SRCS += src/omx_vdec_linux.cpp
SRCS += src/omx_vdec_inpbuf.cpp
SRCS += src/MP4_Utils.cpp
SRCS += src/H264_Utils.cpp

LDLIBS := -lpthread
LDLIBS += -lrt
LDLIBS += -lstdc++
LDLIBS += -lOmxCore

libOmxVdec.so.$(LIBVER): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxVdec.so.$(LIBMAJOR) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------
TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lOmxCore

TEST_SRCS := test/omx_vdec_test.cpp
TEST_SRCS += test/queue.c

mm-vdec-omx-test: libOmxVdec.so.$(LIBVER) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
