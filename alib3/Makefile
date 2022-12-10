# alib3

include makeconf
BINDIR := _$(OS)-$(CPU)

build:
	$(MAKE) -Rr -C ALAC
	$(MAKE) -Rr -C fdk-aac
	$(MAKE) -Rr -C FLAC
	$(MAKE) -Rr -C mp3lame
	$(MAKE) -Rr -C mpg123
	$(MAKE) -Rr -C musepack
	$(MAKE) -Rr -C ogg
	$(MAKE) -Rr -C opus
	$(MAKE) -Rr -C soxr
	$(MAKE) -Rr -C vorbis
	$(MAKE) -Rr -C wavpack

install:
	mkdir -p $(BINDIR)
	$(CP) ALAC/*.$(SO) \
		fdk-aac/*.$(SO) \
		FLAC/*.$(SO) \
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
	$(MAKE) -C fdk-aac clean
	$(MAKE) -C FLAC clean
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

md5:
	md5sum -b \
		ALAC/master.zip \
		fdk-aac/v0.1.6.tar.gz \
		FLAC/flac-1.3.3.tar.xz \
		mp3lame/lame-3.100.tar.gz \
		mpg123/mpg123-1.25.10.tar.bz2 \
		musepack/musepack_src_r475.tar.gz \
		ogg/libogg-1.3.3.tar.xz \
		opus/opus-1.3.1.tar.gz \
		soxr/soxr-0.1.3-Source.tar.xz \
		vorbis/libvorbis-1.3.7.tar.xz \
		wavpack/wavpack-4.75.0.tar.bz2 \
		>packages.md5

md5check:
	md5sum -c packages.md5 --ignore-missing
