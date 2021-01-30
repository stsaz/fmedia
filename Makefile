# fmedia v1.22 makefile

PROJ := fmedia
VER :=
OS :=
OPT := LTO3

# repositories
ROOT := ..
PROJDIR := $(ROOT)/fmedia
SRCDIR := $(PROJDIR)/src
FFBASE := $(ROOT)/ffbase
FFAUDIO := $(ROOT)/ffaudio
AVPACK := $(ROOT)/avpack
FFOS := $(ROOT)/ffos
FF := $(ROOT)/ff
FF3PT := $(ROOT)/ff-3pt

include $(FFOS)/makeconf


# OS-specific options
ifeq ($(OS),win)
BIN := fmedia.exe
INSTDIR := fmedia
CFLAGS_OS += -DFF_WIN_APIVER=0x0501

else
BIN := fmedia
INSTDIR := fmedia-1
ifeq ($(OS),linux)
CFLAGS_OS += -DFF_GLIBCVER=228
endif
endif


FF_OBJ_DIR := ./ff-obj
# CFLAGS_STD += -fsanitize=address
# LDFLAGS += -fsanitize=address -ldl
ifeq ($(OPT),0)
	CFLAGS_OPT += -DFF_DEBUG
endif
FFAUDIO_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -I$(FFBASE) -I$(FFAUDIO)
CFLAGS_STD += -DFFBASE_HAVE_FFERR_STR
FFOS_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -pthread
FF_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU)
FF3PTLIB := $(FF3PT)-bin/$(OS)-$(ARCH)
FF3PT_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU)


# CPU-specific options
ifeq ($(CPU),i686)
CFLAGS_CPU += -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
endif


CFLAGS_APP := \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-implicit-fallthrough \
	-Wno-stringop-overflow \
	-I$(SRCDIR) -I$(FFBASE) -I$(FFAUDIO) -I$(AVPACK) -I$(FF) -I$(FFOS) -I$(FF3PT)
CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) $(CFLAGS_APP)
# alternative optimization flags: no LTO
ifneq ($(OPT),0)
	CFLAGS_OPT := -O3
endif
CFLAGS_ALTOPT := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) $(CFLAGS_APP)
LDFLAGS += -Wno-stringop-overflow -L$(FF3PTLIB) $(LD_LPTHREAD)
ifeq ($(OS),bsd)
	LDFLAGS += -lexecinfo
endif

include $(PROJDIR)/makerules

package:
	rm -f $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS)-debug.$(PACK_EXT) ./*.debug

post-build:
	# ensure we use no GLIBC_2.29 functions
	! objdump -T *.so fmedia | grep GLIBC_2.29
