# Build example binaries for fmedia core

# Set OS
ifndef OS
	uname := $(shell uname)
	ifeq "$(uname)" "Linux"
		OS := linux
	else ifeq "$(uname)" "FreeBSD"
		OS := freebsd
	else ifeq "$(uname)" "Darwin"
		OS := apple
	endif
else ifeq "$(OS)" "Windows_NT"
	# OS=Windows_NT is default env var on Windows
	OS := windows
endif

DOTEXE :=
SO := so
ifeq "$(OS)" "apple"
	SO := dylib
else ifeq "$(OS)" "windows"
	DOTEXE := .exe
	SO := dll
endif

# Set compiler
COMPILER := gcc
ifeq "$(OS)" "freebsd"
	COMPILER := clang
endif
ifeq "$(OS)" "apple"
	COMPILER := clang
endif

CROSS_PREFIX :=
ifndef CROSS_PREFIX
	ifeq "$(OS)" "windows"
		CROSS_PREFIX := x86_64-w64-mingw32-
	endif
endif

C := $(CROSS_PREFIX)gcc -c -pipe
CXX := $(CROSS_PREFIX)g++ -c -pipe
LINK := $(CROSS_PREFIX)gcc -pipe
ifeq "$(COMPILER)" "clang"
	C := clang -c
	CXX := clang++ -c
	LINK := clang
endif

all: fmedia-record$(DOTEXE)

ROOT := ../../..
CFLAGS := -I$(ROOT)/fmedia/src -I$(ROOT)/ffos -I$(ROOT)/ffbase
CFLAGS += -O0 -g

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

fmedia-record$(DOTEXE): record.o
	$(LINK) $+ -o $@
