# libmpg123

include ../makeconf

VER := 1.25.10
URL := http://mpg123.de/download/mpg123-$(VER).tar.bz2
PKG := $(notdir $(URL))
DIR := mpg123-$(VER)
LIB := libmpg123-ff.$(SO)

ifeq ($(OS),windows)
	CONFIGURE_FLAGS := --host=x86_64-w64-mingw32
endif
all: $(DIR)
	cd $(DIR) && ./configure $(CONFIGURE_FLAGS)
	cd $(DIR) && cat ../*.patch | patch -p1
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_BZ2) $(PKG)
	touch $(DIR)

# build
CFLAGS += \
	-I. -I$(DIR) -I$(DIR)/src -I$(DIR)/src/libmpg123 -I$(DIR)/src/compat \
	-DMPG123_SO -DMPG123_EXPORT="" -DHAVE_CONFIG_H -DNO_ID3V2 -DNO_ICY -DNO_WARNING -DNO_ERRORMSG -DNO_ERETURN \
	-DOPT_MULTI -DOPT_GENERIC -DOPT_GENERIC_DITHER -DREAL_IS_FLOAT -DMPG123_NO_LARGENAME \
	-msse2 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

SRC := \
	$(DIR)/src/compat/compat_str.c \
	$(DIR)/src/libmpg123/parse.c \
	$(DIR)/src/libmpg123/index.c \
	$(DIR)/src/libmpg123/frame.c \
	$(DIR)/src/libmpg123/format.c \
	$(DIR)/src/libmpg123/dct64.c \
	$(DIR)/src/libmpg123/optimize.c \
	$(DIR)/src/libmpg123/readers.c \
	$(DIR)/src/libmpg123/tabinit.c \
	$(DIR)/src/libmpg123/libmpg123.c \
	$(DIR)/src/libmpg123/ntom.c \
	$(DIR)/src/libmpg123/synth.c  \
	$(DIR)/src/libmpg123/layer1.c \
	$(DIR)/src/libmpg123/layer2.c \
	$(DIR)/src/libmpg123/layer3.c \
	$(DIR)/src/libmpg123/dither.c

ifeq ($(CPU),i686)

	CFLAGS += \
		-DOPT_I386 -DOPT_I586 -DOPT_I586_DITHER -DOPT_MMX \
		-DOPT_3DNOW -DOPT_3DNOW_VINTAGE -DOPT_3DNOWEXT -DOPT_3DNOWEXT_VINTAGE \
		-DOPT_SSE -DOPT_SSE_VINTAGE -DNOXFERMEM -DNEWOLD_WRITE_SAMPLE

	SRC += \
		$(DIR)/src/libmpg123/synth_8bit.c \
		$(DIR)/src/libmpg123/synth_s32.c \
		$(DIR)/src/libmpg123/synth_real.c

	SRC_AS := \
		$(DIR)/src/libmpg123/dct64_i386.S \
		$(DIR)/src/libmpg123/synth_i586.S \
		$(DIR)/src/libmpg123/synth_i586_dither.S \
		$(DIR)/src/libmpg123/dct64_mmx.S \
		$(DIR)/src/libmpg123/tabinit_mmx.S \
		$(DIR)/src/libmpg123/synth_mmx.S \
		$(DIR)/src/libmpg123/synth_3dnow.S \
		$(DIR)/src/libmpg123/dct64_3dnow.S \
		$(DIR)/src/libmpg123/dct36_3dnow.S \
		$(DIR)/src/libmpg123/dct64_3dnowext.S \
		$(DIR)/src/libmpg123/synth_3dnowext.S \
		$(DIR)/src/libmpg123/dct36_3dnowext.S \
		$(DIR)/src/libmpg123/dct64_sse_float.S \
		$(DIR)/src/libmpg123/synth_sse_float.S \
		$(DIR)/src/libmpg123/synth_stereo_sse_float.S \
		$(DIR)/src/libmpg123/synth_sse_s32.S \
		$(DIR)/src/libmpg123/synth_stereo_sse_s32.S \
		$(DIR)/src/libmpg123/dct36_sse.S \
		$(DIR)/src/libmpg123/dct64_sse.S \
		$(DIR)/src/libmpg123/synth_sse.S \
		$(DIR)/src/libmpg123/getcpuflags.S

else
	CFLAGS += -DOPT_X86_64 -DOPT_AVX

	SRC_AS := \
		$(DIR)/src/libmpg123/getcpuflags_x86_64.S \
		$(DIR)/src/libmpg123/dct36_x86_64.S \
		$(DIR)/src/libmpg123/dct64_x86_64_float.S \
		$(DIR)/src/libmpg123/dct64_x86_64.S \
		$(DIR)/src/libmpg123/dct36_avx.S \
		$(DIR)/src/libmpg123/dct64_avx_float.S \
		$(DIR)/src/libmpg123/dct64_avx.S \
		$(DIR)/src/libmpg123/synth_8bit.S \
		$(DIR)/src/libmpg123/synth_s32.S \
		$(DIR)/src/libmpg123/synth_real.S \
		$(DIR)/src/libmpg123/synth_x86_64_float.S \
		$(DIR)/src/libmpg123/synth_x86_64_s32.S \
		$(DIR)/src/libmpg123/synth_stereo_x86_64_float.S \
		$(DIR)/src/libmpg123/synth_stereo_x86_64_s32.S \
		$(DIR)/src/libmpg123/synth_x86_64.S \
		$(DIR)/src/libmpg123/synth_stereo_x86_64.S \
		$(DIR)/src/libmpg123/synth_stereo_avx_float.S \
		$(DIR)/src/libmpg123/synth_stereo_avx_s32.S \
		$(DIR)/src/libmpg123/synth_stereo_avx.S

endif

OBJ := mpg123-ff.o $(SRC:.c=.o) $(SRC_AS:.S=.o)

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.S
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

clean:
	$(RM) $(OBJ) $(DIR)
