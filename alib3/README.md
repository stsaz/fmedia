# Third-party audio libraries

* In order to be used by fmedia all these libraries must be built in a specific way - with plain and simple make files rather than with the official (sometimes unnecessarily huge and complex) make files.
* Sometimes the call to configure script is necessary to generate header files.
* For each library there's a wrapper that provides a different API that's easier to use and more suitable for fmedia.
* For some of the wrappers to work correctly we require quite large patches to original code.  The downside is that it's hard to upgrade those libs.
* The functionality that's not used by fmedia is either removed or disabled; fmedia doesn't use the official include files.
* The resulting binaries aren't compatible with any applications that use the official builds, so to eliminate ambiguity the file names have "-ff" suffix, e.g. "libNAME-ff.so".

## Libs

* alac-rev2
* DynamicAudioNormalizer-2.10
* fdk-aac-0.1.6
* flac-1.3.3
* lame-3.100
* MAC-433
* mpg123-1.25.10
* musepack-r475
* ogg-1.3.3
* opus-1.3.1
* soxr-0.1.3
* vorbis-1.3.7
* wavpack-4.75.0


## Requirements

* Internet connection (if an archive file doesn't yet exist)
* make
* cmake (for libsoxr)
* dos2unix (for libMAC)
* patch
* gcc/clang
* g++/clang++


## Build

By default the make recipe builds for Linux AMD64.

	make -j8
	make install


## LICENSE

This directory contains copies of original and auto-generated code from 3rd party libraries.  This code is the property of their owners.  This code and binary files created from this code are licensed accordingly to the licenses of those libraries.

All other code provided here is absolutely free.
