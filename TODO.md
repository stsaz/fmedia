# To-do list

This is the list of the things that need to be done.

* Features
* Bugs
* Refactoring
* Android

## Features

* .ts
* .mkv(FLAC)
* accurate .mkv seeking
* seeking: .avi .caf .mpc
* vorbis.mkv --stream-copy
* 24-bit conversion via soxr
* gapless playback of the next track in queue
* noise gate filter
* JACK playback
* support --meta with --stream-copy (.ogg, .m4a, .mp3)

* net:
	* ICY: detect audio format in case of unknown content type

			"unsupported Content-Type: application/octet-stream"

	* ICY: detect real bitrate from data (not HTTP header)
	* open http://....m3u (application/x-mpegURL)
	* HTTPS URLs
	* http client: set Cookie:

			http://stream.site.com/...

			HTTP/1.0 302 Found
			Location: http://***.site.com/...
			Set-Cookie: key=val; Domain=site.com

			GET /... HTTP/1.1
			Host: ***.site.com
			Cookie: ...

* GUI:
	* Open directory from disk
	* Ctrl+Tab
	* theme: listview entry selection color (#13)
	* for macOS
	* remove meta associated with the track after file is renamed
	* read tags on load
	* remember conversion settings
	* show tray icon when recording from console (#39)
	* Set to Play Next

## Second priority features

* .mkv, .avi: support MPEG delay
* join several files into one
* ALSA: Add fallback path (using fmedia timer) in case "snd_async_add_pcm_handler(): (-38) Function not implemented"

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

* AAC unsync on reconnect

		"http://...": I/O timeout
		resolving host ...
		connecting to ...
		aac.decode: *1:    "http://...": ffaac_decode(): (400a) AAC_DEC_UNSUPPORTED_GAIN_CONTROL_DATA: Gain control data found but not supported. Most probably the bitstream is corrupt, or has a wrong format.
		aac: *1:   "http://...": ffaac_adts_read(): lost synchronization.  Offset: ...

* net.icy: can't stop while "precaching data..."
* can't stop running convert with ctrl+C in sync file IO mode

		>fmedia rock.flac -o 1.ogg

* fmedia a.wav --mono=0
	doesn't work in wasapi shared mode because it requires 2 channel input, therefore, conversion is 2ch -> 2ch, i.e. the same sound.

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

* fix ALSA/WASAPI buffer reuse algorithm
* .caf 'data'...'pakt' (alac.caf)
* "play_vorbis.ogg --seek=2": PCM peaks (94,976 total samples)
* "play_mp3.mp3 --seek=2": PCM peaks (94,511 total samples)
* 3kbps AAC-LC
* fix "seek offset 5293016 is bigger than file size 5292032"
* mkv(MP3) seek artifacts (probably because pos > seek and thus mpeg doesnt skip first samples)
* "play_pcm.caf" 0.73MB 0:00.000 (0 samples) 0kbps  int16 48000Hz stereo
* play_aac.avi: always "0:00 / 1:00"

## Refactoring

* "mpeg.copy" can be replaced by "mpeg.in" -> "mpeg.out" chain?
* move GUI icons from *.exe to gui.dll

* FMED_FLAST check requirement for the most filters is strange

		if (d->flags & FMED_FLAST)
			return FMED_RDONE;
		return FMED_RDATA;

	core should handle FMED_RDATA for the last-in-chain filter automatically?

* opus & vorbis still use `input_info`
* setvalstr() leftovers


## Android

	+ Translations
	+ Navigation bar color
	+ Trash on tap Next
	- A11: playback notification: Pause doesn't change the button's icon
