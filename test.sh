# fmedia tester

# print commands before executing
set -x

# exit if a child process reports an error
set -e

BIN=./fmedia

if test "$1" = "clean" ; then
	rm *.wav *.flac *.mp3 *.m4a *.ogg *.opus
	exit
fi

if test "$1" = "radio" ; then
	URL="http://"
	$BIN $URL -o '$artist-$title.mp3' --out-copy --stream-copy -y --meta=artist=A --until=1
fi

if test "$1" = "record" ; then
	# record -> .*
	OPTS="-y --until=2 --rate=44100 --format=int16"
	# TODO --meta=artist=A;title=T
	$BIN --record -o rec.wav $OPTS
	$BIN --record -o rec.flac $OPTS
	$BIN --record -o rec.mp3 $OPTS
	$BIN --record -o rec.m4a $OPTS
	$BIN --record -o rec.ogg $OPTS
	$BIN --record -o rec.opus $OPTS
	$BIN rec.* --pcm-peaks
	$BIN rec.* --pcm-peaks --seek=0.500
fi

if test "$1" = "info" ; then
	OPTS="--info --tags"
	$BIN rec.* $OPTS
fi

if test "$1" = "play" ; then
	# play
	OPTS=""
	$BIN rec.wav $OPTS
	$BIN rec.flac $OPTS
	$BIN rec.mp3 $OPTS
	$BIN rec.m4a $OPTS
	$BIN rec.ogg $OPTS
	$BIN rec.opus $OPTS

	# play with seek
	OPTS="--seek=0.500"
	$BIN rec.wav $OPTS
	$BIN rec.flac $OPTS
	$BIN rec.mp3 $OPTS
	$BIN rec.m4a $OPTS
	# TODO $BIN rec.ogg $OPTS
	# TODO $BIN rec.opus $OPTS

	# play with seek & until
	OPTS="--seek=0.500 --until=2"
	# TODO OPTS="--seek=0.500 --until=1"
	$BIN rec.wav $OPTS
	$BIN rec.flac $OPTS
	$BIN rec.mp3 $OPTS
	$BIN rec.m4a $OPTS
	$BIN rec.ogg $OPTS
	$BIN rec.opus $OPTS
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
	# convert with meta
	OPTS="-y --meta=artist=A2;title=T2"
	$BIN rec.wav -o enc-meta.wav $OPTS
	$BIN rec.wav -o enc-meta.flac $OPTS
	$BIN rec.wav -o enc-meta.mp3 $OPTS
	$BIN rec.wav -o enc-meta.m4a $OPTS
	$BIN rec.wav -o enc-meta.ogg $OPTS
	$BIN rec.wav -o enc-meta.opus $OPTS
	$BIN enc-meta.* --pcm-peaks --tags

	# convert from different format to the same format with meta
	OPTS="-y --meta=artist=A2;title=T2"
	$BIN rec.wav -o enc-meta-fmt.wav $OPTS
	$BIN rec.flac -o enc-meta-fmt.flac $OPTS
	$BIN rec.mp3 -o enc-meta-fmt.mp3 $OPTS
	$BIN rec.m4a -o enc-meta-fmt.m4a $OPTS
	$BIN rec.ogg -o enc-meta-fmt.ogg $OPTS
	$BIN rec.opus -o enc-meta-fmt.opus $OPTS
	$BIN enc-meta-fmt.* --pcm-peaks --tags
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
	OPTS="-y --stream-copy --meta=artist=A2;title=T2"
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
