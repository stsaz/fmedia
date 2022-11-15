# fmedia/Android makefile

# SDK_DIR :=
# KEY_STORE :=
# KEY_PASS :=
# APP_VER :=
SDK_API := 33.0.0
NDK_VER := 25.1.8937393
NDK_DIR := $(SDK_DIR)/ndk/$(NDK_VER)
ADB_DIR := $(SDK_DIR)/platform-tools
DEBUG := 0
APP_ID := com.github.stsaz.fmedia
NDK_FLAGS := V=1
GRADLE_FLAGS := assembleRelease
APK := fmedia/build/outputs/apk/release/fmedia-release-unsigned.apk
APK_SIGNED := fmedia-$(APP_VER)-android.apk

ifeq "$(DEBUG)" "1"
	GRADLE_FLAGS := assembleDebug
	APP_ID := com.github.stsaz.fmedia.debug
	APK := fmedia/build/outputs/apk/debug/fmedia-debug.apk
	NDK_FLAGS += NDK_DEBUG=1
endif

build: lib
	./gradlew $(GRADLE_FLAGS)

build-install: build
	$(MAKE) install

lib:
	cd fmedia && $(NDK_DIR)/ndk-build APP_ABI=arm64-v8a $(NDK_FLAGS)
	cd fmedia && $(NDK_DIR)/ndk-build APP_ABI=armeabi-v7a NDK_DEBUG=1 $(NDK_FLAGS)
	ls -l fmedia/libs/arm64-v8a/libfmedia.so fmedia/libs/armeabi-v7a/libfmedia.so

lib-amd64:
	cd fmedia && $(NDK_DIR)/ndk-build APP_ABI=x86_64 $(NDK_FLAGS)

sign:
	$(SDK_DIR)/build-tools/$(SDK_API)/zipalign -v -f 4 $(APK) $(APK_SIGNED)
	$(SDK_DIR)/build-tools/$(SDK_API)/apksigner sign --ks $(KEY_STORE) --ks-pass pass:$(KEY_PASS) $(APK_SIGNED)
	$(SDK_DIR)/build-tools/$(SDK_API)/apksigner verify $(APK_SIGNED)

install:
	$(ADB_DIR)/adb install -r $(APK)

install-signed:
	$(ADB_DIR)/adb install -r $(APK_SIGNED)

uninstall:
	$(ADB_DIR)/adb uninstall $(APP_ID)
