rem fmedia tester

if %1 equ clean (
	del *.aac *.wav *.flac *.mp3 *.m4a *.ogg *.opus *.mpc *.wv
	goto end
)

rem record -> .*
rem TODO --meta=artist=A;title=T
.\fmedia --record -o rec.wav -y --until=2 --rate=44100 --format=int16
.\fmedia --record -o rec.flac -y --until=2 --rate=44100 --format=int16
.\fmedia --record -o rec.mp3 -y --until=2 --rate=44100 --format=int16
.\fmedia --record -o rec.m4a -y --until=2 --rate=44100 --format=int16
.\fmedia --record -o rec.ogg -y --until=2 --rate=44100 --format=int16
.\fmedia --record -o rec.opus -y --until=2 --rate=44100 --format=int16

rem read the recorded
rem TODO rec.flac.ogg
rem TODO rec.aac
rem TODO rec.wv
rem TODO rec.ape
rem TODO rec.mpc
.\fmedia rec.* --pcm-peaks
.\fmedia rec.* --pcm-peaks --seek=0.500
.\fmedia rec.* --info --tags

rem playback the recorded
.\fmedia rec.*
.\fmedia rec.* --seek=0.500
.\fmedia rec.* --seek=0.500 --until=1.500

rem convert .wav -> .*
.\fmedia rec.wav -o enc.wav -y
.\fmedia rec.wav -o enc.flac -y
.\fmedia rec.wav -o enc.mp3 -y
.\fmedia rec.wav -o enc.m4a -y
.\fmedia rec.wav -o enc.ogg -y
.\fmedia rec.wav -o enc.opus -y
.\fmedia enc.* --pcm-peaks
.\fmedia enc.* --pcm-peaks --seek=0.500

rem convert with --seek: .wav -> .*
.\fmedia rec.wav -o enc_seek.wav -y --seek=0.500
.\fmedia rec.wav -o enc_seek.flac -y --seek=0.500
.\fmedia rec.wav -o enc_seek.mp3 -y --seek=0.500
.\fmedia rec.wav -o enc_seek.m4a -y --seek=0.500
.\fmedia rec.wav -o enc_seek.ogg -y --seek=0.500
.\fmedia rec.wav -o enc_seek.opus -y --seek=0.500
.\fmedia enc_seek.* --pcm-peaks

rem convert with --until: .wav -> .*
.\fmedia rec.wav -o enc_until.wav -y --until=0.500
.\fmedia rec.wav -o enc_until.flac -y --until=0.500
.\fmedia rec.wav -o enc_until.mp3 -y --until=0.500
.\fmedia rec.wav -o enc_until.m4a -y --until=0.500
.\fmedia rec.wav -o enc_until.ogg -y --until=0.500
.\fmedia rec.wav -o enc_until.opus -y --until=0.500
.\fmedia enc_until.* --pcm-peaks

rem convert with --rate: .wav -> .*
.\fmedia rec.wav -o enc48.wav -y --rate=48000
.\fmedia rec.wav -o enc48.flac -y --rate=48000
.\fmedia rec.wav -o enc48.mp3 -y --rate=48000
.\fmedia rec.wav -o enc48.m4a -y --rate=48000
.\fmedia rec.wav -o enc48.ogg -y --rate=48000
.\fmedia rec.wav -o enc48.opus -y --rate=48000
.\fmedia enc48.* --pcm-peaks

:end
