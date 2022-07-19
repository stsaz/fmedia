My anti-war statement:

I feel like I need to clearly define my position about the current Russia-Ukraine war.  The decision to start a military intrusion is taken **without any approval from citizens** of Russian Federation.  I firmly stand **against** any war, and I don't approve this particular war, but I can do nothing about it except writing these words.  Furthermore, I'm sure that the most Russians are also **against this war**, no matter what our propaganda may say.  And I hope the people around the Globe can understand this, because the worst mistake they can make is to hold the Russian people responsible for the situation - please don't let your local propaganda deceive your human reason.  I'm thankful to GitHub for their position on keeping the site open for people such as myself.  And here's my statement to the Government: I demand to immediately **stop** the military operations and **withdraw** all military forces from Ukraine's territory.


## OVERVIEW

<img align="right" src="res/fmedia.png" width="128" height="128">

fmedia is a fast media player/recorder/converter for Windows, macOS, Linux and FreeBSD.
It provides smooth playback and recording even if devices are very slow.
It's highly customizable and can be easily extended with additional plugins.
Its low CPU & memory consumption saves energy when running on a notebook's battery.

Play or convert audio files, record new audio tracks from microphone, save songs from Internet radio, and much more!
fmedia is free and open-source project, and you can use it as a standalone application or as a library for your own software.

**fmedia can read**: .mp3, .ogg (Vorbis/Opus), .opus, .mp4/.m4a/.mov (AAC/ALAC/MPEG), .mka/.mkv/.webm (AAC/ALAC/MPEG/Vorbis/Opus/PCM), .caf (AAC/ALAC/PCM), .avi (AAC/MPEG/PCM), .aac, .mpc, .flac, .ape, .wv, .wav;  .m3u, .pls, .cue.

**fmedia can write**: .mp3, .ogg, .opus, .m4a (AAC), .flac, .wav, .aac (--stream-copy only).

### Contents:

* [Features](#features)
* [Install](#install)
	* Install On Windows
	* Install On Linux
* [Build](#build)
	* Build On Linux
	* Build On Linux For Windows
* [Config](#config)
* [Terminal UI](#terminal-ui)
* [Graphical UI](#graphical-ui)
* [Extract Tracks From flac.cue](#extract-tracks-from-flac.cue)
* [Use-Cases](#use-cases)
	* [Play](#play)
	* [Convert](#convert)
	* [Record](#record)
	* [Edit Tags](#edit-tags)
	* [Other Functions](#other-functions)
* [For Developers](#for-developers)
* [Bug Report](#bug-report)


## FEATURES

* Audio I/O:
	* ALSA (capture/playback)
	* CoreAudio (capture/playback)
	* DirectSound (capture/playback)
	* JACK (capture)
	* OSS (capture/playback)
	* PulseAudio (capture/playback)
	* WASAPI (capture/playback)

* I/O:
	* File (read/write)
	* ICY-stream (read)
	* HLS (read)

* Containers:
	* .aac (read, write: --stream-copy only)
	* .ape (read)
	* .avi (read)
	* .caf (read)
	* .flac (read/write)
	* .mkv/.mka (read)
	* .mp3 (read/write)
	* .mp4/.m4a (read/write)
	* .mpc (read)
	* .ogg/.opus (read/write)
	* .wav (read/write)
	* .wv (read)

* Lossy codecs:
	* AAC (decode/encode)
	* MPEG (decode/encode)
	* Musepack (decode)
	* Opus (decode/encode)
	* Vorbis (decode/encode)

* Lossless codecs:
	* ALAC (decode)
	* APE (decode)
	* FLAC (decode/encode)
	* WavPack (decode)

* Playlists:
	* .m3u/.m3u8, .pls (read)
	* .cue (read)
	* Directory

* Other:
	* PCM converter
	* PCM peaks analyzer
	* Mixer
	* Dynamic Audio Normalizer
	* Terminal UI
	* Graphical UI (Windows, Linux/GTK)

fmedia uses modified versions of these 3rd party libraries: libALAC, libfdk-aac, libFLAC, libMAC, libmp3lame, libmpg123, libmpc, libogg, libopus, libsoxr, libvorbisenc, libvorbis, libwavpack, libDynamicAudioNormalizer.  See contents of `alib3/` for more info.


## INSTALL

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

1. Unpack archive to the directory of your choice, e.g. to your home directory (`~/bin/fmedia-1`):

		tar Jxf ./fmedia-1.0-linux-amd64.tar.xz -C ~/bin

2. Optionally, create a symbolic link:

		ln -s ~/bin/fmedia-1/fmedia /usr/local/bin/fmedia

3. Optionally, add fmedia GUI icon to KDE Applications:

		cp ~/bin/fmedia-1/fmedia.desktop ~/.local/share/applications

	Then edit `Exec=` and `Icon=` rows in `~/.local/share/applications/fmedia.desktop` if necessary.

4. Run `fmedia --gui` to open graphical interface;  or execute commands via `fmedia` from console.


## BUILD

### BUILD ON LINUX

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

5. Ready!  You can copy the directory `./fmedia-1` anywhere you want (see section "INSTALL ON LINUX").

### LIGHT BUILD

You can build fmedia without dependencies on 3rd-party libraries.  This will be a very small package without audio (de)compression features.  Follow these steps:

1-2. Repeat previously described steps.

3. Run this command:

		make install-nodeps

4. Edit `fmedia.conf` and manually remove all modules that require 3rd-party libraries.

### BUILD ON LINUX FOR WINDOWS

0. Requirements:

	For Fedora:

		dnf install mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads mingw64-winpthreads-static dos2unix

1-3. See section "BUILD ON LINUX".

4. Build with mingw (64-bit):

		cd fmedia
		mingw64-make OS=win CPREFIX=x86_64-w64-mingw32- install

	Build with mingw (32-bit):

		cd fmedia
		mingw32-make OS=win ARCH=i686 CPREFIX=i686-w64-mingw32- install

5. Ready!

### BUILD ON WINDOWS
#### With pictures since you Windows folk are so difficult
(Requirements)
1. If you haven't installed GIT, get the download [here](https://git-scm.com/download/win).
![image](https://user-images.githubusercontent.com/32961763/179779556-c811dde6-9033-4bdc-a476-a3fa858a5091.png)

2. Install Msys2 from https://www.msys2.org/ and follow all the instructions, including the `pacman -S --needed base-devel mingw-w64-x86_64-toolchain` command.
![image](https://user-images.githubusercontent.com/32961763/179779827-ba53a61a-1cc0-4748-a8aa-b0e7552e5ab3.png)
![image](https://user-images.githubusercontent.com/32961763/179780111-97e21b2e-4442-409c-91a9-434fbff330d4.png)

4. Open "Edit environment variables for your account" from the start menu.
![image](https://user-images.githubusercontent.com/32961763/179780133-cd2b1e49-ad68-4df5-bf20-d28b7b4c1836.png)

5. Add "C:\msys64\mingw64\bin" to the PATH variable
![image](https://user-images.githubusercontent.com/32961763/179780384-bb4be3e4-a719-424e-90e2-fb6671ee5cbc.png)
Hit OK and OK to close out the window.

(Downloads)
Now we must download fmedia and its dependencies. To do this, open git in the folder you wish to build in.
6. Right click the empty folder you wish to download and build fmedia in, and select "GIT bash here"
![image](https://user-images.githubusercontent.com/32961763/179780922-b1584391-796a-40cc-af2e-42b76f2c0999.png)

7. Copy and paste the following text into the git bash window and hit enter:
 ```git clone --depth=1 https://github.com/stsaz/ffbase
 git clone --depth=1 https://github.com/stsaz/ffaudio
 git clone --depth=1 https://github.com/stsaz/ffos
 git clone --depth=1 https://github.com/stsaz/avpack
 git clone --depth=1 https://github.com/stsaz/fmedia
 ```
 
You should have all 5 folders downloaded.

![image](https://user-images.githubusercontent.com/32961763/179781524-2620393c-9565-453d-b8b6-6076c561fc1e.png)

8. Download the windows makefile patch from here: [ffos-makeconf.patch.txt](https://github.com/ITCMD/fmedia/files/9142245/ffos-makeconf.patch.txt) and save it into the ffoss directory.
![image](https://user-images.githubusercontent.com/32961763/179781851-e5fddad6-cc75-47a3-8d61-08280dd383f4.png)

9. Open a command prompt window **in the ffos directory** and run the following command:
`"C:\Program Files\Git\usr\bin\patch.exe" -p1 < ffos-makeconf.patch.txt`
![image](https://user-images.githubusercontent.com/32961763/179782224-44e906c1-c8e2-45d6-a95e-69dbe8009312.png)

Now, make any changes you wish to the code, and we're all set to compile it!
10. To compile the code into an executable, open a command prompt window **in the fmedia directory** and run the following command:
`mingw32-make.exe OS=win fmedia.exe`
![image](https://user-images.githubusercontent.com/32961763/179782625-81aa4997-2fbd-4fd4-aa68-8a10dede706a.png)

You will now have the fmedia.exe file compiled and ready to go! Make sure you include the following files for fmedia to work properly:
```
mod (folder which includes conversion files)
fmedia.conf
help.txt (and help_**.exe for any other languages)
readme.txt
```

## CONFIG

The global configuration file `fmedia.conf` is located within the fmedia directory itself.  It contains all supported settings and their default values.  You must restart fmedia after you make changes to this file.

Additional settings may be stored in file `fmedia-ext.conf`.  This makes it easier to upgrade fmedia without the need to edit `fmedia.conf`.

Per-user configuration settings are also supported, they must be stored in `fmedia-user.conf` file in home directory:

	Windows: %APPDATA%/fmedia/fmedia-user.conf
	Linux:   $HOME/.config/fmedia/fmedia-user.conf

Settings for a module must be in format "so.module.key value", e.g. to overwrite the global setting for OGG Vorbis encoding quality you should write:

	vorbis.encode.quality "7.0"

Core configuration settings start with "core.", e.g. set codepage for non-Unicode text:

	core.codepage win1252


## TERMINAL UI

By default fmedia runs with a terminal UI, which shows information about the currently playing audio track and the currently playing audio position.  User commands such as seeking are also supported, all supported commands are described in file "help-tui.txt".


## GRAPHICAL UI

To run fmedia in GUI mode (Windows and Linux) you may execute the console binary like this:

	fmedia --gui

Or use this special executable file (Windows only):

	fmedia-gui.exe

You should use this binary file for opening files via Explorer's "Open With..." feature.
Note: command-line options are not supported.

fmedia GUI is highly customizable, thanks to FF library that is used under the hood.
FF UI technology allows you to modify properties of every UI control: windows, buttons, menus, tray icons and more.
You may resize controls, set different styling, change any text, hotkeys, etc.
All this information is stored within `fmedia.gui` which is a plain text file.
After you make some changes in `fmedia.gui`, save it and then restart fmedia.

By default fmedia GUI saves its state in file `%APPDATA%\fmedia\fmedia.gui.conf`.
You can change this by setting `portable_conf` to `true` in `fmedia.conf`.
After that, `fmedia.gui.conf` will be stored in program directory (e.g. `C:\Program Files\fmedia\fmedia.gui.conf`), thus making fmedia completely portable.


## EXTRACT TRACKS FROM flac.cue

While extracting a track from the album in FLAC using CUE sheet, the audio is first decoded to PCM and then re-encoded with FLAC.  This behaviour won't result in any audio quality loss since FLAC is a lossless codec.


## USE-CASES

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
	fmedia ./file.wav --out=./file.ogg --vorbis-quality=7.0
	fmedia ./file.wav --out=./file.mp3 --mpeg-quality=0 --rate=48000

Convert all .wav files from the current directory to .ogg

	fmedia ./*.wav --out=.ogg

Convert file and override meta info

	fmedia ./file.flac --out=.ogg --meta='artist=Artist Name;comment=My Comment'

Extract several tracks from .cue file

	fmedia ./album.flac.cue --track=3,7,13 --out='$tracknumber. $artist - $title.flac'

Split audio file

	fmedia ./file.wav --seek=00:35 --until=01:35 --out=./file-1.wav
	fmedia ./file.wav --split=01:00 -o 'file-$counter.wav'

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

Record from playback or "record what you hear" (Windows/WASAPI only)

	fmedia --dev-loopback=1 --record --out=./rec.wav

Record from playback AND record from microphone in parallel into 2 different files (Windows/WASAPI only)

	fmedia --dev-loopback=1 --dev-capture=1 --record --out='./rec-$counter.wav'

Record while playing

	fmedia ./file.ogg --record --out=./rec.wav

Live output

	fmedia --record

Record audio from Internet radio (without re-encoding)

	fmedia http://radio-stream:80/ -o ./radio.mp3 --stream-copy

Play AND record audio from Internet radio into separate files (without re-encoding)

	fmedia http://radio-stream:80/ --out-copy -o './$time. $artist - $title.mp3' --stream-copy

### EDIT TAGS

Modify file's meta tags in-place

	fmedia --edit-tags --meta='artist=ARTIST;title=TITLE' ./file.mp3

Set artist, track number and title meta tags from file name

	fmedia --edit-tags --meta-from-filename='$artist - $tracknumber. $title' './Cool Artist - 04. Best Song.mp3'

### OTHER FUNCTIONS

Print audio meta info

	fmedia --info ./file.mp3

Print audio meta info and all tags

	fmedia --info --tags ./file.mp3

Show PCM information

	fmedia input.ogg --pcm-peaks

Create a playlist file from directory:

	fmedia ./Music -o music.m3u8


## FOR DEVELOPERS

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

See fmedia source code for more details.
For example, `main.c::main()` will show you how fmedia command line binary initializes core module.
See `src/format/wav.c` for an example on how to write a simple filter for fmedia.


### LOW-LEVEL INTERFACE

If you'd like to use low level interfaces, take a look at the source code of FF libraries.  Together they provide you with an easy interface that you can use to work with a large set of file formats, decode or encode audio and much more.  fmedia itself is built upon FF library - it's completely free and open-source.


### PARTICIPATE

You are welcome to participate in fmedia's development.  Send suggestions, improvements, bug reports, patches - anything that can help the project!

Understanding the top-level source code hierarchy can help you to get involved into fmedia quicker.  The source code consists of these separate repositories:

	------------------------
	    fmedia
	------------------------
	 ffos, avpack, ffaudio
	------------------------
	    ffbase
	------------------------

Each of them plays its own part:

* ffbase provides base containers and algorithms
* ffaudio provides audio I/O
* ffos provides cross-platform abilities.  Code based on ffos can run on Windows, Linux and FreeBSD.
* avpack provides API for reading/writing audio-video container formats, e.g. ".mp4".
* fmedia contains application code, it's largely based on all FF libraries.


## BUG REPORT

If you encounter a bug, please report it: the more issues will be reported by users, the more stable fmedia will become.  When filing a bug report try to provide information that can help us to fix the problem.  Try to execute the same command once again, only this time add --debug switch, e.g.:

	fmedia --debug OPTIONS INPUT_FILES...

It will print a lot of information about what fmedia is doing.  This info or a screenshot would be very helpful.


## HOMEPAGE

https://stsaz.github.io/fmedia/
