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
	ffmpeg -i rec.wav -c:a flac -y rec.flac.ogg
	ffmpeg -i rec.wav -y rec.aac
	ffmpeg -i rec.wav -c:a wavpack -y rec.wv
	#mpcenc-bin rec.wav rec.mpc
	# TODO rec.ape

	./fmedia rec.*
	./fmedia rec.* --seek=0.500
	./fmedia rec.* --seek=0.500 --until=1.500
fi

if test "$1" = "convert" ; then
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
	./fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16

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
	OPTS="-y --stream-copy"
	$BIN rec.mp3 -o copy.mp3 $OPTS
	$BIN rec.ogg -o copy.ogg $OPTS
	$BIN rec.opus -o copy.opus $OPTS
	$BIN rec.m4a -o copy.m4a $OPTS
	$BIN copy.* --pcm-peaks

	# convert with stream-copy and seek
	OPTS="-y --stream-copy --seek=0.500"
	$BIN rec.mp3 -o copyseek.mp3 $OPTS
	$BIN rec.ogg -o copyseek.ogg $OPTS
	$BIN rec.opus -o copyseek.opus $OPTS
	$BIN rec.m4a -o copyseek.m4a $OPTS
	$BIN copyseek.* --pcm-peaks

	# convert with stream-copy and new meta
	OPTS="-y --stream-copy --meta=artist=SomeArtist;title=SomeTitle"
	$BIN rec.mp3 -o copymeta.mp3 $OPTS
	$BIN rec.ogg -o copymeta.ogg $OPTS # TODO meta not applied
	$BIN rec.opus -o copymeta.opus $OPTS # TODO meta not applied
	$BIN rec.m4a -o copymeta.m4a $OPTS
	$BIN copymeta.* --pcm-peaks
fi

if test "$1" = "filters" ; then
	sh $0 filters_gain
	sh $0 filters_dynanorm
	OPTS="-y"
	$BIN rec.wav -o 'split-$counter.wav' --split=0.100 $OPTS
	$BIN rec.wav -o 'split-$counter.mp3' --split=0.100 $OPTS
	$BIN split-* --pcm-peaks
fi

if test "$1" = "filters_gain" ; then
	OPTS="-y"
	$BIN rec.wav -o vol.wav --volume=50 $OPTS
	$BIN vol.wav --pcm-peaks
	$BIN rec.wav -o gain.wav --gain=-6.0 $OPTS
	$BIN gain.wav --pcm-peaks
fi

if test "$1" = "filters_dynanorm" ; then
	OPTS="-y"
	$BIN rec.wav -o dynanorm.wav --dynanorm $OPTS
	$BIN dynanorm.wav --pcm-peaks
fi

if test "$1" = "all" ; then
	$BIN --list-dev
	sh $0 record
	sh $0 info
	sh $0 play
	sh $0 convert
	sh $0 convert_meta
	sh $0 convert_streamcopy
	sh $0 convert_parallel
	sh $0 filters
fi

echo DONE
