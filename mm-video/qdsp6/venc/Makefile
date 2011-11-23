# ---------------------------------------------------------------------------------
#				MM-VIDEO-OSS-8K-VENC
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

# venc specific defines
CPPFLAGS += -DPROFILE_DECODER
CPPFLAGS += -DVENC_MSG_ERROR_ENABLE
CPPFLAGS += -DVENC_MSG_FATAL_ENABLE
CPPFLAGS += -DQCOM_OMX_VENC_EXT
CPPFLAGS += -Du32="unsigned int"

CPPFLAGS += -Iinc
CPPFLAGS += -Icommon/inc
CPPFLAGS += -Idevice/inc
CPPFLAGS += -Iomx/inc
CPPFLAGS += -Itest/common/inc

# linker flags
LDFLAGS += -L$(SYSROOT)/usr/lib

# linker flags for shared objects
LDFLAGS_SO += -shared

MM_VENC_OMX_LDLIBS := -lpthread
MM_VENC_OMX_LDLIBS += -lrt
MM_VENC_OMX_LDLIBS += -lstdc++
MM_VENC_OMX_LDLIBS += -lOmxCore

LDLIBS += $(MM_VENC_OMX_LDLIBS)

SRCS := omx/src/OMX_Venc.cpp
SRCS += omx/src/OMX_VencBufferManager.cpp
SRCS += device/src/venc_device.c

# defintions
LIBMAJOR := $(basename $(basename $(LIBVER)))
LIBINSTALLDIR := $(DESTDIR)usr/lib
BININSTALLDIR := $(DESTDIR)usr/bin

# ---------------------------------------------------------------------------------
#					BUILD
# ---------------------------------------------------------------------------------
all: libOmxVidEnc.so.$(LIBVER) mm-venc-omx-test

install:
	echo "installing opensource video encoder libs in $(DESTDIR)"
	if [ ! -d $(LIBINSTALLDIR) ]; then mkdir -p $(LIBINSTALLDIR); fi
	if [ ! -d $(BININSTALLDIR) ]; then mkdir -p $(BININSTALLDIR); fi
	install -m 555 libOmxVidEnc.so.$(LIBVER) $(LIBINSTALLDIR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVidEnc.so.$(LIBVER) libOmxVidEnc.so.$(LIBMAJOR)
	cd $(LIBINSTALLDIR) && ln -s libOmxVidEnc.so.$(LIBMAJOR) libOmxVidEnc.so
	install -m 555 mm-venc-omx-test $(BININSTALLDIR)

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------
LDLIBS := -lpthread
LDLIBS += -lrt
LDLIBS += -lstdc++
LDLIBS += -lOmxCore

SRCS := omx/src/OMX_Venc.cpp
SRCS += omx/src/OMX_VencBufferManager.cpp
SRCS += device/src/venc_device.c

libOmxVidEnc.so.$(LIBVER): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxVidEnc.so.$(LIBMAJOR) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------
TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lOmxCore

TEST_SRCS	:= test/app/src/venctest_App.cpp
TEST_SRCS	+= test/common/src/venctest_Config.cpp
TEST_SRCS	+= test/common/src/venctest_Pmem.cpp
TEST_SRCS	+= test/common/src/venctest_Encoder.cpp
TEST_SRCS	+= test/common/src/venctest_Script.cpp
TEST_SRCS	+= test/common/src/venctest_File.cpp
TEST_SRCS	+= test/common/src/venctest_FileSink.cpp
TEST_SRCS	+= test/common/src/venctest_Mutex.cpp
TEST_SRCS	+= test/common/src/venctest_Parser.cpp
TEST_SRCS	+= test/common/src/venctest_FileSource.cpp
TEST_SRCS	+= test/common/src/venctest_Queue.cpp
TEST_SRCS	+= test/common/src/venctest_Signal.cpp
TEST_SRCS	+= test/common/src/venctest_SignalQueue.cpp
TEST_SRCS	+= test/common/src/venctest_Sleeper.cpp
TEST_SRCS	+= test/common/src/venctest_Thread.cpp
TEST_SRCS	+= test/common/src/venctest_Time.cpp
TEST_SRCS	+= test/common/src/venctest_StatsThread.cpp
TEST_SRCS	+= test/common/src/venctest_TestCaseFactory.cpp
TEST_SRCS	+= test/common/src/venctest_ITestCase.cpp
TEST_SRCS	+= test/common/src/venctest_TestEncode.cpp
TEST_SRCS	+= test/common/src/venctest_TestIFrameRequest.cpp
TEST_SRCS	+= test/common/src/venctest_TestGetSyntaxHdr.cpp
TEST_SRCS	+= test/common/src/venctest_TestEOS.cpp
TEST_SRCS	+= test/common/src/venctest_TestChangeQuality.cpp
TEST_SRCS	+= test/common/src/venctest_TestChangeIntraPeriod.cpp
TEST_SRCS	+= test/common/src/venctest_TestStateExecutingToIdle.cpp
TEST_SRCS	+= test/common/src/venctest_TestStatePause.cpp
TEST_SRCS	+= test/common/src/venctest_TestFlush.cpp
TEST_SRCS	+= common/src/venc_queue.c
TEST_SRCS	+= common/src/venc_semaphore.c
TEST_SRCS	+= common/src/venc_mutex.c
TEST_SRCS	+= common/src/venc_time.c
TEST_SRCS	+= common/src/venc_sleep.c
TEST_SRCS	+= common/src/venc_signal.c
TEST_SRCS	+= common/src/venc_file.c
TEST_SRCS	+= common/src/venc_thread.c

mm-venc-omx-test: libOmxVidEnc.so.$(LIBVER) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
