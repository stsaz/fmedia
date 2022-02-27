# alib3

include makeconf
# ARCH := CPU=i686
BINDIR := _$(OS)-amd64

all:
	$(MAKE) -Rr -C ALAC
	$(MAKE) -Rr -C DynamicAudioNormalizer
	$(MAKE) -Rr -C fdk-aac
	$(MAKE) -Rr -C FLAC
	$(MAKE) -Rr -C MAC
	$(MAKE) -Rr -C mp3lame
	$(MAKE) -Rr -C mpg123 $(ARCH)
	$(MAKE) -Rr -C musepack
	$(MAKE) -Rr -C ogg
	$(MAKE) -Rr -C opus
	$(MAKE) -Rr -C soxr
	$(MAKE) -Rr -C vorbis
	$(MAKE) -Rr -C wavpack $(ARCH)

install:
	mkdir -p $(BINDIR)
	cp -u ALAC/*.$(SO) \
		DynamicAudioNormalizer/*.$(SO) \
		fdk-aac/*.$(SO) \
		FLAC/*.$(SO) \
		MAC/*.$(SO) \
		mp3lame/*.$(SO) \
		mpg123/*.$(SO) \
		musepack/*.$(SO) \
		ogg/*.$(SO) \
		opus/*.$(SO) \
		soxr/*.$(SO) \
		vorbis/*.$(SO) \
		wavpack/*.$(SO) \
		$(BINDIR)

clean:
	$(MAKE) -C ALAC clean
	$(MAKE) -C DynamicAudioNormalizer clean
	$(MAKE) -C fdk-aac clean
	$(MAKE) -C FLAC clean
	$(MAKE) -C MAC clean
	$(MAKE) -C mp3lame clean
	$(MAKE) -C mpg123 $(ARCH) clean
	$(MAKE) -C musepack clean
	$(MAKE) -C ogg clean
	$(MAKE) -C opus clean
	$(MAKE) -C soxr clean
	$(MAKE) -C vorbis clean
	$(MAKE) -C wavpack $(ARCH) clean

cleanlib:
	$(RM) ALAC/*.$(SO) \
		DynamicAudioNormalizer/*.$(SO) \
		fdk-aac/*.$(SO) \
		FLAC/*.$(SO) \
		MAC/*.$(SO) \
		mp3lame/*.$(SO) \
		mpg123/*.$(SO) \
		musepack/*.$(SO) \
		ogg/*.$(SO) \
		opus/*.$(SO) \
		soxr/*.$(SO) \
		vorbis/*.$(SO) \
		wavpack/*.$(SO)
