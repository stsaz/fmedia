---------------
OVERVIEW
---------------
fmedia is a fast asynchronous media player/recorder/converter for Windows (Linux/ALSA version is being tested now and it will be available in the next releases).  Its goal is to provide smooth playback even if input device is very slow and unresponsive.  The architecure allows to extend the functionality of the application in any way: adding a new audio input/output format, a new DSP filter or even a new GUI.  fmedia is very small and fast: it has low CPU & memory consumption making it ideal to listen to music or process audio files while running on a notebook's battery.

Note: it's beta version - not tested well enough, there's no even interactive UI.  See section "USE-CASES" to have an idea of which features should work.

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
	. WAV input/output
	. RAW input

	Other:
	. PCM converter
	. Mixer
	. Terminal UI

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

Play wav file with a corrupted header
	fmedia ./file.raw --fseek=44

Change sound volume in an audio file
	fmedia --gain=5.0 ./file.wav --out=./file-loud.wav

---------------
HOMEPAGE
---------------
http://fmedia.firmdev.com/
