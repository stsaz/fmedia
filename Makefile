
OS := linux

PROJ := fmedia
SRCDIR := ./src
INSTDIR := ./$(PROJ)
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

BIN := $(PROJ)
TARGET := amd64
override CFLAGS += -fpic
LD_LMATH := -lm
SO := so
PACK_EXT := tar.xz

ifeq ($(OS),linux)

override CFLAGS += -DFF_OLDLIBC
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

endif


C := gcc
LD := gcc
OPT := -O2
# -D_DEBUG

override CFLAGS += -c $(OPT) -g -Wall -Werror -pthread \
	-I$(SRCDIR) -I$(FF) -I$(FFOS) -I$(FF)/3pt \
	-ffunction-sections -fdata-sections  -fvisibility=hidden -fno-exceptions

override LDFLAGS += -pthread \
	-L$(FF)/3pt/win-x64 \
	-fvisibility=hidden -Wl,-gc-sections

# 3-party libraries
ZLIB := -lz
SSL_LIBS := -lcrypto -lssl

include ./makerules

package: install
	rm -f $(PROJ)-$(VER)-$(OS)-$(TARGET).$(PACK_EXT) \
		&&  $(PACK) $(PROJ)-$(VER)-$(OS)-$(TARGET).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(TARGET)-debug.$(PACK_EXT) ./*.debug
