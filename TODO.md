# To-do list

This is the list of the things that need to be done.

## Features

* .ts
* .avi seeking
* 24-bit conversion via soxr
* gapless playback of the next track in queue
* noise gate filter
* JACK playback
* ICY: detect audio format in case of unknown content type

		"unsupported Content-Type: application/octet-stream"

* ICY: detect real bitrate from data (not HTTP header)
* support --meta with --stream-copy (.ogg, .m4a, .mp3)
* GUI: Open directory from disk
* GUI: Ctrl+Tab
* GUI theme: listview entry selection color (#13)
* GUI for macOS
* GUI: remove meta associated with the track after file is renamed
* GUI: read tags on load
* GUI: remember conversion settings
* show tray icon when recording from console (#39)
* translate help.txt (#15)
* Set to Play Next
* build for ARM (#3)
* open http://....m3u (application/x-mpegURL)
* wasapi: process AUDCLNT_E_DEVICE_INVALIDATED
* Modify audio tags in-place

		>fmedia --tags --meta='artist=NewArtist' ./file.mp3

## Second priority features

* .mkv, .avi: support MPEG delay
* join several files into one
* ALSA: Add fallback path (using fmedia timer) in case "snd_async_add_pcm_handler(): (-38) Function not implemented"
* docker build (#42)


## Doubtful features or "need more info"

* Recording: dynamically adjust Windows audio volume slider if the input level is too quiet or too loud
* Windows: register file associations
* Playback: insert a properly timed audio block with silence in case audio frame is corrupted, instead of just skipping it.  As a result, the total output samples will stay exactly the same.
* ICY: while recording from ICY, split output files by time chunks
* ICY: reset time on a new song
* ICY: Start/stop recording by 'T' command (arbitrary, without meta)
* ICY: Recording: Add several seconds of audio to the beginning of the new track which is being recorded, to compensate for inaccurate ICY meta change
* TUI: Linux: determine terminal window width and adjust playbar
* find and split tracks from one large file
* MPEG decode: must report the offset where the invalid data begins
* Recording: Wait (don't finalize) until the active capture buffer is flushed (Otherwise the last recorded milliseconds are not written to file)
* filter: cutoff frequency


## Bugs

* --stream-copy (.ogg, .mp3, .m4a) can produce files with audio length which is less than expected (with or without --seek/--until)?
* .ogg file produced by --stream-copy have incorrect granule position (1 - total samples by "fmedia -i" can be incorrect;  2 - other players may not support it)
* .ogg: --seek near the end won't work because there's no next OGG page to get granule position from
* "Stop" command can't immediately break the track loop if it's hanging?
* .wv: ID3v1 tags have higher priority than APE tags
* "fmedia --record --channels=left" records in mono, but is it really left channel?
* GUI: Invert Sel doesn't work
* delayed module load doesn't work if its settings are specified in fmedia-user.conf
* gui-gtk: too large control buttons
* --rate= --record doesn't apply to input format, but to conversion
* gui convert: file format write module returned error, but there's "done" status for the entry (should be "error")
* wasapi: doesn't play 8bit file?
* crash in

		addfilter1(t, core->props->record_module);

	record_module = NULL
	because there's no input module specified in config

		# input "wasapi.in"
		# input "direct-sound.in"

* fmedia --mix 1.wav thr-30.wav 2.wav --record --out=./m.wav --debug
* Must read meta (mp3.in) before file.out

		>fmedia "06 - Name.mp3" --stream-copy -o $artist.mp3 --seek=4:0
		saved file .mp3

* the audio is corrupt if saved as "mono"

		--out=Stream.mp3 --mpeg-quality=64 --rate=44100 --channels=mono

		>fmedia --record --channels=mono -o 1.wav -y
		>fmedia ./1.wav -o 1.mp3 -y --channels=mono

* AAC unsync on reconnect

		"http://...": I/O timeout
		resolving host ...
		connecting to ...
		aac.decode: *1:    "http://...": ffaac_decode(): (400a) AAC_DEC_UNSUPPORTED_GAIN_CONTROL_DATA: Gain control data found but not supported. Most probably the bitstream is corrupt, or has a wrong format.
		aac: *1:   "http://...": ffaac_adts_read(): lost synchronization.  Offset: ...

* mp3 unsync

		./fmedia 1.mp3 -o 1.wav --until=5
		./fmedia 1.wav --pcm-peaks
			0:04.998 (220,453 samples)

* mp3 stream-copy produces inaccurate results (02.469 instead of 02.500)
  "--meta" affects audio length!

		>fmedia classic.mp3  --stream-copy --seek=2.500 -y -o classic-copy.mp3

		" - " classic.mp3 0.11 MB, 0:05.000 (220,500 samples), 191 kbps, MPEG, 44100 Hz, int16, stereo

		[========================================================..............] 0:04 / 0:0510:49:28.875 info mpeg.copy: *1:
		MPEG: frames:95

		saved file classic-copy.mp3, 58 kbytes

		>fmedia classic-copy.mp3 --pcm-peaks

		"Antonio Vivaldi - Sinfonia in C major - Allegro molto" classic-copy.mp3 0.06 MB, 0:02.469 (108,911 samples), 189 kbps,
		MPEG, 44100 Hz, float32, stereo

		[=========================================================.............] 0:02 / 0:02

		PCM peaks (108,911 total samples):
		Channel #1: highest peak:-7.76dB, avg peak:-22.63dB.  Clipped: 0 (0.0000%).  CRC:00000000
		Channel #2: highest peak:-4.52dB, avg peak:-20.49dB.  Clipped: 0 (0.0000%).  CRC:00000000

		>fmedia classic.mp3  --stream-copy --seek=2.500 -y --meta=artist=A;title=T -o classic-copy.mp3

		"A - T" classic.mp3 0.11 MB, 0:05.000 (220,500 samples), 190 kbps, MPEG, 44100 Hz, int16, stereo

		[========================================================..............] 0:04 / 0:05
		saved file classic-copy.mp3, 59 kbytes

		>fmedia classic-copy.mp3 --pcm-peaks

		"A - T" classic-copy.mp3 0.06 MB, 0:02.480 (109,368 samples), 192 kbps, MPEG, 44100 Hz, float32, stereo

		[========================================================..............] 0:02 / 0:02

		PCM peaks (111,744 total samples):
		Channel #1: highest peak:-7.76dB, avg peak:-22.70dB.  Clipped: 0 (0.0000%).  CRC:00000000
		Channel #2: highest peak:-4.52dB, avg peak:-20.59dB.  Clipped: 0 (0.0000%).  CRC:00000000

		>./fmedia 1.mp3 --stream-copy -o 2.mp3 --until=2
			0:01.999 (88,175 samples)

* net.icy: can't stop while "precaching data..."
* can't stop running convert with ctrl+C in sync file IO mode

		>fmedia rock.flac -o 1.ogg

* fmedia a.wav --mono=0
	doesn't work in wasapi shared mode because it requires 2 channel input, therefore, conversion is 2ch -> 2ch, i.e. the same sound.

* when seeking, wasapi module returns RMORE after snd_output_clear flag is tested and cleared
	the sound will stop until the next block is found(!) by demuxer, decoded and only then pass to wasapi again
	a better approach will be to do this stuff while playing the old buffers (those that are already in wasapi buffers)
	furthermore, if the control is within file module (aio reading) while we issue the SEEK command from tui, the first time we pass new valid data to wasapi, it will just skip it, returning RMORE on snd_output_clear flag!

* GUI: stop keyboard arrows acting on a focused track bar

* gui: starting fmedia with input file adds this file into beginning of the list (before auto-saved list1.m3u)
	that's bcs qu->cmd(FMED_QUE_PLAY, first) is issued from fmedia-gui.c before saved playlist is added and expanded from gui module

* gui: Record command and Select Device (Capture) command must respect user's config setting (similar to core->props->playback_dev_index)

* hanging
	. add file to list
	. set Random or Repeat All feature
	. delete file from disk
	. start the file

	It will run and fail with "file doesn' exist" error.
	But 'queue' will add the same file again via core.task().
	'Stop' pressed by user sends STOP_ALL, but it does nothing useful
	 because the start of the next track is already scheduled by 'queue'.


## Refactoring

* "mpeg.copy" can be replaced by "mpeg.in" -> "mpeg.out" chain?
* move GUI icons from *.exe to gui.dll
