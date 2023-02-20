# fmedia API Examples

fmedia can be used as a library for your own software, and here's how you can use it with an official fmedia installation, without building anything else but your own code.
By default, `fmedia-1/fmedia` executable file loads `core.so` and prepares the jobs for it according to command line.
You can do same things but **without** the official `fmedia` executable.
The approach is plain and simple.
All interfaces to fmedia modules are located in a single file `fmedia.h` which you include into your code.
Then you load `core.so` into your process and use C interfaces from `fmedia.h` to achieve your goals.
In theory, it's possible to use `core.so` from C# or Java, but you have to prepare the bindings for those languages yourself.

You need:

* Unpacked fmedia installation (e.g. from `fmedia-1.27.3-linux-amd64.tar.xz`)
* fmedia source code with git-HEAD set to the *same release tag* (e.g. `v1.27.3`)
* Source code for ffbase and ffos libraries with git-HEAD set to the *same commit date* as fmedia tag
* C compiler and linker
* `make`

Examples:

* `record.c`: Record to .wav file


## BUILD

	make

On Linux for Windows:

	make OS=windows


## INSTALL

The executable files MUST be inside fmedia directory (i.e. `fmedia-1/` for Linux) for the fmedia core to correctly find other modules in `mod/` directory.
This limitation should be removed in the future.
But for now, just copy your binary into fmedia directory.

Linux:

	cp fmedia-record PATH_TO_FMEDIA/

Windows:

	copy fmedia-record.exe PATH_TO_FMEDIA/
