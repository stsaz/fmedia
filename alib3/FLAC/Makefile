# libFLAC

include ../makeconf

VER := 1.4.2
URL := https://github.com/xiph/flac/releases/download/$(VER)/flac-$(VER).tar.xz
PKG := $(notdir $(URL))
DIR := flac-$(VER)
LIB := libFLAC-ff.$(SO)

all: $(DIR)/config.h
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_XZ) $(PKG)
	touch $(DIR)

_:=
ifeq "$(OS)" "windows"
	CONFIGURE_FLAGS := --host=x86_64-w64-mingw32
else ifeq "$(CPU)" "arm64"
	CONFIGURE_FLAGS := --host=aarch64
else ifeq "$(CPU)" "arm"
	CONFIGURE_FLAGS := --host=armv7
endif
ifeq "$(SYS)" "android"
	CONFIGURE_ENV := CC="$(C_DIR)/clang" CFLAGS="$(CFLAGS_TARGET)"
endif
$(DIR)/config.h: $(DIR)
	cd $(DIR) && $(CONFIGURE_ENV) ./configure --disable-ogg $(CONFIGURE_FLAGS)
	cd $(DIR) && cat ../*.patch | patch -p1
	cat $@

# build
CFLAGS += -O3 \
	-I. -I$(DIR) -I$(DIR)/src/libFLAC/include -I$(DIR)/include \
	-DHAVE_CONFIG_H -D_FORTIFY_SOURCE=2 \
	-fvisibility=hidden -fassociative-math -fno-signed-zeros -fno-trapping-math -freciprocal-math -funroll-loops \
	-Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Waggregate-return -Wcast-align \
	-Wnested-externs -Wshadow -Wundef -Wmissing-declarations -Winline -Wdeclaration-after-statement

SRC := \
	$(DIR)/src/libFLAC/bitmath.c \
	$(DIR)/src/libFLAC/bitreader.c \
	$(DIR)/src/libFLAC/bitwriter.c \
	$(DIR)/src/libFLAC/cpu.c \
	$(DIR)/src/libFLAC/crc.c \
	$(DIR)/src/libFLAC/fixed.c \
	$(DIR)/src/libFLAC/float.c \
	$(DIR)/src/libFLAC/format.c \
	$(DIR)/src/libFLAC/lpc.c \
	$(DIR)/src/libFLAC/md5.c \
	$(DIR)/src/libFLAC/memory.c \
	$(DIR)/src/libFLAC/stream_decoder.c \
	$(DIR)/src/libFLAC/stream_encoder.c \
	$(DIR)/src/libFLAC/stream_encoder_framing.c \
	$(DIR)/src/libFLAC/window.c

SRC += \
	$(DIR)/src/libFLAC/fixed_intrin_sse2.c \
	$(DIR)/src/libFLAC/fixed_intrin_ssse3.c \
	$(DIR)/src/libFLAC/lpc_intrin_avx2.c \
	$(DIR)/src/libFLAC/lpc_intrin_fma.c \
	$(DIR)/src/libFLAC/lpc_intrin_neon.c \
	$(DIR)/src/libFLAC/lpc_intrin_sse2.c \
	$(DIR)/src/libFLAC/lpc_intrin_sse41.c \
	$(DIR)/src/libFLAC/lpc_intrin_vsx.c \
	$(DIR)/src/libFLAC/stream_encoder_intrin_sse2.c \
	$(DIR)/src/libFLAC/stream_encoder_intrin_ssse3.c \
	$(DIR)/src/libFLAC/stream_encoder_intrin_avx2.c

OBJ := FLAC-ff.o $(SRC:.c=.o)

FLAC-ff.o: FLAC-ff.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@
ifeq "$(SYS)" "android"
	$(C_DIR)/llvm-objdump -T $@
endif

clean:
	$(RM) $(OBJ) $(DIR)
