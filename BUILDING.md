# fmedia Building Instructions

Contents:

* [Autobuild Script](#autobuild-script)
* [Build On Linux](#build-on-linux)
* [Build On Linux For Windows](#build-on-linux-for-windows)


## Autobuild Script

The easiest way to build fmedia is by using `fmedia-autobuild.sh` script.  It will install the necessary tools and libraries on your OS, download the repositories with fmedia source code and automatically build everything for you.  In the end you will get a local directory with complete fmedia installation.

> Note that the script builds the latest `master` branch (it may contain new features or bugs).

> Currently the script only works on `fedora/amd64` & `debian/amd64` with partial support for `macos/arm64`.

> Note for macos/arm64: not all 3rd-party libraries can be built.  You have to disable them by manually applying the patch from `apple-m1` branch.  As a result, `--dynanorm` switch won't work and decoding `.ape` files won't work too.


## Build On Linux

0. Requirements:

	* GNU make
	* gcc or clang
	* libalsa-devel (for ALSA module)
	* libpulse-devel (for Pulse Audio module)
	* jack-audio-connection-kit-devel/pipewire-jack-audio-connection-kit-devel (for JACK module)
	* gtk3-devel (for GUI module)
	* dbus-devel (for DBUS module)

	For Fedora:

		dnf install make gcc alsa-lib-devel pulseaudio-libs-devel pipewire-jack-audio-connection-kit-devel gtk3-devel dbus-devel

	For Debian:

		apt-get install make gcc libasound2-dev libpulse-dev libjack-dev libgtk-3-dev libdbus-1-dev

1. Create a directory for all needed sources:

		mkdir fmedia-src && cd fmedia-src

2. Download all needed source repositories:

		git clone https://github.com/stsaz/ffbase
		git clone https://github.com/stsaz/ffaudio
		git clone https://github.com/stsaz/ffos
		git clone https://github.com/stsaz/avpack
		git clone https://github.com/stsaz/ffpack
		git clone https://github.com/stsaz/alphahttpd
		git clone https://github.com/stsaz/fmedia

Note: builds from the latest `master` branch are not supported and *may not work*!
To build a working package you should checkout a specific git tag for `fmedia` repo (e.g. `v1.26`) and then checkout the corresponding git commits for other repositories with the same commit date.

3. Build alib3 package (3rd-party audio codec libraries) or use pre-built binaries from the previous fmedia release.

	Option 1. Build anew:

		cd fmedia/alib3
		make
		make md5check
		make install
		cd ../../

	Option 2. Use pre-built binaries (copy to `alib3/_{OS}-{CPU}/` directory), e.g. for Linux/AMD64:

		tar Jxf fmedia-1.XX-linux-amd64.tar.xz -C /tmp
		mkdir -p fmedia/alib3/_linux-amd64
		cp -v /tmp/fmedia-1/mod/lib*-ff.so fmedia/alib3/_linux-amd64

4. Build other third-party packages:

		cd ffpack/zstd
		make
		cd ..
		make md5check
		make install
		cd ..

5. Build fmedia:

		cd fmedia
		make

	You can explicitly specify the path to each source repository, e.g.:

		make FFOS=~/ffos FFBASE=~/ffbase FFAUDIO=~/ffaudio AVPACK=~/avpack FFPACK=~/ffpack

	On Linux `gcc` is used by default.  Specify `clang` like this:

		make COMPILER=clang

6. Prepare the complete fmedia installation directory:

		make install

7. Ready!  You can copy the directory `./fmedia-1` anywhere you want (see README.md, section "INSTALL ON LINUX").


## Build On Linux For Windows

0. Requirements:

	For Fedora (Windows/AMD64 target):

		dnf install mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads mingw64-winpthreads-static dos2unix

1-4. See section "Build On Linux".

5. Build with mingw (64-bit):

		cd fmedia
		mingw64-make OS=windows CPU=amd64 CROSS_PREFIX=x86_64-w64-mingw32-

	Build with mingw (32-bit):

		cd fmedia
		mingw32-make OS=windows CPU=x86 CROSS_PREFIX=i686-w64-mingw32-


## Light Build

You can build fmedia without dependencies on 3rd-party libraries.  This will be a very small package without audio (de)compression features.  Follow these steps:

1-2. Repeat previously described steps.

3. Run this command:

		make install-nodeps

4. Edit `fmedia.conf` and manually remove all modules that require 3rd-party libraries.
