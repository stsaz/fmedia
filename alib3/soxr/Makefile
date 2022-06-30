# libsoxr

include ../makeconf

VER := 0.1.3
URL := https://jztkft.dl.sourceforge.net/project/soxr/soxr-$(VER)-Source.tar.xz
PKG := $(notdir $(URL))
DIR := soxr-$(VER)-Source
LIB := libsoxr-ff.$(SO)

all: $(DIR)
	cd $(DIR) && cmake -Wno-dev -DCMAKE_BUILD_TYPE="Release" .
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_XZ) $(PKG)
	touch $(DIR)

# build
CFLAGS += \
	-I$(DIR) -I$(DIR)/src \
	-DNDEBUG -DSOXR_LIB -D_DEFAULT_SOURCE \
	-Wconversion -Wall -W -pedantic -Wundef -Wcast-align -Wpointer-arith -Wno-long-long -Wnested-externs -Wmissing-prototypes -Wstrict-prototypes
ifeq ($(OS),windows)
	CFLAGS += -DSOXR_DLL -Dsoxr_EXPORTS -D_USE_MATH_DEFINES
else
	CFLAGS += -DSOXR_VISIBILITY
endif

SRC := \
	$(DIR)/src/cr.c \
	$(DIR)/src/cr32.c \
	$(DIR)/src/cr32s.c \
	$(DIR)/src/cr64.c \
	$(DIR)/src/cr64s.c \
	$(DIR)/src/data-io.c \
	$(DIR)/src/dbesi0.c \
	$(DIR)/src/fft4g32.c \
	$(DIR)/src/fft4g64.c \
	$(DIR)/src/filter.c \
	$(DIR)/src/pffft32s.c \
	$(DIR)/src/pffft64s.c \
	$(DIR)/src/soxr.c \
	$(DIR)/src/util32s.c \
	$(DIR)/src/util64s.c \
	$(DIR)/src/vr32.c
OBJ := $(SRC:.c=.o)

%.o: %.c $(wildcard $(DIR)/src/*.h)
	$(C) $(CFLAGS) $< -o $@

$(DIR)/src/cr64s.o: $(DIR)/src/cr64s.c $(wildcard $(DIR)/src/*.h)
	$(C) $(CFLAGS) -mavx $< -o $@

$(DIR)/src/pffft64s.o: $(DIR)/src/pffft64s.c $(wildcard $(DIR)/src/*.h)
	$(C) $(CFLAGS) -mavx $< -o $@

$(DIR)/src/util64s.o: $(DIR)/src/util64s.c $(wildcard $(DIR)/src/*.h)
	$(C) $(CFLAGS) -mavx $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

clean:
	$(RM) $(OBJ) $(DIR)
