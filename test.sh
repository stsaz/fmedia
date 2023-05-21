#!/bin/bash
# fmedia tester


TESTS_ALL=(
	record info play cue
	convert convert_meta convert_streamcopy convert_parallel
	filters filters_aconv filters_gain filters_dynanorm
	playlist-heal
	)

if test "$#" -lt 1 ; then
	echo Usage: bash test.sh all
	echo Usage: bash test.sh TEST...
	echo "TEST: ${TESTS_ALL[@]}"
	exit 1
fi

# print commands before executing
set -x

# exit if a child process reports an error
set -e

TESTS=("$@")
if test "$1" = "all" ; then
	TESTS=("${TESTS_ALL[@]}")
fi

for CMD in "${TESTS[@]}" ; do

rm -rf fmedtest
mkdir fmedtest

if test "$CMD" = "perf" ; then
	for i in $(seq 1 3) ; do
		./fmedia afile --print-time --pcm-peaks
		./fmedia afile --print-time -o fmedia-test.wav -y --rate=96000 --format=int32
	done

elif test "$CMD" = "clean" ; then
	rm -rf fmedtest
	rm *.aac *.wav *.flac *.mp3 *.m4a *.ogg *.opus *.mpc *.wv *.mp4 *.mkv *.avi *.caf *.cue
	exit

elif test "$CMD" = "radio" ; then
	URL="http://"
	./fmedia $URL -o '$artist-$title.mp3' --out-copy --stream-copy -y --meta=artist=A --until=1

elif test "$CMD" = "rec1" ; then
	./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16

elif test "$CMD" = "record" ; then
	# record -> .*
	# TODO --meta=artist=A;title=T
	./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.flac -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.mp3 -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.m4a -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.ogg -y --until=2 --rate=44100 --format=int16
	./fmedia --record -o rec.opus -y --until=2 --rate=44100 --format=int16
	./fmedia rec.* --pcm-peaks

elif test "$CMD" = "info" ; then
	OPTS="--info --tags"
	./fmedia rec.* $OPTS

elif test "$CMD" = "play" ; then
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

if test "$CMD" = "cue" ; then
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

if test "$CMD" = "convert" ; then
	if ! test -f "rec.wav" ; then
		./fmedia --record --format=int16 --rate=48000 --channels=2 --until=2 -o rec.wav -y
	fi
	# convert .wav -> .*
	OPTS="-y"
	./fmedia rec.wav -o enc.wav $OPTS
	./fmedia rec.wav -o enc.flac $OPTS
	./fmedia rec.wav -o enc.mp3 $OPTS
	./fmedia rec.wav -o enc.m4a $OPTS
	./fmedia rec.wav -o enc.ogg $OPTS
	./fmedia rec.wav -o enc.opus $OPTS
	./fmedia enc.* --pcm-peaks
	./fmedia enc.* --pcm-peaks --seek=0.500

	# convert with seek: .wav -> .*
	OPTS="-y --seek=0.500"
	./fmedia rec.wav -o enc-seek.wav $OPTS
	./fmedia rec.wav -o enc-seek.flac $OPTS
	./fmedia rec.wav -o enc-seek.mp3 $OPTS
	./fmedia rec.wav -o enc-seek.m4a $OPTS
	./fmedia rec.wav -o enc-seek.ogg $OPTS
	./fmedia rec.wav -o enc-seek.opus $OPTS
	./fmedia enc-seek.* --pcm-peaks

	# convert with --until: .wav -> .*
	OPTS="-y --until=0.500"
	./fmedia rec.wav -o enc-until.wav $OPTS
	./fmedia rec.wav -o enc-until.flac $OPTS
	./fmedia rec.wav -o enc-until.mp3 $OPTS
	./fmedia rec.wav -o enc-until.m4a $OPTS
	./fmedia rec.wav -o enc-until.ogg $OPTS
	./fmedia rec.wav -o enc-until.opus $OPTS
	./fmedia enc-until.* --pcm-peaks

	# convert with rate: .wav -> .*
	OPTS="-y --rate=48000"
	./fmedia rec.wav -o enc48.wav $OPTS
	./fmedia rec.wav -o enc48.flac $OPTS
	./fmedia rec.wav -o enc48.mp3 $OPTS
	./fmedia rec.wav -o enc48.m4a $OPTS
	./fmedia rec.wav -o enc48.ogg $OPTS
	./fmedia rec.wav -o enc48.opus $OPTS
	./fmedia enc48.* --pcm-peaks

	# convert with rate & channels .wav -> .*
	OPTS="-y --rate=48000 --channels=mono"
	./fmedia rec.wav -o enc48mono.wav $OPTS
	./fmedia rec.wav -o enc48mono.flac $OPTS
	./fmedia rec.wav -o enc48mono.mp3 $OPTS
	./fmedia rec.wav -o enc48mono.m4a $OPTS
	./fmedia rec.wav -o enc48mono.ogg $OPTS
	./fmedia rec.wav -o enc48mono.opus $OPTS
	./fmedia enc48mono.* --pcm-peaks

	# convert with format & rate .wav -> .*
	OPTS="-y --format=int32 --rate=48000"
	./fmedia rec.wav -o enc48-32.wav $OPTS
	# TODO ./fmedia rec.wav -o enc48-32.flac $OPTS
	./fmedia rec.wav -o enc48-32.mp3 $OPTS
	./fmedia rec.wav -o enc48-32.m4a $OPTS
	./fmedia rec.wav -o enc48-32.ogg $OPTS
	./fmedia rec.wav -o enc48-32.opus $OPTS
	./fmedia enc48-32.* --pcm-peaks

	# convert with format, rate & channels .wav -> .*
	OPTS="-y --format=int32 --rate=48000 --channels=mono"
	./fmedia rec.wav -o enc48mono-32.wav $OPTS
	# TODO ./fmedia rec.wav -o enc48mono-32.flac $OPTS
	./fmedia rec.wav -o enc48mono-32.mp3 $OPTS
	./fmedia rec.wav -o enc48mono-32.m4a $OPTS
	./fmedia rec.wav -o enc48mono-32.ogg $OPTS
	./fmedia rec.wav -o enc48mono-32.opus $OPTS
	./fmedia enc48mono-32.* --pcm-peaks
fi

if test "$CMD" = "convert_meta" ; then
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

if test "$CMD" = "convert_parallel" ; then
	# parallel conversion
	OPTS="-y --parallel"
	./fmedia rec.* -o 'parallel-$counter.m4a' $OPTS
	./fmedia parallel-*.m4a --pcm-peaks --parallel

elif test "$CMD" = "convert_streamcopy" ; then
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

elif test "$CMD" = "filters" ; then
	OPTS="-y"
	./fmedia rec.wav -o 'split-$counter.wav' --split=0.100 $OPTS
	./fmedia rec.wav -o 'split-$counter.mp3' --split=0.100 $OPTS
	./fmedia split-* --pcm-peaks

elif test "$CMD" = "filters_aconv" ; then
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

elif test "$CMD" = "filters_gain" ; then
	OPTS="-y"
	./fmedia rec.wav -o vol.wav --volume=50 $OPTS
	./fmedia vol.wav --pcm-peaks
	./fmedia rec.wav -o gain.wav --gain=-6.0 $OPTS
	./fmedia gain.wav --pcm-peaks

elif test "$CMD" = "filters_dynanorm" ; then
	./fmedia --record --until=2 --dynanorm -o rec-dynanorm.wav -y
	./fmedia rec-dynanorm.wav --pcm-peaks

	./fmedia rec-dynanorm.wav --dynanorm

	./fmedia rec-dynanorm.wav -o dynanorm.wav --dynanorm -y
	./fmedia dynanorm.wav --pcm-peaks

elif test "$CMD" = "playlist-heal" ; then
	mkdir fmedtest/plheal
	echo '#EXTM3U
#EXTINF:123,ARTIST - abs-rel
/tmp/fmedia/fmedia-1/fmedtest/plheal/file.mp3
#EXTINF:123,ARTIST - chg-ext
plheal/file.mp3
#EXTINF:123,ARTIST - chg-dir
plheal/dir1/dir2/file-cd.mp3
#EXTINF:123,ARTIST - chg-dir-ext
plheal/dir1/dir2/file-cde.mp3
#EXTINF:123,ARTIST - abs-out-of-scope
/tmp/plheal/file-oos.mp3' >fmedtest/list.m3u
	echo '#EXTM3U
#EXTINF:123,ARTIST - abs-rel
plheal/file.ogg
#EXTINF:123,ARTIST - chg-ext
plheal/file.ogg
#EXTINF:123,ARTIST - chg-dir
plheal/dir3/file-cd.mp3
#EXTINF:123,ARTIST - chg-dir-ext
plheal/dir3/file-cde.ogg
#EXTINF:123,ARTIST - abs-out-of-scope
/tmp/plheal/file-oos.mp3' >fmedtest/list2.m3u
	touch fmedtest/plheal/file.ogg
	mkdir fmedtest/plheal/dir3
	touch fmedtest/plheal/dir3/file-cd.mp3
	touch fmedtest/plheal/dir3/file-cde.ogg

	./fmedia --playlist-heal="" "fmedtest/list.m3u"
	diff -Z fmedtest/list.m3u fmedtest/list2.m3u

	echo '#EXTM3U
#EXTINF:123,ARTIST - unchanged
plheal/file.ogg' >fmedtest/list.m3u
	echo '#EXTM3U
#EXTINF:123,ARTIST - unchanged
plheal/file.ogg' >fmedtest/list2.m3u
	./fmedia --playlist-heal="" "fmedtest/list.m3u"
	diff -Z fmedtest/list.m3u fmedtest/list2.m3u

fi

done

rm -rf fmedtest
echo DONE
