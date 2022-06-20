# fmedia tester

# print commands before executing
set -x

# exit if a child process reports an error
set -e

BIN=./fmedia

if test "$1" = "clean" ; then
	rm *.aac *.wav *.flac *.mp3 *.m4a *.ogg *.opus *.mpc *.wv
	exit
fi

if test "$1" = "radio" ; then
	URL="http://"
	$BIN $URL -o '$artist-$title.mp3' --out-copy --stream-copy -y --meta=artist=A --until=1
fi

if test "$1" = "rec1" ; then
	./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16
fi

if test "$1" = "record" ; then
	# record -> .*
	# TODO --meta=artist=A;title=T
	./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.flac -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.mp3 -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.m4a -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.ogg -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.opus -y --until=2 --rate=44100 --format=int16
	./fmedia rec.* --pcm-peaks
fi

if test "$1" = "info" ; then
	OPTS="--info --tags"
	$BIN rec.* $OPTS
fi

if test "$1" = "play" ; then
	if ! test -f "rec4.wav" ; then
		./fmedia --record --format=int16 --rate=48000 --channels=2 --until=4 -o rec4.wav -y
	fi
	if ! test -f "play_wv.wv" ; then
		ffmpeg -i rec4.wav -c:a aac -y play_aac.aac
		ffmpeg -i rec4.wav -c:a aac -y play_aac.avi
		ffmpeg -i rec4.wav -c:a aac -y play_aac.mkv
		ffmpeg -i rec4.wav -c:a aac -y play_aac.mp4
		# ffmpeg -i rec4.wav -c:a alac -y play_alac.caf
		ffmpeg -i rec4.wav -c:a alac -y play_alac.mkv
		ffmpeg -i rec4.wav -c:a alac -y play_alac.mp4
		ffmpeg -i rec4.wav -c:a flac -y play_flac.flac
		ffmpeg -i rec4.wav -c:a flac -y play_flac.ogg
		ffmpeg -i rec4.wav -c:a libmp3lame -y play_mp3.avi
		ffmpeg -i rec4.wav -c:a libmp3lame -y play_mp3.mkv
		ffmpeg -i rec4.wav -c:a libmp3lame -y play_mp3.mp3
		ffmpeg -i rec4.wav -c:a libopus -y play_opus.mkv
		ffmpeg -i rec4.wav -c:a libopus -y play_opus.ogg
		ffmpeg -i rec4.wav -c:a libvorbis -y play_vorbis.mkv
		ffmpeg -i rec4.wav -c:a libvorbis -y play_vorbis.ogg
		ffmpeg -i rec4.wav -c:a pcm_s16le -y play_pcm.avi
		ffmpeg -i rec4.wav -c:a pcm_s16le -y play_pcm.caf
		ffmpeg -i rec4.wav -c:a pcm_s16le -y play_pcm.mkv
		ffmpeg -i rec4.wav -c:a pcm_s16le -y play_pcm.wav
		ffmpeg -i rec4.wav -c:a wavpack -y play_wv.wv
		# mpcenc rec4.wav play_mpc.mpc
		# TODO play_ape.ape
	fi

	./fmedia play_* --seek=2 --pcm-peaks
	./fmedia play_*
	./fmedia play_* --seek=2
fi

if test "$1" = "cue" ; then
	if ! test -f "rec4.flac" ; then
		./fmedia --record --format=int16 --rate=48000 --channels=2 --until=4 -o rec4.flac -y
	fi
	echo 'FILE "rec4.flac" WAVE
TRACK 01 AUDIO
 TITLE T1
 INDEX 01 00:00:00
TRACK 02 AUDIO
 TITLE T2
 INDEX 01 00:02:00' >cue.cue
	./fmedia cue.cue
fi

if test "$1" = "convert" ; then
	if ! test -f "rec.wav" ; then
		./fmedia --record --format=int16 --rate=48000 --channels=2 --until=2 -o rec.wav -y
	fi
	# convert .wav -> .*
	OPTS="-y"
	$BIN rec.wav -o enc.wav $OPTS
	$BIN rec.wav -o enc.flac $OPTS
	$BIN rec.wav -o enc.mp3 $OPTS
	$BIN rec.wav -o enc.m4a $OPTS
	$BIN rec.wav -o enc.ogg $OPTS
	$BIN rec.wav -o enc.opus $OPTS
	$BIN enc.* --pcm-peaks
	$BIN enc.* --pcm-peaks --seek=0.500

	# convert with seek: .wav -> .*
	OPTS="-y --seek=0.500"
	$BIN rec.wav -o enc-seek.wav $OPTS
	$BIN rec.wav -o enc-seek.flac $OPTS
	$BIN rec.wav -o enc-seek.mp3 $OPTS
	$BIN rec.wav -o enc-seek.m4a $OPTS
	$BIN rec.wav -o enc-seek.ogg $OPTS
	$BIN rec.wav -o enc-seek.opus $OPTS
	$BIN enc-seek.* --pcm-peaks

	# convert with --until: .wav -> .*
	OPTS="-y --until=0.500"
	$BIN rec.wav -o enc-until.wav $OPTS
	$BIN rec.wav -o enc-until.flac $OPTS
	$BIN rec.wav -o enc-until.mp3 $OPTS
	$BIN rec.wav -o enc-until.m4a $OPTS
	$BIN rec.wav -o enc-until.ogg $OPTS
	$BIN rec.wav -o enc-until.opus $OPTS
	$BIN enc-until.* --pcm-peaks

	# convert with rate: .wav -> .*
	OPTS="-y --rate=48000"
	$BIN rec.wav -o enc48.wav $OPTS
	$BIN rec.wav -o enc48.flac $OPTS
	$BIN rec.wav -o enc48.mp3 $OPTS
	$BIN rec.wav -o enc48.m4a $OPTS
	$BIN rec.wav -o enc48.ogg $OPTS
	$BIN rec.wav -o enc48.opus $OPTS
	$BIN enc48.* --pcm-peaks

	# convert with rate & channels .wav -> .*
	OPTS="-y --rate=48000 --channels=mono"
	$BIN rec.wav -o enc48mono.wav $OPTS
	$BIN rec.wav -o enc48mono.flac $OPTS
	$BIN rec.wav -o enc48mono.mp3 $OPTS
	$BIN rec.wav -o enc48mono.m4a $OPTS
	$BIN rec.wav -o enc48mono.ogg $OPTS
	$BIN rec.wav -o enc48mono.opus $OPTS
	$BIN enc48mono.* --pcm-peaks

	# convert with format & rate .wav -> .*
	OPTS="-y --format=int32 --rate=48000"
	$BIN rec.wav -o enc48-32.wav $OPTS
	# TODO $BIN rec.wav -o enc48-32.flac $OPTS
	$BIN rec.wav -o enc48-32.mp3 $OPTS
	$BIN rec.wav -o enc48-32.m4a $OPTS
	$BIN rec.wav -o enc48-32.ogg $OPTS
	$BIN rec.wav -o enc48-32.opus $OPTS
	$BIN enc48-32.* --pcm-peaks

	# convert with format, rate & channels .wav -> .*
	OPTS="-y --format=int32 --rate=48000 --channels=mono"
	$BIN rec.wav -o enc48mono-32.wav $OPTS
	# TODO $BIN rec.wav -o enc48mono-32.flac $OPTS
	$BIN rec.wav -o enc48mono-32.mp3 $OPTS
	$BIN rec.wav -o enc48mono-32.m4a $OPTS
	$BIN rec.wav -o enc48mono-32.ogg $OPTS
	$BIN rec.wav -o enc48mono-32.opus $OPTS
	$BIN enc48mono-32.* --pcm-peaks
fi

if test "$1" = "convert_meta" ; then
	if ! test -f "rec.wav" ; then
		./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16
	fi

	# convert with meta
	./fmedia rec.wav -o enc_meta.flac -y --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia enc_meta.flac --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia rec.wav -o enc_meta.mp3 -y --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia enc_meta.mp3 --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia rec.wav -o enc_meta.m4a -y --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia enc_meta.m4a --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia rec.wav -o enc_meta.ogg -y --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia enc_meta.ogg --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia rec.wav -o enc_meta.opus -y --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia enc_meta.opus --info 2>&1 | grep 'SomeArtist - SomeTitle'

	ffmpeg -i rec.wav -metadata artist='SomeArtist' -metadata title='SomeTitle' -c:a flac -y enc_meta.flac.ogg
	./fmedia enc_meta.flac.ogg --info 2>&1 | grep 'SomeArtist - SomeTitle'

	ffmpeg -i rec.wav -metadata artist='SomeArtist' -metadata title='SomeTitle' -c:a wavpack -y enc_meta.wv
	./fmedia enc_meta.wv --info 2>&1 | grep 'SomeArtist - SomeTitle'

	# mpcenc-bin rec.wav enc_meta.mpc
	# TODO ./fmedia enc_meta.mpc --info 2>&1 | grep 'SomeArtist - SomeTitle'

	# TODO ./fmedia enc_meta.ape --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia enc_meta.* --pcm-peaks --tags

	# convert from a file with meta to the same format
	./fmedia enc_meta.flac -o enc_meta_fmt.flac -y
	./fmedia enc_meta_fmt.flac --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia enc_meta.mp3 -o enc_meta_fmt.mp3 -y
	./fmedia enc_meta_fmt.mp3 --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia enc_meta.m4a -o enc_meta_fmt.m4a -y
	./fmedia enc_meta_fmt.m4a --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia enc_meta.ogg -o enc_meta_fmt.ogg -y
	./fmedia enc_meta_fmt.ogg --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia enc_meta.opus -o enc_meta_fmt.opus -y
	./fmedia enc_meta_fmt.opus --info 2>&1 | grep 'SomeArtist - SomeTitle'
fi

if test "$1" = "convert_parallel" ; then
	# parallel conversion
	OPTS="-y --parallel"
	$BIN rec.* -o 'parallel-$counter.m4a' $OPTS
	$BIN parallel-*.m4a --pcm-peaks --parallel
fi

if test "$1" = "convert_streamcopy" ; then
	# convert with stream-copy
	./fmedia play_aac.mp4 -o copy_aac.m4a -y --stream-copy
	./fmedia play_mp3.mp3 -o copy_mp3.mp3 -y --stream-copy
	./fmedia play_opus.mkv -o copy_opus_mkv.ogg -y --stream-copy
	./fmedia play_opus.ogg -o copy_opus.ogg -y --stream-copy
	./fmedia play_vorbis.ogg -o copy_vorbis.ogg -y --stream-copy
	./fmedia copy_* --pcm-peaks

	# convert with stream-copy and seek
	./fmedia play_aac.mp4 -o copy_seek_aac.m4a -y --stream-copy --seek=2
	./fmedia play_mp3.mp3 -o copy_seek_mp3.mp3 -y --stream-copy --seek=2
	./fmedia play_opus.ogg -o copy_seek_opus.ogg -y --stream-copy --seek=2
	./fmedia play_opus.mkv -o copy_seek_opus_mkv.ogg -y --stream-copy --seek=2
	./fmedia play_vorbis.ogg -o copy_seek_vorbis.ogg -y --stream-copy --seek=2
	./fmedia copy_seek_* --pcm-peaks

	# convert with stream-copy and new meta
	./fmedia play_mp3.mp3 -o copy_meta.mp3 -y --stream-copy --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia copy_meta.mp3 --info 2>&1 | grep 'SomeArtist - SomeTitle'

	./fmedia play_aac.mp4 -o copy_meta.m4a -y --stream-copy --meta='artist=SomeArtist;title=SomeTitle'
	./fmedia copy_meta.m4a --info 2>&1 | grep 'SomeArtist - SomeTitle'
fi

if test "$1" = "filters" ; then
	sh $0 filters_aconv
	sh $0 filters_gain
	sh $0 filters_dynanorm
	OPTS="-y"
	$BIN rec.wav -o 'split-$counter.wav' --split=0.100 $OPTS
	$BIN rec.wav -o 'split-$counter.mp3' --split=0.100 $OPTS
	$BIN split-* --pcm-peaks
fi

if test "$1" = "filters_aconv" ; then
	./fmedia --record --format=int16 --rate=48000 --channels=2 --until=1 -o aconv_rec.wav -y

	# int16 -> int32
	./fmedia aconv_rec.wav --format=int32 -o aconv32.wav -y -D | grep 'PCM conv'
	./fmedia aconv32.wav --pcm-peaks

	# stereo -> mono
	./fmedia aconv_rec.wav --channels=mono -o aconv_mono.wav -y -D | grep 'PCM conv'
	./fmedia aconv_mono.wav --pcm-peaks

	# mono -> stereo
	./fmedia aconv_mono.wav --channels=stereo -o aconv_stereo.wav -y -D | grep 'PCM conv'
	./fmedia aconv_stereo.wav --pcm-peaks

	# int16/48000 -> int32/192000
	./fmedia aconv_rec.wav --format=int32 --rate=192000 -o aconv32-192.wav -y -D | grep 'PCM conv'
	./fmedia aconv32-192.wav --pcm-peaks
fi

if test "$1" = "filters_gain" ; then
	OPTS="-y"
	$BIN rec.wav -o vol.wav --volume=50 $OPTS
	$BIN vol.wav --pcm-peaks
	$BIN rec.wav -o gain.wav --gain=-6.0 $OPTS
	$BIN gain.wav --pcm-peaks
fi

if test "$1" = "filters_dynanorm" ; then
	./fmedia --record --until=2 --dynanorm -o rec-dynanorm.wav -y
	./fmedia rec-dynanorm.wav --pcm-peaks

	./fmedia rec-dynanorm.wav --dynanorm

	./fmedia rec-dynanorm.wav -o dynanorm.wav --dynanorm -y
	./fmedia dynanorm.wav --pcm-peaks
fi

if test "$1" = "all" ; then
	$BIN --list-dev
	sh $0 record
	sh $0 info
	sh $0 play
	sh $0 cue
	sh $0 convert
	sh $0 convert_meta
	sh $0 convert_streamcopy
	sh $0 convert_parallel
	sh $0 filters
fi

echo DONE
