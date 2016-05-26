---------------
OVERVIEW
---------------
fmedia is a fast asynchronous media player/recorder/converter for Windows and Linux.  Its goal is to provide smooth playback even if input device is very slow and unresponsive.  The architecure allows to extend the functionality of the application in any way: adding a new audio input/output format, a new DSP filter or even a new GUI.  fmedia is very small and fast: it has low CPU & memory consumption making it ideal to listen to music or process audio files while running on a notebook's battery.

Note: it's beta version - not tested well enough, not all functions will work as expected.  See section "USE-CASES" to have an idea of which features should work.

Contents:
	. MODULES
	. INSTALL ON WINDOWS
	. INSTALL ON LINUX
	. BUILD ON LINUX
	. CONFIG
	. EXTRACT TRACKS FROM FLAC.CUE
	. TERMINAL UI
	. GRAPHICAL UI
	. USE-CASES
	. BUG REPORT


---------------
MODULES
---------------
All features are provided by fmedia modules divided into 3 groups:

INPUT
	. File
	. ICY
	. WASAPI Capture
	. Windows Direct Sound Capture
	. ALSA Capture

FILTERS
	. MPEG input/output
	. OGG Vorbis input/output
	. AAC input

	Lossless:
	. FLAC input/output
	. ALAC input
	. WavPack input
	. APE input
	. WAV input/output
	. RAW input

	Playlists:
	. M3U input
	. CUE input
	. Directory input

	Other:
	. PCM converter
	. PCM peaks
	. Mixer
	. Terminal UI
	. Graphical UI

OUTPUT
	. File
	. Windows Direct Sound Playback
	. WASAPI Playback
	. ALSA Playback

---------------
INSTALL ON WINDOWS
---------------
1. Unpack archive to the directory of your choice, e.g. to "C:\Program Files\fmedia"

2. Optionally, run the following command (from console):

	"C:\Program Files\fmedia\fmedia.exe" --install

	This command will:
	. add fmedia directory into user's environment
	. create a desktop shortcut to fmedia-gui.exe

---------------
INSTALL ON LINUX
---------------
1. Unpack archive to the directory of your choice, e.g. to "/usr/local/fmedia-0":

	tar Jxf ./fmedia-0.8-linux-amd64.tar.xz -C /usr/local

2. Optionally, create a symbolic link:

	ln -s /usr/local/fmedia-0/fmedia /usr/local/bin/fmedia

Note: the file "fmedia-0/fmedia" is just a script that executes binary file "fmedia-0/fmedia-bin" with proper environment.  If the script doesn't work for some reason, call fmedia-bin directly:

	env LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/fmedia-0 /usr/local/fmedia-0/fmedia-bin

---------------
BUILD ON LINUX
---------------
1. Create a directory for all needed sources:

	mkdir firmdev && cd firmdev

2. Download all needed source repositories:

	git clone https://github.com/stsaz/ffos
	git clone https://github.com/stsaz/ff
	wget http://firmdev.com/ff-3pt.tar.xz
	tar Jxf ff-3pt.tar.xz

	git clone https://github.com/stsaz/fmedia

3. Build

	cd fmedia
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ff-3pt/linux-amd64
	make install

4. Ready!  You can copy the directory ./fmedia-0 anywhere you want (see section "INSTALL ON LINUX").

---------------
CONFIG
---------------
The global configuration file "fmedia.conf" is located within the fmedia directory itself.  It contains all supported settings and their default values.  You must restart fmedia after you make changes to this file.

Per-user configuration settings are also supported, they must be stored in "fmedia.conf" file in home directory:
 Windows: %APPDATA%/fmedia/fmedia.conf
 Linux:   $HOME/.config/fmedia/fmedia.conf

Settings for a module must be in format "so.module.key value", e.g. to overwrite the global setting for OGG Vorbis encoding quality you should write:

	ogg-vorbis.encode.quality "7.0"

Core configuration settings start with "core.", e.g. set codepage for non-Unicode text:

	core.codepage win1252

---------------
EXTRACT TRACKS FROM flac.cue
---------------
If you're extracting a track from the album using CUE sheet, please note, that a track isn't exactly copied but first decoded to PCM and then re-encoded with FLAC.  Luckily, this behaviour won't result in any audio quality loss since FLAC is a lossless codec.  However, fmedia doesn't support pictures (album covers) in FLAC header, so they won't be copied into the output file.

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

Note: GUI isn't supported on Linux.

fmedia GUI is provided by a separate module - gui.dll.  It is written in such a way that it won't become unresponsive even if the main thread (the one that processes audio) is waiting for I/O operations.  The module doesn't perform any sound processing, it just issues commands to fmedia core and receives the feedback.

fmedia GUI is highly customizable, thanks to FF library that is used under the hood.  FF UI technology allows you to modify properties of every UI control: windows, buttons, menus, tray icons and more.  You may resize controls, set different styling, change any text, hotkeys, etc.  All this information is stored within "fmedia.gui" which is a plain text file.  After you make some changes in fmedia.gui, save it and then restart fmedia.


---------------
USE-CASES
---------------
Play files, directories, Internet-radio streams
	fmedia ./file.ogg ./*.mp3
	fmedia ./Music
	fmedia http://radio-stream:80/

Convert
	fmedia ./file.ogg --out=./file.wav
	fmedia ./file.wav --out=./file.ogg --ogg-quality=7.0
	fmedia ./file.wav --out=./file.mp3 --mpeg-quality=0 --rate=48000

Convert all .wav files from the current directory to .ogg
	fmedia ./*.wav --out=.ogg --outdir=.

Convert file and override meta info (Use single quotes on Linux, double quotes on Windows)
	fmedia ./file.flac --out=.ogg --meta='artist=Artist Name;comment=My Comment'

Extract several tracks from .cue file (Use single quotes on Linux, double quotes on Windows)
	fmedia ./album.flac.cue --track=3,7,13 --out='$tracknumber. $artist - $title.flac'

Cut compressed audio without re-encoding
	fmedia ./file.ogg --out=./out.ogg --seek=1:00 --until=2:00

Mix multiple streams
	fmedia --mix ./file1.ogg ./file2.ogg

Record
	fmedia --record --out=rec.flac

Record for 60 seconds then stop
	fmedia --record --out=rec.flac --until=60

Record while playing
	fmedia ./file.ogg --record --out=./rec.wav

Live output
	fmedia --record

Print audio meta info
	fmedia --info ./file.mp3

Print audio meta info and all tags
	fmedia --info --tags ./file.mp3

Split audio file
	fmedia ./file.wav --seek=00:35 --until=01:35 --out=./file-1.wav

Play wav file with a corrupted header
	fmedia ./file.raw --fseek=44

Change sound volume in an audio file
	fmedia --gain=5.0 ./file.wav --out=./file-loud.wav

Show PCM information
	fmedia input.ogg --pcm-peaks


---------------
BUG REPORT
---------------
If you encounter a bug, please report it: the more issues will be reported by users, the more stable fmedia will become.  When filing a bug report try to provide information that can help us to fix the problem.  Try to execute the same command once again, only this time add --debug switch, e.g.:

	fmedia --debug OPTIONS INPUT_FILES...

It will print a lot of information about what fmedia is doing.  This info or a screenshot would be very helpful.


---------------
HOMEPAGE
---------------
http://fmedia.firmdev.com/
