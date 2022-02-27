# fmedia v1.22 makefile

# Requirements:
# make gcc cp objcopy strip touch rm mkdir chmod
# Linux: tar
# Windows: zip unix2dos
# macOS: zip

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

include $(FFOS)/makeconf


# OS-specific options
ifeq ($(OS),win)
BIN := fmedia.exe
INSTDIR := fmedia
CFLAGS_OS += -DFF_WIN_APIVER=0x0501

else
BIN := fmedia
INSTDIR := fmedia-1
endif

ALIB3 := $(PROJDIR)/alib3/_$(OSFULL)-$(ARCH)

FF_OBJ_DIR := ./ff-obj
# CFLAGS_STD += -fsanitize=address
# LDFLAGS += -fsanitize=address -ldl
ifeq ($(OPT),0)
	CFLAGS_OPT += -DFF_DEBUG
endif
FFAUDIO_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -I$(FFBASE) -I$(FFAUDIO)
CFLAGS_STD += -DFFBASE_HAVE_FFERR_STR
FFOS_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -pthread
FF_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -I$(AVPACK)


# CPU-specific options
ifeq "$(CPU)" "amd64"
	CFLAGS_CPU += -msse4.2
else ifeq "$(CPU)" "i686"
	CFLAGS_CPU += -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
endif


CFLAGS_APP := \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-implicit-fallthrough \
	-Wno-stringop-overflow \
	-I$(SRCDIR) -I$(PROJDIR)/alib3 -I$(FFBASE) -I$(FFAUDIO) -I$(AVPACK) -I$(FF) -I$(FFOS)
CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) $(CFLAGS_APP)
ifneq ($(OPT),0)
	CFLAGS_OPT := -O3
endif
LDFLAGS += -Wno-stringop-overflow $(LD_LPTHREAD) -L$(ALIB3)
ifeq ($(OS),linux)
	LDFLAGS += -L/usr/lib64/pipewire-0.3/jack
endif
ifeq ($(OS),bsd)
	LDFLAGS += -lexecinfo
endif

include $(PROJDIR)/makerules

package:
	rm -f $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS)-debug.$(PACK_EXT) ./*.debug
