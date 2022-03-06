# libMAC

include ../makeconf

VER := 433
URL := https://github.com/stsaz/fmedia/files/8148606/MAC_SDK_433.zip
PKG := $(notdir $(URL))
DIR := mac
LIB := libMAC-ff.$(SO)

all: $(DIR)
	find $(DIR) -name '*.cpp' -print0 | xargs -0 dos2unix
	find $(DIR) -name '*.h' -print0 | xargs -0 dos2unix
	cd $(DIR) && cat ../*.patch | patch -p1
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNZIP) $(PKG) -d $(DIR)
	touch $(DIR)

# build
CFLAGS += \
	-I. -I$(DIR)/Source/Shared -I$(DIR)/Source/MACLib
ifeq ($(OS),windows)
	CFLAGS += -DPLATFORM_WINDOWS -DWINVER=0x0501
else
	CFLAGS += -DPLATFORM_LINUX
endif

SRC := \
	$(DIR)/Source/MACLib/APEDecompress.cpp \
	$(DIR)/Source/MACLib/NewPredictor.cpp \
	$(DIR)/Source/MACLib/NNFilter.cpp \
	$(DIR)/Source/MACLib/Prepare.cpp \
	$(DIR)/Source/MACLib/UnBitArrayBase.cpp \
	$(DIR)/Source/MACLib/UnBitArray.cpp \
	$(DIR)/Source/Shared/GlobalFunctions.cpp
OBJ := MAC-ff.o $(SRC:.cpp=.o)

MAC-ff.o: MAC-ff.cpp
	$(CXX) $(CFLAGS) $< -o $@

%.o: %.cpp
	$(CXX) $(CFLAGS) $< -o $@

$(DIR)/Source/MACLib/NNFilter.o: $(DIR)/Source/MACLib/NNFilter.cpp
	$(CXX) $(CFLAGS) -msse2 $< -o $@

$(LIB): $(OBJ)
	$(LINKXX) $(LINKXXFLAGS) -shared $+ -o $@

clean:
	$(RM) $(OBJ) $(DIR)


MAC_SRC := \
	$(DIR)/Source/MACLib/APEDecompress.cpp \
	$(DIR)/Source/MACLib/NewPredictor.cpp \
	$(DIR)/Source/MACLib/NNFilter.cpp \
	$(DIR)/Source/MACLib/Prepare.cpp \
	$(DIR)/Source/MACLib/UnBitArrayBase.cpp \
	$(DIR)/Source/MACLib/UnBitArray.cpp \
	$(DIR)/Source/Shared/GlobalFunctions.cpp \
	\
	$(DIR)/Source/MACLib/MACLib.cpp \
	$(DIR)/Source/MACLib/md5.cpp \
	$(DIR)/Source/MACLib/APEHeader.cpp \
	$(DIR)/Source/MACLib/APEInfo.cpp \
	$(DIR)/Source/MACLib/APECompressCore.cpp \
	$(DIR)/Source/MACLib/APECompress.cpp \
	$(DIR)/Source/MACLib/APECompressCreate.cpp \
	$(DIR)/Source/MACLib/APESimple.cpp \
	$(DIR)/Source/MACLib/WAVInputSource.cpp \
	$(DIR)/Source/MACLib/MACProgressHelper.cpp \
	$(DIR)/Source/MACLib/APELink.cpp \
	$(DIR)/Source/MACLib/BitArray.cpp \
	$(DIR)/Source/MACLib/APETag.cpp \
	$(DIR)/Source/Shared/CircleBuffer.cpp \
	$(DIR)/Source/Shared/StdLibFileIO.cpp \
	$(DIR)/Source/Shared/CharacterHelper.cpp \
	$(DIR)/Source/Console/Console.cpp
MAC_O := $(MAC_SRC:.cpp=.o)

mac-bin: $(MAC_O)
	$(LINKXX) $(LINKXXFLAGS) -o $@
