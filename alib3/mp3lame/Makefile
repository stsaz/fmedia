# libmp3lame

include ../makeconf

VER := 3.100
URL := https://sourceforge.net/projects/lame/files/lame/
PKG := lame-$(VER).tar.gz
DIR := lame-$(VER)
LIB := libmp3lame-ff.$(SO)

all: $(DIR)
	cd $(DIR) && ./configure --disable-decoder
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	echo "Please download $(PKG) from $(URL)"
	exit 1

# unpack
$(DIR): $(PKG)
	$(UNTAR_GZ) $(PKG)
	touch $(DIR)

# build
CFLAGS += \
	-DHAVE_CONFIG_H \
	-I$(DIR) -I$(DIR)/include -I$(DIR)/libmp3lame \
	-ffast-math -funroll-loops

SRC := \
	$(DIR)/libmp3lame/bitstream.c \
	$(DIR)/libmp3lame/encoder.c \
	$(DIR)/libmp3lame/fft.c \
	$(DIR)/libmp3lame/gain_analysis.c \
	$(DIR)/libmp3lame/lame.c \
	$(DIR)/libmp3lame/newmdct.c \
	$(DIR)/libmp3lame/presets.c \
	$(DIR)/libmp3lame/psymodel.c \
	$(DIR)/libmp3lame/quantize.c \
	$(DIR)/libmp3lame/quantize_pvt.c \
	$(DIR)/libmp3lame/reservoir.c \
	$(DIR)/libmp3lame/set_get.c \
	$(DIR)/libmp3lame/tables.c \
	$(DIR)/libmp3lame/takehiro.c \
	$(DIR)/libmp3lame/util.c \
	$(DIR)/libmp3lame/vbrquantize.c \
	$(DIR)/libmp3lame/VbrTag.c \
	$(DIR)/libmp3lame/version.c \
	$(DIR)/libmp3lame/vector/xmm_quantize_sub.c
OBJ := lame-ff.o $(SRC:.c=.o)

lame-ff.o: lame-ff.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

$(DIR)/libmp3lame/vector/xmm_quantize_sub.o: $(DIR)/libmp3lame/vector/xmm_quantize_sub.c
	$(C) $(CFLAGS) -msse $< -o$@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

clean:
	$(RM) $(OBJ) $(DIR)
