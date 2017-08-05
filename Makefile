# fmedia v0.14 makefile

PROJ := fmedia
ROOT := ..
PROJDIR := $(ROOT)/fmedia
SRCDIR := $(PROJDIR)/src
VER :=
OS :=
OPT := LTO3

FFOS := $(ROOT)/ffos
FF := $(ROOT)/ff
FF3PT := $(ROOT)/ff-3pt

include $(FFOS)/makeconf


# OS-specific options
ifeq ($(OS),win)
BIN := fmedia.exe
INSTDIR := fmedia
CFLAGS += -DFF_WIN=0x0502

else
BIN := fmedia-bin
INSTDIR := fmedia-0
endif


FF_OBJ_DIR := ./ff-obj
FFOS_CFLAGS := $(CFLAGS) -pthread
FF_CFLAGS := $(CFLAGS)
FF3PTLIB := $(FF3PT)-bin/$(OS)-$(ARCH)
FF3PT_CFLAGS := $(CFLAGS)


# CPU-specific options
ifeq ($(CPU),i686)
CFLAGS += -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
endif


CFLAGS += \
	-DFFS_FMT_NO_e \
	-Werror -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-I$(SRCDIR) -I$(FF) -I$(FFOS) -I$(FF3PT)
LDFLAGS += -L$(FF3PTLIB) $(LD_LWS2_32)

include $(PROJDIR)/makerules

package: install
	rm -f $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS)-debug.$(PACK_EXT) ./*.debug
