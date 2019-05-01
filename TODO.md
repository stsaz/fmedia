# To-do list

This is the list of the things that need to be done.

## Features

* .ts
* .mkv, .avi seeking
* 24-bit conversion via soxr
* conversion for >2 channels
* gapless playback of the next track in queue
* noise gate filter
* ICY: detect audio format in case of unknown content type, .e.g. "Content-Type: application/octet-stream"
* support --meta with --stream-copy (.ogg, .m4a, .mp3)
* GUI: Open directory from disk
* GUI: Ctrl+Tab
* GUI: remember column width
* GUI theme: listview selection color
* GUI for macOS
* Linux: Don't allow system to sleep while playing or recording


## Second priority features

* .mkv, .avi: support MPEG delay
* join several files into one
* shuffle playlist
* fatal decoding errors should have filename in their log messages
* PulseAudio input
* ALSA: Add fallback path (using fmedia timer) in case "snd_async_add_pcm_handler(): (-38) Function not implemented"


## Doubtful features or "need more info"

* Recording: dynamically adjust Windows audio volume slider if the input level is too quiet or too loud
* Windows: register file associations
* Playback: insert a properly timed audio block with silence in case audio frame is corrupted, instead of just skipping it.  As a result, the total output samples will stay exactly the same.
* ICY: while recording from ICY, split output files by time chunks
* ICY: reset time on a new song
* ICY: Start/stop recording by 'T' command (arbitrary, without meta)
* ICY: Recording: Add several seconds of audio to the beginning of the new track which is being recorded, to compensate for inaccurate ICY meta change
* TUI: Windows: determine terminal window width and adjust playbar (CONSOLE_SCREEN_BUFFER_INFO)
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
* FreeBSD: segfault on playback after quick input ('N') in TUI?
* GUI: Invert Sel doesn't work
* delayed module load doesn't work if its settings are specified in fmedia-user.conf


## Refactoring

* "mpeg.copy" can be replaced by "mpeg.in" -> "mpeg.out" chain?
* file: AIO write
* move GUI icons from *.exe to gui.dll
