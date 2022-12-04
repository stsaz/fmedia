# fmedia Building Instructions

Contents:

* [Build On Linux](#build-on-linux)
* [Build On Linux For Windows](#build-on-linux-for-windows)


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

		git clone --depth=1 https://github.com/stsaz/ffbase
		git clone --depth=1 https://github.com/stsaz/ffaudio
		git clone --depth=1 https://github.com/stsaz/ffos
		git clone --depth=1 https://github.com/stsaz/avpack
		git clone --depth=1 https://github.com/stsaz/fmedia

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

		tar Jxf fmedia-1.28-linux-amd64.tar.xz -C /tmp
		mkdir -p fmedia/alib3/_linux-amd64
		cp -v /tmp/fmedia-1/mod/lib*-ff.so fmedia/alib3/_linux-amd64

4. Build fmedia:

		cd fmedia
		make install

	You can explicitly specify path to each of FF source repositories, e.g.:

		make install FFOS=~/ffos

	Default architecture is amd64.  You can specify different target architecture like this:

		make install ARCH=i686

5. Ready!  You can copy the directory `./fmedia-1` anywhere you want (see README.md, section "INSTALL ON LINUX").

### Light Build

You can build fmedia without dependencies on 3rd-party libraries.  This will be a very small package without audio (de)compression features.  Follow these steps:

1-2. Repeat previously described steps.

3. Run this command:

		make install-nodeps

4. Edit `fmedia.conf` and manually remove all modules that require 3rd-party libraries.


## Build On Linux For Windows

0. Requirements:

	For Fedora:

		dnf install mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads mingw64-winpthreads-static dos2unix

1-3. See section "Build On Linux".

4. Build with mingw (64-bit):

		cd fmedia
		mingw64-make OS=windows CPREFIX=x86_64-w64-mingw32- install

	Build with mingw (32-bit):

		cd fmedia
		mingw32-make OS=windows ARCH=i686 CPREFIX=i686-w64-mingw32- install

5. Ready!