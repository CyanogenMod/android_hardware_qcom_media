# ---------------------------------------------------------------------------------
#				MM-CORE-OSS-OMXCORE
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

#CPPFLAGS += -D_ENABLE_QC_MSG_LOG_
CPPFLAGS += -Iinc
CPPFLAGS += -Isrc/common

# linker flags for shared objects
LDFLAGS_SO += -shared

# defintions
LIBMAJOR := $(basename $(basename $(LIBVER)))
LIBINSTALLDIR := $(DESTDIR)usr/lib
INCINSTALLDIR := $(DESTDIR)usr/include

# ---------------------------------------------------------------------------------
#					BUILD
# ---------------------------------------------------------------------------------

all: libOmxCore.so.$(LIBVER)

install:
	echo "intalling omxcore in $(DESTDIR)"
	if [ ! -d $(LIBINSTALLDIR) ]; then mkdir -p $(LIBINSTALLDIR); fi
	if [ ! -d $(INCINSTALLDIR) ]; then mkdir -p $(INCINSTALLDIR); fi
	install -m 555 libOmxCore.so.$(LIBVER) $(LIBINSTALLDIR)
	cd $(LIBINSTALLDIR) && ln -s libOmxCore.so.$(LIBVER) libOmxCore.so.$(LIBMAJOR)
	cd $(LIBINSTALLDIR) && ln -s libOmxCore.so.$(LIBMAJOR) libOmxCore.so
	install -m 644 inc/*.h $(INCINSTALLDIR)

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------

SRCS := src/common/qc_omx_core.c
SRCS += src/8x50A/qc_registry_table.c
SRCS += src/common/omx_core_cmp.cpp

LDLIBS := -lrt
LDLIBS += -lpthread

libOmxCore.so.$(LIBVER): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxCore.so.$(LIBMAJOR) -o $@ $^ $(LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
