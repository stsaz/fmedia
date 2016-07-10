# fmedia v0.14 makefile

PROJ := fmedia
ROOT := ..
PROJDIR := $(ROOT)/fmedia
SRCDIR := $(PROJDIR)/src
VER :=
OS :=
OPT := LTO

FFOS := $(ROOT)/ffos
FF := $(ROOT)/ff
FF3PT := $(ROOT)/ff-3pt

include $(FFOS)/makeconf

ifeq ($(OS),win)
BIN := fmedia.exe
INSTDIR := fmedia
else
BIN := fmedia-bin
INSTDIR := fmedia-0
endif

FF_OBJ_DIR := ./ff-obj
FF_CFLAGS := $(CFLAGS)
FF3PTLIB := $(FF3PT)/$(OS)-$(ARCH)
FF3PT_CFLAGS := $(CFLAGS)

CFLAGS += \
	-Werror -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-I$(SRCDIR) -I$(FF) -I$(FFOS) -I$(FF3PT)
LDFLAGS += -L$(FF3PTLIB) $(LD_LWS2_32)

include $(PROJDIR)/makerules

package: install
	rm -f $(PROJ)-$(VER)-$(OS)-$(ARCH).$(PACK_EXT)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH)-debug.$(PACK_EXT) ./*.debug
