---------------
OVERVIEW
---------------
fmedia is a fast asynchronous media player/recorder/converter for Windows (Linux/ALSA version is being tested now and it will be available in the next releases).  Its goal is to provide smooth playback even if input device is very slow and unresponsive.  The architecure allows to extend the functionality of the application in any way: adding a new audio input/output format, a new DSP filter or even a new GUI.  fmedia is very small and fast: it has low CPU & memory consumption making it ideal to listen to music or process audio files while running on a notebook's battery.

Note: it's beta version - not tested well enough, not all functions will work as expected.  See section "USE-CASES" to have an idea of which features should work.

---------------
MODULES
---------------
All features are provided by fmedia modules divided into 3 groups:

INPUT
	. File
	. WASAPI Capture
	. Windows Direct Sound Capture

FILTERS
	. MPEG input
	. OGG Vorbis input/output.  Note: OGG file positioning isn't precise.

	Lossless:
	. FLAC input/output
	. WavPack input
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

---------------
INSTALL
---------------
1. Unpack archive to the directory of your choice, e.g. to "C:/Program Files/fmedia"

2. Optionally, add the path into the system environment:

	set PATH=%PATH%;C:/Program Files/fmedia

---------------
EXTRACT TRACKS FROM flac.cue
---------------
If you're extracting a track from the album using CUE sheet, please note, that a track isn't exactly copied but first decoded to PCM and then re-encoded with FLAC.  Luckily, this behaviour won't result in any audio quality loss since FLAC is a lossless codec.  However, fmedia doesn't support pictures (album covers) in FLAC header, so they won't be copied into the output file.

---------------
GRAPHICAL UI
---------------
If you specify '--gui' argument to fmedia binary, it will run in GUI mode:
	fmedia --gui

fmedia GUI is provided by a separate module - gui.dll.  It is written in such a way that it won't become unresponsive even if the main thread (the one that processes audio) is waiting for I/O operations.  The module doesn't perform any sound processing, it just issues commands to fmedia core and receives the feedback.

fmedia GUI is highly customizable, thanks to FF library that is used under the hood.  FF UI technology allows you to modify properties of every UI control: windows, buttons, menus, tray icons and more.  You may resize controls, set different styling, change any text, hotkeys, etc.  All this information is stored within "fmedia.gui" which is a plain text file.  After you make some changes in fmedia.gui, save it and then restart fmedia.

---------------
USE-CASES
---------------
Play
	fmedia ./file.ogg

Convert
	fmedia ./file.ogg --out=./file.wav
	fmedia ./file.wav --out=./file.ogg --ogg-quality=7.0
	fmedia ./*.wav --out=.ogg --outdir=.

Mix multiple streams
	fmedia --mix ./file1.ogg ./file2.ogg

Record while playing
	fmedia ./file.ogg --record --out=./rec.wav

Live output
	fmedia --record

Get audio meta info
	fmedia --info ./file.mp3

Get audio meta info and all tags
	fmedia --info --debug ./file.mp3

Split audio file
	fmedia ./file.wav --seek=00:35 --until=01:35 --out=./file-1.wav

Extract one track from flac.cue
	fmedia ./album.flac.cue --track=3 --out=./track3.flac

Play wav file with a corrupted header
	fmedia ./file.raw --fseek=44

Change sound volume in an audio file
	fmedia --gain=5.0 ./file.wav --out=./file-loud.wav

Show PCM information
	fmedia input.ogg --pcm-peaks

---------------
HOMEPAGE
---------------
http://fmedia.firmdev.com/
