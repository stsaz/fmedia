---------------
OVERVIEW
---------------

fmedia is a fast media player/recorder/converter for Windows, macOS, Linux and FreeBSD.
It provides smooth playback and recording even if devices are very slow.
It's highly customizable and can be easily extended with additional plugins.
Its low CPU & memory consumption saves energy when running on a notebook's battery.

Play or convert audio files, record new audio tracks from microphone, save songs from Internet radio, and much more!
fmedia is free and open-source project, and you can use it as a standalone application or as a library for your own software.

fmedia can read: .mp3, .ogg (Vorbis, Opus), .opus, .m4a/.mp4 (AAC, ALAC, MPEG), .mka/.mkv (AAC, ALAC, MPEG, Vorbis), .avi (AAC, MPEG), .aac, .mpc, .flac, .ape, .wv, .wav.

fmedia can write: .mp3, .ogg, .opus, .m4a (AAC), .flac, .wav, .aac (--stream-copy only).

### Contents:

* FEATURES
* INSTALL
	* INSTALL ON WINDOWS
	* INSTALL ON LINUX
	* BUILD ON LINUX
	* BUILD ON LINUX FOR WINDOWS
* CONFIG
* EXTRACT TRACKS FROM FLAC.CUE
* TERMINAL UI
* GRAPHICAL UI
* USE-CASES
* FOR DEVELOPERS
* BUG REPORT


---------------
FEATURES
---------------

### INPUT
* File
* ICY
* HLS
* WASAPI Capture
* Windows Direct Sound Capture
* ALSA Capture

### FILTERS
* Containers:
	* .mp4/.m4a (read/write)
	* .mkv/.mka (read)
	* .ogg/.opus (read/write)
	* .avi (read)
	* .mp3 (read/write)
	* .aac (read, write: --stream-copy only)
	* .mpc (read)
	* .flac (read/write)
	* .ape (read)
	* .wv (read)
	* .wav (read/write)

* Lossy codecs:
	* MPEG (decode/encode)
	* Vorbis (decode/encode)
	* Opus (decode/encode)
	* AAC (decode/encode)
	* Musepack (decode)

* Lossless codecs:
	* FLAC (decode/encode)
	* ALAC (decode)
	* WavPack (decode)
	* APE (decode)

* Playlists:
	* .m3u/.m3u8, .pls (read)
	* .cue (read)
	* Directory

* Other:
	* PCM converter
	* PCM peaks
	* Mixer
	* Dynamic Audio Normalizer
	* Terminal UI
	* Graphical UI

### OUTPUT
* File
* Windows Direct Sound Playback
* WASAPI Playback
* ALSA Playback
* Pulse Audio Playback
* OSS Playback

fmedia uses modified versions of these 3rd party libraries: libALAC, libfdk-aac, libFLAC, libMAC, libmp3lame, libmpg123, libmpc, libogg, libopus, libsoxr, libvorbisenc, libvorbis, libwavpack, libDynamicAudioNormalizer.  See `ff-3pt/README.txt` for details.


---------------
INSTALL
---------------

### INSTALL ON WINDOWS

1. Unpack archive to the directory of your choice, e.g. to `"C:\Program Files\fmedia"`

	* Right click on fmedia package file (e.g. `fmedia-1.0-win-x64.zip`) in Explorer
	* Choose "Extract All..." in the popup menu
	* Follow the Wizard steps

2. Optionally, run the following command from console (cmd.exe):

		"C:\Program Files\fmedia\fmedia.exe" --install

	This command will:
	* add fmedia directory into user's environment
	* create a desktop shortcut to `fmedia-gui.exe`

3. Run `fmedia-gui.exe` to open graphical interface;  or execute commands via `fmedia.exe` from console (cmd.exe).

### INSTALL ON LINUX

1. Unpack archive to the directory of your choice, e.g. to `/usr/local/fmedia-1`:

		tar Jxf ./fmedia-1.0-linux-amd64.tar.xz -C /usr/local

2. Optionally, create a symbolic link:

		ln -s /usr/local/fmedia-1/fmedia /usr/local/bin/fmedia

3. Run `fmedia --gui` to open graphical interface;  or execute commands via `fmedia` from console.


### BUILD ON LINUX

0. Requirements:
	* GNU make
	* gcc or clang
	* libalsa-devel (for ALSA module)
	* libpulse-devel (for Pulse Audio module)
	* gtk3-devel (for GUI module)

1. Create a directory for all needed sources:

		mkdir fmedia-src && cd fmedia-src

2. Download all needed source repositories:

		git clone https://github.com/stsaz/ffos
		git clone https://github.com/stsaz/ff
		git clone https://github.com/stsaz/ff-3pt
		git clone https://github.com/stsaz/fmedia

3. Build ff-3pt package (3rd-party libraries) or download pre-built binaries.  See `ff-3pt/README.txt` for details.

4. Build fmedia:

		cd fmedia
		export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ff-3pt-bin/linux-amd64
		make install

	You can explicitly specify path to each of FF source repositories, e.g.:

		make install FFOS=~/ffos FF=~/ff FF3PT=~/ff-3pt

	Default architecture is amd64.  You can specify different target architecture like this:

		make install ARCH=i686

	You'll also need to specify the proper path to ff-3pt binaries in `LD_LIBRARY_PATH`.

5. Ready!  You can copy the directory `./fmedia-1` anywhere you want (see section "INSTALL ON LINUX").

### LIGHT BUILD

You can build fmedia without dependencies on 3rd-party libraries.  This will be a very small package without audio (de)compression features.  Follow these steps:

1-2. Repeat previously described steps.

3. Run this command:

		make install-nodeps

4. Edit `fmedia.conf` and manually remove all modules that require 3rd-party libraries.

### BUILD ON LINUX FOR WINDOWS

1-3. See section "BUILD ON LINUX".

4. Build with mingw (64-bit):

		cd fmedia
		export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ff-3pt-bin/win-amd64
		mingw64-make OS=win CPREFIX=x86_64-w64-mingw32- install

	Build with mingw (32-bit):

		cd fmedia
		export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ff-3pt-bin/win-i686
		mingw32-make OS=win ARCH=i686 CPREFIX=i686-w64-mingw32- install

5. Ready!


---------------
CONFIG
---------------
The global configuration file `fmedia.conf` is located within the fmedia directory itself.  It contains all supported settings and their default values.  You must restart fmedia after you make changes to this file.

Additional settings may be stored in file `fmedia-ext.conf`.  This makes it easier to upgrade fmedia without the need to edit `fmedia.conf`.

Per-user configuration settings are also supported, they must be stored in `fmedia-user.conf` file in home directory:

	Windows: %APPDATA%/fmedia/fmedia-user.conf
	Linux:   $HOME/.config/fmedia/fmedia-user.conf

Settings for a module must be in format "so.module.key value", e.g. to overwrite the global setting for OGG Vorbis encoding quality you should write:

	vorbis.encode.quality "7.0"

Core configuration settings start with "core.", e.g. set codepage for non-Unicode text:

	core.codepage win1252


---------------
EXTRACT TRACKS FROM flac.cue
---------------
While extracting a track from the album in FLAC using CUE sheet, the audio is first decoded to PCM and then re-encoded with FLAC.  This behaviour won't result in any audio quality loss since FLAC is a lossless codec.


---------------
TERMINAL UI
---------------
By default fmedia runs with a terminal UI, which shows information about the currently playing audio track and the currently playing audio position.  User commands such as seeking are also supported, all supported commands are described in file "help-tui.txt".


---------------
GRAPHICAL UI
---------------
On Windows you can run fmedia in GUI mode:

	fmedia-gui.exe

You should use this binary file for opening files via Explorer's "Open With..." feature.

Note: command-line options are not supported.

Or you may execute the console binary like this:

	fmedia --gui

fmedia GUI is provided by a separate module - gui.dll.  It is written in such a way that it won't become unresponsive even if the main thread (the one that processes audio) is waiting for I/O operations.  The module doesn't perform any sound processing, it just issues commands to fmedia core and receives the feedback.

fmedia GUI is highly customizable, thanks to FF library that is used under the hood.  FF UI technology allows you to modify properties of every UI control: windows, buttons, menus, tray icons and more.  You may resize controls, set different styling, change any text, hotkeys, etc.  All this information is stored within "fmedia.gui" which is a plain text file.  After you make some changes in fmedia.gui, save it and then restart fmedia.

By default fmedia GUI saves its state in file `"%APPDATA%\fmedia\fmedia.gui.conf"`.  You can change this by setting "portable_conf" to "true" in fmedia.conf.  After that, "fmedia.gui.conf" will be stored in program directory (e.g. `"C:\Program Files\fmedia\fmedia.gui.conf"`), thus making fmedia completely portable.


---------------
USE-CASES
---------------

Note the difference between UNIX and Windows terminals when you use special characters and spaces:

* Use single quotes ('') on Linux (sh, bash), e.g.:

		fmedia './my file.ogg'
		fmedia file.wav -o '$filename.ogg'

* Use double quotes ("") on Windows (cmd.exe), e.g.:

		fmedia "./my file.ogg"


### PLAY

Play files, directories, Internet-radio streams

	fmedia ./file.ogg ./*.mp3
	fmedia ./Music
	fmedia http://radio-stream:80/

Play (mix) multiple streams simultaneously

	fmedia --mix ./file1.ogg ./file2.ogg

Play wav file with a corrupted header

	fmedia ./file.raw --fseek=44

### CONVERT

Convert with parameters

	fmedia ./file.ogg --out=./file.wav --format=int16
	fmedia ./file.wav --out=./file.ogg --vorbis.quality=7.0
	fmedia ./file.wav --out=./file.mp3 --mpeg-quality=0 --rate=48000

Convert all .wav files from the current directory to .ogg

	fmedia ./*.wav --out=.ogg

Convert file and override meta info

	fmedia ./file.flac --out=.ogg --meta='artist=Artist Name;comment=My Comment'

Extract several tracks from .cue file

	fmedia ./album.flac.cue --track=3,7,13 --out='$tracknumber. $artist - $title.flac'

Split audio file

	fmedia ./file.wav --seek=00:35 --until=01:35 --out=./file-1.wav

Cut compressed audio without re-encoding

	fmedia ./file.ogg --out=./out.ogg --seek=1:00 --until=2:00 --stream-copy

Copy left channel's audio from a stereo source

	fmedia ./stereo.ogg -o left.wav --channels=left

Change sound volume in an audio file

	fmedia --gain=5.0 ./file.wav --out=./file-loud.wav

### RECORD

Capture audio from the default audio input device until stopped

	fmedia --record --out=rec.flac

Record with the specific audio format

	fmedia --record -o rec.wav --format=int24 --channels=mono --rate=48000

Record for 60 seconds then stop

	fmedia --record --out=rec.flac --until=60

Record while playing

	fmedia ./file.ogg --record --out=./rec.wav

Live output

	fmedia --record

Record audio from Internet radio (without re-encoding)

	fmedia http://radio-stream:80/ -o ./radio.mp3 --stream-copy

Play AND record audio from Internet radio into separate files (without re-encoding)

	fmedia http://radio-stream:80/ --out-copy -o './$time. $artist - $title.mp3' --stream-copy

### OTHER FUNCTIONS

Print audio meta info

	fmedia --info ./file.mp3

Print audio meta info and all tags

	fmedia --info --tags ./file.mp3

Show PCM information

	fmedia input.ogg --pcm-peaks

Create a playlist file from directory:

	fmedia ./Music -o music.m3u8


---------------
FOR DEVELOPERS
---------------

### YOUR APPLICATION BASED ON FMEDIA

fmedia can be used as a sound library: you can freely use its abilities in your own software.  And you don't have to build fmedia by yourself to use its features.  All you need to do is link your binary file with `core.so` (or `core.dll`) and you'll be able to do everything that fmedia can: playback, record and convert audio from your application.

### SUPPORT NEW FORMAT

You may add support for a new audio format into fmedia.  To do that you have to add your module into "fmedia.conf" and add an appropriate file extension into "input_ext" or "output_ext" section.

For example, after you have built your module (e.g. `xyz.so`), add it into "fmedia.conf":

	mod "xyz.decode"

Then associate it with ".xyz" file extension:

	input_ext {
		...
		"xyz.decode" xyz
	}

fmedia will call module "xyz.decode" each time user orders fmedia to play "*.xyz" files.

See fmedia source code for more details.  For example, main.c::main() will show you how fmedia command line binary initializes core module.  See acodec/wav.c for an example on how to write a simple filter for fmedia.


### LOW-LEVEL INTERFACE

If you'd like to use low level interfaces, take a look at the source code of FF & FF-3pt libraries.  Together they provide you with an easy interface that you can use to work with a large set of file formats, decode or encode audio and much more.  fmedia itself is built upon FF library - it's completely free and open-source.


### PARTICIPATE

You are welcome to participate in fmedia's development.  Send suggestions, improvements, bug reports, patches - anything that can help the project!

Understanding the top-level source code hierarchy can help you to get involved into fmedia quicker.  The source code consists of the 3 levels of these 4 separate repositories:

	---------------
	    fmedia
	---------------
	      FF
	---------------
	 FFOS | FF-3pt
	---------------

Each of them plays its own part:

* FFOS provides cross-platform abilities.  Code based on FFOS can run on Windows, Linux and FreeBSD.
* FF-3pt provides simple access to 3rd party libraries such as libFLAC.
* FF contains all low/mid level interfaces that can be reused between different applications.
* fmedia contains application code, it's largely based on all FF libraries.


---------------
BUG REPORT
---------------
If you encounter a bug, please report it: the more issues will be reported by users, the more stable fmedia will become.  When filing a bug report try to provide information that can help us to fix the problem.  Try to execute the same command once again, only this time add --debug switch, e.g.:

	fmedia --debug OPTIONS INPUT_FILES...

It will print a lot of information about what fmedia is doing.  This info or a screenshot would be very helpful.


---------------
LICENSE
---------------
The code provided here is free for use in open-source and proprietary projects.

You may distribute, redistribute, modify the whole code or the parts of it, just keep the original copyright statement inside the files.


---------------
HOMEPAGE
---------------
https://stsaz.github.io/fmedia/
