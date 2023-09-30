**Next Major Version (beta)**

Hi!
I invite you to participate in beta-testing process of a new major version.

The main improvement is the revised command-line interface that will eliminate ambiguity with over 50 (as of `v1.31`) command-line options that allow hundreds of use-cases without any real value (in such cases, the options are either silently ignored or can result in an unexpected behaviour).  Obviously, it's necessary to split different use-cases over different commands, just like `git` or `docker` CLI executables do.  So I hope the new CLI will find support among current fmedia users as well as assist new users who are trying to use CLI for audio files manipulation for the first time.

The second improvement is for the developers who want to use fmedia as a library that provides complete solution for audio playback/recording/conversion with plugin support.  Both C and Java interfaces are now much easier to use, and it's also very easy to insert some custom audio filters anywhere into the processing chain.  Furthermore, I merged main/core code for desktop OS and Android, proving that the SDK is correctly designed and really works as it should.

The third improvement is that it is no longer required to have an explicit configuration file (i.e. `fmedia.conf`) for all functions to work correctly - all supported settings can be specified via command-line.  And of course, just as before, all plugins are loaded dynamically on demand, and they don't affect the startup time.

Note however, that not all current features are included in the new beta.  For now, I decided to leave out some rarely used functions to minimize the package size, until it becomes clear that the omitted function is really necessary.

So, if you're interested, please visit the project page and build or download the latest package: [phiola](https://github.com/stsaz/phiola/).
Due to the fact that it's *beta* version, there may be some bugs.
That's why your feedback is very important, and together we can make this small tool better.


## OVERVIEW

<img align="right" src="res/fmedia.png" width="128" height="128">

fmedia is a fast audio player/recorder/converter for Windows, macOS, Linux, FreeBSD, Android`*`.
It provides smooth playback and recording even if devices are very slow.
It's highly customizable and can be easily extended with additional plugins.
Its low CPU & memory consumption saves energy when running on a notebook's battery.

Play or convert audio files, record new audio tracks from microphone, save songs from Internet radio, and much more!
fmedia is free and open-source project, and you can use it as a standalone application or as a library for your own software.

**fmedia can read**: .mp3, .ogg (Vorbis/Opus), .opus, .mp4/.m4a/.mov (AAC/ALAC/MPEG), .mka/.mkv/.webm (AAC/ALAC/MPEG/Vorbis/Opus/PCM), .caf (AAC/ALAC/PCM), .avi (AAC/MPEG/PCM), .aac, .mpc, .flac, .ape, .wv, .wav;  .m3u, .pls, .cue.

**fmedia can write**: .mp3, .ogg, .opus, .m4a (AAC), .flac, .wav, .aac (--stream-copy only).

`*` fmedia/Android is currently far behind on features compared to the full-functional desktop-version.

### Contents:

* [Features](#features)
	* [fmedia/Android features](#fmedia-android-features)
 * [Install](#install)
	* [Install On Windows](#install-on-windows)
	* [Install On Linux](#install-on-linux)
* [Build](#build)
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

fmedia uses modified versions of these 3rd party libraries: libALAC, libfdk-aac, libFLAC, libMAC, libmp3lame, libmpg123, libmpc, libogg, libopus, libsoxr, libvorbisenc, libvorbis, libwavpack, libDynamicAudioNormalizer, libzstd.  See contents of `alib3/` for more info.


### fmedia/Android features

Currently implemented features:

* Playback: .m4a, .mp3, .flac, .ogg, .opus (depends on OS)
* Recording: .m4a(AAC), .flac
* Convert (decode): .mp3, .mp4/.m4a(AAC-LC,ALAC), .flac
* Convert (encode): .m4a(AAC-LC), .flac
* Convert (stream copy): .mp3, .m4a
* GUI: list of meta tags
* GUI: file explorer
* GUI: 2 playlists


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

		mkdir -p ~/bin
		tar Jxf ./fmedia-1.0-linux-amd64.tar.xz -C ~/bin

2. Optionally, create a symbolic link:

		ln -s ~/bin/fmedia-1/fmedia ~/bin/fmedia

3. Optionally, add fmedia GUI icon to KDE Applications:

		cp ~/bin/fmedia-1/fmedia.desktop ~/.local/share/applications

	Then edit `Exec=` and `Icon=` rows in `~/.local/share/applications/fmedia.desktop` if necessary.

4. Run `fmedia --gui` to open graphical interface;  or execute commands via `fmedia` from console.


## BUILD

Read [fmedia Build Instructions](https://github.com/stsaz/fmedia/blob/master/BUILDING.md).


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

By default fmedia runs with a terminal UI, which shows information about the currently playing audio track and the currently playing audio position.  Hot keys are also supported, the most commonly used are:

* `Space` for "Play/Pause"
* `Right Arrow`/`Alt+Right Arrow`/`Ctrl+Right Arrow` to seek forward
* `n` to play the next track
* `s` to stop playback
* `q` to quit fmedia
* `h` to show all supported commands


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
