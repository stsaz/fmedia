
OS := linux

PROJ := fmedia
PROJDIR := .
SRCDIR := $(PROJDIR)/src
ifeq ($(OS),win)
INSTDIR := ./$(PROJ)
else
INSTDIR := ./$(PROJ)-0
endif
VER :=

FFOS := ../ffos
FF := ../ff
FF_OBJ_DIR := ./ff-obj


OSTYPE := unix
ifeq ($(OS),win)
OSTYPE := wint
endif

# set initial values for:
# . linux/bsd/windows
# . gcc
# . packaging: tar.xz/zip
ifeq ($(OSTYPE),unix)

BIN := $(PROJ)-bin
TARGET := amd64
override ALL_CFLAGS += -fpic
LD_LMATH := -lm
SO := so
PACK_EXT := tar.xz

ifeq ($(OS),linux)

override ALL_CFLAGS += -DFF_OLDLIBC
LD_LDL := -ldl
CP := cp -u -v -p
PACK := tar --owner=0 --group=0 --numeric-owner -cJv -f

else #bsd:

CP := cp -v
PACK := tar --uid=0 --gid=0 --numeric-owner -cJv -f

endif

else #windows:

BIN := $(PROJ).exe
TARGET := x64
LDFLAGS := -lws2_32
SO := dll
CP := cp -u -v -p
PACK := zip -9 -r -v
PACK_EXT := zip
WINDRES := windres

endif

FF3PTLIB := $(FF)-3pt/$(OS)-$(TARGET)


C := gcc
LD := gcc
OPT := -O2
# -D_DEBUG
OBJCOPY := objcopy
STRIP := strip

override CFLAGS += $(ALL_CFLAGS) -c $(OPT) -g -Wall -Werror -pthread \
	-I$(SRCDIR) -I$(FF) -I$(FFOS) -I$(FF)-3pt \
	-ffunction-sections -fdata-sections  -fvisibility=hidden

override FF_CFLAGS += $(ALL_CFLAGS) $(OPT) -g \
	-ffunction-sections -fdata-sections  -fvisibility=hidden

override LDFLAGS += -pthread \
	-L$(FF3PTLIB) \
	-fvisibility=hidden -Wl,-gc-sections

# 3-party libraries
ZLIB := -lz
SSL_LIBS := -lcrypto -lssl

include $(PROJDIR)/makerules


%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@


package: install
	rm -f $(PROJ)-$(VER)-$(OS)-$(TARGET).$(PACK_EXT) \
		&&  $(PACK) $(PROJ)-$(VER)-$(OS)-$(TARGET).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(TARGET)-debug.$(PACK_EXT) ./*.debug
