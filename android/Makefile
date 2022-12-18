# fmedia/Android makefile

ROOT := ../..
FMED_DIR := $(ROOT)/fmedia

# Set utils
MKDIR := mkdir -p
RM := rm -rf
CP := cp -au
LS := ls -l

# SDK_DIR :=
SDK_API := 33.0.0
NDK_VER := 25.1.8937393
NDK_DIR := $(SDK_DIR)/ndk/$(NDK_VER)

# Gradle
GRADLE := ./gradlew
GRD_BUILD := assembleRelease
ifeq "$(DEBUG)" "1"
	GRD_BUILD := assembleDebug
endif

# APK_KEY_STORE :=
# APK_KEY_PASS :=
# APK_VER :=
APK_SIGNED := fmedia-$(APK_VER)-android.apk
APP_ID := com.github.stsaz.fmedia
APK := fmedia/build/outputs/apk/release/fmedia-release-unsigned.apk
ifeq "$(DEBUG)" "1"
	APP_ID := com.github.stsaz.fmedia.debug
	APK := fmedia/build/outputs/apk/debug/fmedia-debug.apk
endif

# Libs paths
LIBDIR_AMD64 := $(FMED_DIR)/android/fmedia/libs/x86_64
LIBDIR_ARM64 := $(FMED_DIR)/android/fmedia/libs/arm64-v8a
LIBDIR_ARM := $(FMED_DIR)/android/fmedia/libs/armeabi-v7a

build: libs
	$(RM) $(LIBDIR_AMD64)
	$(MAKE) CPU=arm64 alib3
	$(MAKE) CPU=arm alib3
	$(GRADLE) $(GRD_BUILD)
	$(LS) $(APK)
	unzip -l $(APK) | grep lib

libs:
	$(MKDIR) $(BUILD_DIR)/obj-arm64 $(BUILD_DIR)/obj-arm $(LIBDIR_ARM64) $(LIBDIR_ARM)
	$(MAKE) CPU=arm64 $(LIBDIR_ARM64)/libfmedia.so.debug
	$(MAKE) CPU=arm $(LIBDIR_ARM)/libfmedia.so.debug
	$(LS) $(LIBDIR_ARM64)/libfmedia.so $(LIBDIR_ARM)/libfmedia.so
# 	$(C_DIR)/llvm-objdump -T $(LIBDIR_ARM64)/libfmedia.so

CPU := amd64
include $(FMED_DIR)/android/andk.mk

CFLAGS += -fvisibility=hidden -Wno-for-loop-analysis
ifeq "$(DEBUG)" "1"
	CFLAGS += -g -DFMED_DEBUG -DFF_DEBUG -Werror
endif
CFLAGS += \
	-I$(FMED_DIR)/src -I$(FMED_DIR)/alib3 \
	-I$(ROOT)/avpack \
	-I$(ROOT)/ffos -I$(ROOT)/ffbase
CFLAGS_OPT := -O2
APP_CFLAGS_OPT := $(CFLAGS_OPT) -fno-strict-aliasing
ifeq "$(COPT)" "0"
	CFLAGS_OPT := -O0
	APP_CFLAGS_OPT := $(CFLAGS_OPT)
endif
ifeq "$(CPU)" "arm"
	APP_CFLAGS_OPT := -O0
endif

LINK_RPATH_ORIGIN := '-Wl,-rpath,$$ORIGIN' -Wl,--disable-new-dtags
LINKFLAGS += -L$(FMED_DIR)/alib3/_android-$(CPU)
LINKFLAGS += $(LINK_RPATH_ORIGIN) \
	-lfdk-aac-phi \
	-lmpg123-phi \
	-latomic -llog -lc -lm

BUILD_DIR := .
OBJ_DIR := $(BUILD_DIR)/obj-$(CPU)

OBJ := \
	$(OBJ_DIR)/acodec.o \
	$(OBJ_DIR)/aconv.o \
	$(OBJ_DIR)/afilter.o \
	$(OBJ_DIR)/core.o \
	$(OBJ_DIR)/ffpcm.o \
	$(OBJ_DIR)/fmedia-jni.o \
	$(OBJ_DIR)/fmt.o

$(OBJ_DIR)/ffpcm.o: $(FMED_DIR)/src/afilter/ffpcm.c \
		$(wildcard $(FMED_DIR)/src/afilter/*.h)
	$(C) $(CFLAGS) $(CFLAGS_OPT) $< -o $@

$(OBJ_DIR)/%.o: $(FMED_DIR)/src/afilter/%.c \
		$(wildcard $(FMED_DIR)/src/afilter/*.h)
	$(C) $(CFLAGS) $(APP_CFLAGS_OPT) $< -o $@

$(OBJ_DIR)/%.o: $(FMED_DIR)/android/fmedia/src/main/c/%.c \
		$(wildcard $(FMED_DIR)/android/fmedia/src/main/c/*.h)
	$(C) $(CFLAGS) $(APP_CFLAGS_OPT) $< -o $@

LIBDIR := $(LIBDIR_AMD64)
ifeq "$(CPU)" "arm64"
	LIBDIR := $(LIBDIR_ARM64)
else ifeq "$(CPU)" "arm"
	LIBDIR := $(LIBDIR_ARM)
endif

$(LIBDIR)/libfmedia.so: $(OBJ)
	$(LINK) -shared $(LINKFLAGS) $+ -o $@

alib3:
	$(CP) \
		$(FMED_DIR)/alib3/_android-$(CPU)/libfdk-aac-phi.so \
		$(FMED_DIR)/alib3/_android-$(CPU)/libmpg123-phi.so \
		$(LIBDIR)

OBJCOPY := $(C_DIR)/llvm-objcopy
STRIP := $(C_DIR)/llvm-strip

%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@

lib-amd64:
	$(MKDIR) $(BUILD_DIR)/obj-amd64 $(LIBDIR_AMD64)
	$(MAKE) $(LIBDIR_AMD64)/libfmedia.so

build-install-amd64: lib-amd64
	$(MAKE) alib3
	$(GRADLE) $(GRD_BUILD)
	$(MAKE) install

clean:
	$(RM) $(BUILD_DIR)/obj-amd64 $(BUILD_DIR)/obj-arm64 $(BUILD_DIR)/obj-arm
	$(GRADLE) clean

sign:
	$(SDK_DIR)/build-tools/$(SDK_API)/zipalign -v -f 4 $(APK) $(APK_SIGNED)
	$(SDK_DIR)/build-tools/$(SDK_API)/apksigner sign --ks $(APK_KEY_STORE) --ks-pass pass:$(APK_KEY_PASS) $(APK_SIGNED)
	$(SDK_DIR)/build-tools/$(SDK_API)/apksigner verify $(APK_SIGNED)

build-sign: build
	$(MAKE) sign

sign-install: sign
	$(SDK_DIR)/platform-tools/adb install -r $(APK_SIGNED)

install:
	$(SDK_DIR)/platform-tools/adb install -r $(APK)

uninstall:
	$(SDK_DIR)/platform-tools/adb uninstall $(APP_ID)

build-install: build
	$(MAKE) install

emulator:
	$(SDK_DIR)/emulator/emulator -list-avds
	$(SDK_DIR)/emulator/emulator $(AVD)

log:
	$(SDK_DIR)/platform-tools/adb logcat -v time | grep --color=auto --line-buffered '/fmedia'

log-clear:
	$(SDK_DIR)/platform-tools/adb logcat -c

apk-list:
	unzip -l $(APK)
