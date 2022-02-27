# libDynamicAudioNormalizer

include ../makeconf

VER := 2.10
URL := https://github.com/lordmulder/DynamicAudioNormalizer/archive/refs/tags/$(VER).tar.gz
PKG := $(notdir $(URL))
DIR := DynamicAudioNormalizer-$(VER)
LIB := libDynamicAudioNormalizer-ff.$(SO)

all: $(DIR)
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_GZ) $(PKG)
	touch $(DIR)

# build
CFLAGS += -Wall -std=gnu++11 \
	-ffast-math -mfpmath=sse -msse -fomit-frame-pointer -fno-tree-vectorize \
	-DNO_JAVA_SUPPORT -DNDEBUG \
	-I$(DIR)/DynamicAudioNormalizerAPI/src -I$(DIR)/DynamicAudioNormalizerAPI/include -I$(DIR)/DynamicAudioNormalizerShared/include

SRC := \
	$(DIR)/DynamicAudioNormalizerAPI/src/DynamicAudioNormalizer.cpp \
	$(DIR)/DynamicAudioNormalizerAPI/src/GaussianFilter.cpp \
	$(DIR)/DynamicAudioNormalizerAPI/src/FrameBuffer.cpp \
	$(DIR)/DynamicAudioNormalizerAPI/src/Version.cpp
OBJ := DynamicAudioNormalizer-ff.o $(SRC:.cpp=.o)

%.o: %.cpp
	$(CXX) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINKXX) $(LINKXXFLAGS) -shared $+ -o $@

clean:
	$(RM) $(OBJ) $(DIR)
