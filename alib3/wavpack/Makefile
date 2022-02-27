# libwavpack

include ../makeconf

VER := 4.75.0
URL := http://www.wavpack.com/wavpack-$(VER).tar.bz2
PKG := $(notdir $(URL))
DIR := wavpack-$(VER)
LIB := libwavpack-ff.$(SO)

all: $(DIR)
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
CFLAGS += -I. \
	-DWVPK_EXP -DHAVE___BUILTIN_CLZ=1 -DHAVE___BUILTIN_CTZ=1 \
	-DVER4_ONLY -DNO_SEEKING -DNO_TAGS -DNO_USE_FSTREAMS

SRC := \
	$(DIR)/src/common_utils.c \
	$(DIR)/src/decorr_utils.c \
	$(DIR)/src/entropy_utils.c \
	$(DIR)/src/open_utils.c \
	$(DIR)/src/read_words.c \
	$(DIR)/src/unpack.c \
	$(DIR)/src/unpack_floats.c \
	$(DIR)/src/unpack_utils.c

ifeq ($(CPU),i686)
	CFLAGS += -DOPT_ASM_X86=1
	SRC_AS := $(DIR)/src/unpack_x86.s

else
	CFLAGS += -DOPT_ASM_X64=1
	SRC_AS := $(DIR)/src/unpack_x64.s
endif

OBJ := $(SRC:.c=.o)
OBJ += $(SRC_AS:.s=.o)

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.s
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

clean:
	$(RM) $(OBJ) $(DIR)
