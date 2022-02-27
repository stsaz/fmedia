# libvorbis, libvorbisenc

include ../makeconf

VER := 1.3.7
URL := http://downloads.xiph.org/releases/vorbis/libvorbis-$(VER).tar.xz
PKG := $(notdir $(URL))
DIR := libvorbis-$(VER)

all: $(DIR)
	cd $(DIR) && cat ../*.patch | patch -p1
	$(MAKE) -rR libvorbis-ff.$(SO) libvorbisenc-ff.$(SO)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_XZ) $(PKG)
	touch $(DIR)

# build
CFLAGS += \
	-I$(DIR)/lib -I$(DIR)/include -I.. \
	-DUSE_MEMORY_H -DVORB_EXP \
	-Wall -Wextra
ifeq ($(OS),linux)
	CFLAGS += -DHAVE_ALLOCA_H=1 -ffast-math
else ifeq ($(OS),freebsd)
	CFLAGS += -ffast-math
endif
LINKFLAGS += -L../ogg -logg-ff

SRC := \
	$(DIR)/lib/mdct.c \
	$(DIR)/lib/smallft.c \
	$(DIR)/lib/block.c \
	$(DIR)/lib/envelope.c \
	$(DIR)/lib/window.c \
	$(DIR)/lib/lsp.c \
	$(DIR)/lib/lpc.c \
	$(DIR)/lib/analysis.c \
	$(DIR)/lib/synthesis.c \
	$(DIR)/lib/psy.c \
	$(DIR)/lib/info.c \
	$(DIR)/lib/floor1.c \
	$(DIR)/lib/floor0.c \
	$(DIR)/lib/res0.c \
	$(DIR)/lib/mapping0.c \
	$(DIR)/lib/registry.c \
	$(DIR)/lib/codebook.c \
	$(DIR)/lib/sharedbook.c \
	$(DIR)/lib/lookup.c \
	$(DIR)/lib/bitrate.c
OBJ := vorbis-ff.o $(SRC:.c=.o)

SRC_ENC := $(DIR)/lib/vorbisenc.c
OBJ_ENC := vorbisenc-ff.o $(SRC_ENC:.c=.o)

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

libvorbis-ff.$(SO): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

libvorbisenc-ff.$(SO): $(OBJ_ENC) libvorbis-ff.$(SO)
	$(LINK) -shared $(OBJ_ENC) $(LINKFLAGS) -L. -lvorbis-ff -o $@

clean:
	$(RM) $(OBJ) $(OBJ_ENC) $(DIR)
