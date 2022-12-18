
# Set compiler
C_DIR := $(NDK_DIR)/toolchains/llvm/prebuilt/linux-x86_64/bin
C := $(C_DIR)/clang -c
CXX := $(C_DIR)/clang++ -c
LINK := $(C_DIR)/clang
LINKXX := $(C_DIR)/clang++

# Set target
ifeq "$(CPU)" "amd64"
	CFLAGS += -target x86_64-none-linux-android21
	LINKFLAGS += -target x86_64-none-linux-android21
else ifeq "$(CPU)" "arm64"
	CFLAGS += -target aarch64-none-linux-android21
	LINKFLAGS += -target aarch64-none-linux-android21
else ifeq "$(CPU)" "arm"
	CFLAGS += -target armv7-none-linux-androideabi19 -mthumb
	LINKFLAGS += -target armv7-none-linux-androideabi19
endif

CFLAGS += -Wall -Wextra -Wno-unused-parameter \
	-fPIC -fdata-sections -ffunction-sections -fstack-protector-strong -funwind-tables \
	-no-canonical-prefixes \
	--sysroot $(NDK_DIR)/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
	-D_FORTIFY_SOURCE=2 -DANDROID -DNDEBUG

LINKFLAGS += -no-canonical-prefixes \
	-Wl,-no-undefined -Wl,--gc-sections -Wl,--build-id=sha1 -Wl,--no-rosegment
