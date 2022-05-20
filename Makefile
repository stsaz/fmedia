# fmedia makefile

# Requirements:
# make gcc cp objcopy strip touch rm mkdir chmod
# Linux: tar
# Windows: zip unix2dos
# macOS: zip

PROJ := fmedia
VER :=
OS :=
OPT := LTO3

# repositories
ROOT := ..
PROJDIR := $(ROOT)/fmedia
SRCDIR := $(PROJDIR)/src
FFBASE := $(ROOT)/ffbase
FFAUDIO := $(ROOT)/ffaudio
AVPACK := $(ROOT)/avpack
FFOS := $(ROOT)/ffos

include $(FFOS)/makeconf
LINK := $(LD)
LINKFLAGS := $(LDFLAGS)


# OS-specific options
ifeq ($(OS),win)
BIN := fmedia.exe
INSTDIR := fmedia
CFLAGS_OS += -DFF_WIN_APIVER=0x0600

else
BIN := fmedia
INSTDIR := fmedia-1
endif

ALIB3 := $(PROJDIR)/alib3/_$(OSFULL)-$(ARCH)

# CFLAGS_STD += -fsanitize=address
# LINKFLAGS += -fsanitize=address -ldl
ifeq ($(OPT),0)
	CFLAGS_OPT += -DFF_DEBUG
endif
FFAUDIO_CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) -I$(FFBASE) -I$(FFAUDIO)
CFLAGS_STD += -DFFBASE_HAVE_FFERR_STR


# CPU-specific options
ifeq "$(CPU)" "amd64"
	CFLAGS_CPU += -msse4.2
else ifeq "$(CPU)" "i686"
	CFLAGS_CPU += -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
endif


CFLAGS_APP := \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-implicit-fallthrough \
	-Wno-stringop-overflow \
	-I$(SRCDIR) -I$(PROJDIR)/alib3 -I$(FFBASE) -I$(FFAUDIO) -I$(AVPACK) -I$(FFOS)
CFLAGS := $(CFLAGS_STD) $(CFLAGS_DEBUG) $(CFLAGS_OPT) $(CFLAGS_OS) $(CFLAGS_CPU) $(CFLAGS_APP)
ifneq ($(OPT),0)
	CFLAGS_OPT := -O3
endif
LINKFLAGS += -Wno-stringop-overflow $(LD_LPTHREAD) -L$(ALIB3)
ifeq ($(OS),linux)
	LINKFLAGS += -L/usr/lib64/pipewire-0.3/jack
endif
ifeq ($(OS),bsd)
	LINKFLAGS += -lexecinfo
endif


OBJ_DIR := .
BIN_CONTAINERS := fmt.$(SO)
BIN_ACODECS := \
	aac.$(SO) alac.$(SO) ape.$(SO) flac.$(SO) mpeg.$(SO) mpc.$(SO) opus.$(SO) vorbis.$(SO) wavpack.$(SO)
BIN_AFILTERS := dynanorm.$(SO) \
	soxr.$(SO) \
	afilter.$(SO)
BINS := $(BIN) core.$(SO) tui.$(SO) net.$(SO) plist.$(SO) \
	$(BIN_CONTAINERS) \
	$(BIN_ACODECS) \
	$(BIN_AFILTERS)

# OS-specific modules
OS_BINS :=
ifeq ($(OS),linux)

OS_BINS += alsa.$(SO) pulse.$(SO) jack.$(SO) gui.$(SO) dbus.$(SO)

else ifeq ($(OS),apple)
OS_BINS += coreaudio.$(SO)

else ifeq ($(OS),bsd)
OS_BINS += oss.$(SO)

else ifeq ($(OS),win)
#windows:

OS_BINS += direct-sound.$(SO) wasapi.$(SO) \
	gui.$(SO) fmedia-gui.exe

RES := $(OBJ_DIR)/fmedia.coff

endif

BINS += $(OS_BINS)

build: $(BINS)

FF_O := $(OBJ_DIR)/ffos.o \
	$(OBJ_DIR)/ffstring.o
ifeq "$(OSFULL)" "windows"
	FF_O +=	$(OBJ_DIR)/ffwin.o
	FFOS_SKT := $(OBJ_DIR)/ffwin-skt.o
else ifeq "$(OSFULL)" "macos"
	FF_O +=	$(OBJ_DIR)/ffunix.o $(OBJ_DIR)/ffapple.o
else ifeq "$(OSFULL)" "bsd"
	FF_O +=	$(OBJ_DIR)/ffunix.o $(OBJ_DIR)/ffbsd.o
else
	FF_O +=	$(OBJ_DIR)/ffunix.o $(OBJ_DIR)/fflinux.o
endif

GLOBDEPS := $(SRCDIR)/fmedia.h \
	$(wildcard $(SRCDIR)/util/*.h) \
	$(wildcard $(SRCDIR)/util/ffos-compat/*.h) \
	$(wildcard $(FFBASE)/ffbase/*.h) $(wildcard $(FFOS)/FFOS/*.h)

$(OBJ_DIR)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h) $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/cmd.h $(SRCDIR)/cmdline.h $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRCDIR)/adev/%.c $(wildcard $(SRCDIR)/adev/*.h) $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRCDIR)/acodec/%.c \
		$(wildcard $(SRCDIR)/acodec/*.h) \
		$(wildcard $(SRCDIR)/acodec/alib3-bridge/*.h) \
		$(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRCDIR)/format/%.c $(wildcard $(SRCDIR)/format/*.h) $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRCDIR)/util/%.c \
		$(wildcard $(SRCDIR)/util/*.h) \
		$(wildcard $(SRCDIR)/util/ffos-compat/*.h)
	$(C) $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRCDIR)/util/ffos-compat/%.c \
		$(wildcard $(SRCDIR)/util/ffos-compat/*.h)
	$(C) $(CFLAGS) $< -o $@

$(RES): $(PROJDIR)/res/fmedia.rc $(wildcard $(PROJDIR)/res/*.ico)
	$(WINDRES) -I$(SRCDIR) -I$(FFBASE) -I$(FFOS) $(PROJDIR)/res/fmedia.rc $@


BIN_O := \
	$(OBJ_DIR)/crash.o \
	$(OBJ_DIR)/main.o \
	$(FF_O) \
	$(FFOS_WREG) \
	$(OBJ_DIR)/ffpcm.o

ifeq ($(OS),win)
BIN_O += $(RES)
endif

$(BIN): $(BIN_O)
	$(LINK) $(BIN_O) $(LINKFLAGS) $(LD_LDL) $(LD_LMATH) -o $@


#
$(OBJ_DIR)/%.o: $(SRCDIR)/core/%.c $(GLOBDEPS) $(wildcard $(SRCDIR)/core/*.h)
	$(C) $(CFLAGS) $< -o $@
CORE_O := $(OBJ_DIR)/core.o $(OBJ_DIR)/core-conf.o \
	$(OBJ_DIR)/track.o \
	$(OBJ_DIR)/file.o \
	$(OBJ_DIR)/file-out.o \
	$(OBJ_DIR)/file-std.o \
	$(OBJ_DIR)/queue.o \
	$(OBJ_DIR)/globcmd.o \
	$(OBJ_DIR)/ffpcm.o

ifeq ($(OS),win)
	CORE_O += $(OBJ_DIR)/sys-sleep-win.o
endif

CORE_O += $(FF_O) \
	$(FFOS_WREG) \
	$(OBJ_DIR)/fffileread.o \
	$(OBJ_DIR)/fffilewrite.o \
	$(OBJ_DIR)/ffthpool.o
ifeq ($(OS),win)
CORE_O += $(OBJ_DIR)/ffwohandler.o
endif
core.$(SO): $(CORE_O)
	$(LINK) -shared $(CORE_O) $(LINKFLAGS) $(LD_LDL) $(LD_LMATH) $(LD_LPTHREAD) -o $@


#
$(OBJ_DIR)/%.o: $(SRCDIR)/afilter/%.c $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@
$(OBJ_DIR)/soundmod.o: $(SRCDIR)/afilter/soundmod.c $(GLOBDEPS) \
		$(wildcard $(SRCDIR)/afilter/gain.h)
	$(C) $(CFLAGS) $< -o $@
$(OBJ_DIR)/%.o: $(PROJDIR)/3pt/crc/%.c $(GLOBDEPS)
	$(C) $(CFLAGS) $< -o $@

afilter.$(SO): $(OBJ_DIR)/soundmod.o \
		$(OBJ_DIR)/aconv.o \
		$(OBJ_DIR)/auto-attenuator.o \
		$(OBJ_DIR)/mixer.o \
		$(OBJ_DIR)/peaks.o \
		$(OBJ_DIR)/split.o \
		$(OBJ_DIR)/start-stop-level.o \
		$(FF_O) \
		$(OBJ_DIR)/crc.o \
		$(OBJ_DIR)/ffpcm.o
	$(LINK) -shared $+ $(LINKFLAGS) $(LD_LMATH) -o $@


#
TUI_O := \
	$(OBJ_DIR)/tui.o \
	$(FF_O) \
	$(OBJ_DIR)/ffpcm.o
tui.$(SO): $(TUI_O)
	$(LINK) -shared $(TUI_O) $(LINKFLAGS) $(LD_LMATH) $(LD_LPTHREAD) -o $@


#
$(OBJ_DIR)/%.o: $(SRCDIR)/net/%.c $(GLOBDEPS) $(SRCDIR)/net/net.h
	$(C) $(CFLAGS)  $< -o$@
NET_O := $(OBJ_DIR)/net.o $(OBJ_DIR)/hls.o \
	$(OBJ_DIR)/ffhttp-client.o \
	$(OBJ_DIR)/ffurl.o \
	$(FF_O) \
	$(FFOS_SKT)
net.$(SO): $(NET_O)
	$(LINK) -shared $(NET_O) $(LINKFLAGS) $(LD_LWS2_32) -o$@


#
SOXR_O := $(OBJ_DIR)/soxr.o \
	$(OBJ_DIR)/ffpcm.o \
	$(FF_O)
soxr.$(SO): $(SOXR_O)
	$(LINK) -shared $(SOXR_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lsoxr-ff -o $@


#
DYNANORM_O := $(OBJ_DIR)/dynanorm.o \
	$(FF_O)
dynanorm.$(SO): $(DYNANORM_O)
	$(LINK) -shared $(DYNANORM_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lDynamicAudioNormalizer-ff -o $@


#
MPEG_O := $(OBJ_DIR)/mpeg.o \
	$(FF_O)
mpeg.$(SO): $(MPEG_O)
	$(LINK) -shared $(MPEG_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lmpg123-ff -lmp3lame-ff -o $@


#
MPC_O := $(OBJ_DIR)/mpc.o $(FF_O)
mpc.$(SO): $(MPC_O)
	$(LINK) -shared $(MPC_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lmpc-ff -o $@


#
FMT_O := $(OBJ_DIR)/mod-fmt.o \
	$(OBJ_DIR)/aac-adts.o \
	$(OBJ_DIR)/ape-read.o \
	$(OBJ_DIR)/avi.o \
	$(OBJ_DIR)/caf.o \
	$(OBJ_DIR)/edit-tags.o \
	$(OBJ_DIR)/mkv.o \
	$(OBJ_DIR)/mp3.o \
	$(OBJ_DIR)/mp4.o \
	$(OBJ_DIR)/mpc-read.o \
	$(OBJ_DIR)/ogg.o \
	$(OBJ_DIR)/wav.o \
	$(OBJ_DIR)/wv.o \
	$(FF_O)
fmt.$(SO): $(FMT_O)
	$(LINK) -shared $(FMT_O) $(LINKFLAGS) -o $@


#
VORBIS_O := $(OBJ_DIR)/vorbis.o \
	$(FF_O)
vorbis.$(SO): $(VORBIS_O)
	$(LINK) -shared $(VORBIS_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -logg-ff -lvorbis-ff -lvorbisenc-ff -o $@


#
OPUS_O := $(OBJ_DIR)/opus.o \
	$(FF_O)
opus.$(SO): $(OPUS_O)
	$(LINK) -shared $(OPUS_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lopus-ff -o $@


#
# Note: .flac r/w can't be moved to fmt.so because avpack/flac code depents on libFLAC-ff.so::flac_crc8()
FLAC_O := $(OBJ_DIR)/flac.o \
	$(OBJ_DIR)/flac-fmt.o \
	$(OBJ_DIR)/flac-ogg.o \
	$(FF_O) \
	$(OBJ_DIR)/ffpcm.o
flac.$(SO): $(FLAC_O)
	$(LINK) -shared $(FLAC_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lFLAC-ff -o $@


#
WAVPACK_O := $(OBJ_DIR)/wavpack.o \
	$(FF_O)
wavpack.$(SO): $(WAVPACK_O)
	$(LINK) -shared $(WAVPACK_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lwavpack-ff -o $@


#
APE_O := $(OBJ_DIR)/ape.o \
	$(FF_O)
ape.$(SO): $(APE_O)
	$(LINK) -shared $(APE_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lMAC-ff -o $@


#
AAC_O := $(OBJ_DIR)/aac.o \
	$(FF_O)
aac.$(SO): $(AAC_O)
	$(LINK) -shared $(AAC_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lfdk-aac-ff -o $@


#
ALAC_O := $(OBJ_DIR)/alac.o \
	$(FF_O)
alac.$(SO): $(ALAC_O)
	$(LINK) -shared $(ALAC_O) $(LINKFLAGS) $(LD_RPATH_ORIGIN) -lALAC-ff -o $@


#
PLIST_O := $(OBJ_DIR)/plist.o \
	$(OBJ_DIR)/cue.o \
	$(OBJ_DIR)/dir.o \
	$(FF_O) \
	$(FFOS_WREG)
plist.$(SO): $(PLIST_O)
	$(LINK) -shared $(PLIST_O) $(LINKFLAGS) -o $@


#
$(OBJ_DIR)/ffaudio-dsound.o: $(FFAUDIO)/ffaudio/dsound.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

DSOUND_O := $(OBJ_DIR)/dsound.o $(FF_O) \
	$(OBJ_DIR)/ffwohandler.o \
	$(OBJ_DIR)/ffaudio-dsound.o \
	$(OBJ_DIR)/ffpcm.o
direct-sound.$(SO): $(DSOUND_O)
	$(LINK) -shared $(DSOUND_O) $(LINKFLAGS) -ldsound -ldxguid -o $@


#
$(OBJ_DIR)/ffaudio-wasapi.o: $(FFAUDIO)/ffaudio/wasapi.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

WASAPI_O := $(OBJ_DIR)/wasapi.o $(FF_O) \
	$(OBJ_DIR)/ffwohandler.o \
	$(OBJ_DIR)/ffpcm.o \
	$(OBJ_DIR)/ffaudio-wasapi.o
wasapi.$(SO): $(WASAPI_O)
	$(LINK) -shared $(WASAPI_O) $(LINKFLAGS) -lole32 -o$@


#
$(OBJ_DIR)/ffaudio-alsa.o: $(FFAUDIO)/ffaudio/alsa.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

ALSA_O := $(OBJ_DIR)/alsa.o $(FF_O) \
	$(OBJ_DIR)/ffaudio-alsa.o
alsa.$(SO): $(ALSA_O)
	$(LINK) -shared $(ALSA_O) $(LINKFLAGS) -lasound -o$@


#
$(OBJ_DIR)/ffaudio-pulse.o: $(FFAUDIO)/ffaudio/pulse.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

PULSE_O := $(OBJ_DIR)/pulse.o $(FF_O) \
	$(OBJ_DIR)/ffaudio-pulse.o
pulse.$(SO): $(PULSE_O)
	$(LINK) -shared $(PULSE_O) $(LINKFLAGS) -lpulse -o$@


#
$(OBJ_DIR)/ffaudio-jack.o: $(FFAUDIO)/ffaudio/jack.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

JACK_O := $(OBJ_DIR)/jack.o $(FF_O) \
	$(OBJ_DIR)/ffaudio-jack.o
jack.$(SO): $(JACK_O)
	$(LINK) -shared $(JACK_O) $(LINKFLAGS) -ljack -o$@


#
$(OBJ_DIR)/ffaudio-coreaudio.o: $(FFAUDIO)/ffaudio/coreaudio.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

COREAUDIO_O := $(OBJ_DIR)/coreaudio.o $(FF_O) \
	$(OBJ_DIR)/ffpcm.o \
	$(OBJ_DIR)/ffaudio-coreaudio.o
coreaudio.$(SO): $(COREAUDIO_O)
	$(LINK) -shared $(COREAUDIO_O) $(LINKFLAGS) -framework CoreFoundation -framework CoreAudio -o$@


#
$(OBJ_DIR)/ffaudio-oss.o: $(FFAUDIO)/ffaudio/oss.c
	$(C) $(FFAUDIO_CFLAGS) $< -o $@

OSS_O := $(OBJ_DIR)/oss.o $(FF_O) \
	$(OBJ_DIR)/ffpcm.o \
	$(OBJ_DIR)/ffaudio-oss.o
oss.$(SO): $(OSS_O)
	$(LINK) -shared $(OSS_O) $(LINKFLAGS) $(LD_LMATH) -o $@


ifeq ($(OS),win)
#
FF_GUIHDR := $(wildcard $(SRCDIR)/util/gui-winapi/*.h)
$(OBJ_DIR)/gui-dlgs.o: $(SRCDIR)/gui-winapi/gui-dlgs.c $(SRCDIR)/gui-winapi/*.h $(GLOBDEPS) $(FF_GUIHDR)
	$(C) $(CFLAGS)  $< -o$@
$(OBJ_DIR)/%.o: $(SRCDIR)/gui-winapi/%.c $(SRCDIR)/gui-winapi/gui.h $(GLOBDEPS) $(FF_GUIHDR)
	$(C) $(CFLAGS)  $< -o$@

$(OBJ_DIR)/%.o: $(SRCDIR)/util/gui-winapi/%.c $(FF_GUIHDR) $(GLOBDEPS)
	$(C) $(CFLAGS)  $< -o$@

GUI_O := $(OBJ_DIR)/gui.o \
	$(OBJ_DIR)/gui-main.o \
	$(OBJ_DIR)/gui-dlgs.o \
	$(OBJ_DIR)/gui-convert.o \
	$(OBJ_DIR)/gui-rec.o \
	$(OBJ_DIR)/gui-theme.o \
	$(FF_O) \
	$(FFOS_WREG) \
	$(OBJ_DIR)/ffpcm.o \
	$(OBJ_DIR)/ffgui-winapi-loader.o \
	$(OBJ_DIR)/ffgui-winapi.o
LIBS_GUIWAPI := -lshell32 -luxtheme -lcomctl32 -lcomdlg32 -lgdi32 -lole32 -luuid
gui.$(SO): $(GUI_O)
	$(LINK) -shared $(GUI_O) $(LINKFLAGS) $(LIBS_GUIWAPI) -o $@

#
BINGUI_O := \
	$(OBJ_DIR)/crash.o \
	$(OBJ_DIR)/fmedia-gui.o \
	$(FF_O) \
	$(FFOS_WREG) \
	$(OBJ_DIR)/ffgui-winapi.o \
	$(RES)
fmedia-gui.exe: $(BINGUI_O)
	$(LINK) $(BINGUI_O) $(LINKFLAGS) -lcomctl32 -lole32 -luuid -mwindows -o$@

else ifeq ($(OS),linux)

#
FF_GUIHDR := $(wildcard $(SRCDIR)/util/gui-gtk/*.h)
CFLAGS_GTK := -I/usr/include/gtk-3.0 -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/fribidi -I/usr/include/cairo -I/usr/include/pixman-1 -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/uuid -I/usr/include/harfbuzz -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/gio-unix-2.0/ -I/usr/include/libdrm -I/usr/include/valgrind -I/usr/include/atk-1.0 -I/usr/include/at-spi2-atk/2.0 -I/usr/include/at-spi-2.0 -I/usr/include/dbus-1.0 -I/usr/lib64/dbus-1.0/include
LIBS_GTK := -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lpango-1.0 -lfribidi -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

$(OBJ_DIR)/gui.o: $(SRCDIR)/gui-gtk/gui.c $(SRCDIR)/gui-gtk/gui.h $(GLOBDEPS) $(FF_GUIHDR)
	$(C) $(CFLAGS) $(CFLAGS_GTK) $< -o $@

$(OBJ_DIR)/gui-dlgs.o: $(SRCDIR)/gui-gtk/gui-dlgs.c $(wildcard $(SRCDIR)/gui-gtk/*.h) $(GLOBDEPS) $(FF_GUIHDR)
	$(C) $(CFLAGS) $(CFLAGS_GTK)  $< -o$@

$(OBJ_DIR)/gui-main.o: $(SRCDIR)/gui-gtk/gui-main.c $(SRCDIR)/gui-gtk/gui.h $(SRCDIR)/gui-gtk/file-explorer.h $(GLOBDEPS) $(FF_GUIHDR)
	$(C) $(CFLAGS) $(CFLAGS_GTK)  $< -o$@

$(OBJ_DIR)/%.o: $(SRCDIR)/util/gui-gtk/%.c $(FF_GUIHDR) $(GLOBDEPS)
	$(C) $(CFLAGS) $(CFLAGS_GTK)  $< -o$@

GUIGTK_O := $(OBJ_DIR)/gui.o \
	$(OBJ_DIR)/gui-main.o \
	$(OBJ_DIR)/gui-dlgs.o \
	$(OBJ_DIR)/ffgui-gtk.o \
	$(OBJ_DIR)/ffgui-gtk-loader.o \
	$(OBJ_DIR)/ffpcm.o \
	$(FF_O)
gui.$(SO): $(GUIGTK_O)
	$(LINK) -shared $(GUIGTK_O) $(LINKFLAGS) $(LIBS_GTK) $(LD_LPTHREAD) $(LD_LMATH) -o$@

endif


#
$(OBJ_DIR)/sys-sleep-dbus.o: $(SRCDIR)/sys-sleep-dbus.c $(GLOBDEPS)
	$(C) $(CFLAGS) -I/usr/include/dbus-1.0 -I/usr/lib64/dbus-1.0/include  $< -o$@
# pkg-config --cflags dbus-1

dbus.$(SO): $(OBJ_DIR)/sys-sleep-dbus.o $(FF_O)
	$(LINK) -shared $+ $(LINKFLAGS) -ldbus-1 -o$@


clean:
	rm -vf $(BINS) *.debug *.o $(RES)

distclean: clean
	rm -vfr $(INSTDIR) ./$(PROJ)-*.zip ./$(PROJ)-*.tar.xz


strip: $(BINS:.$(SO)=.$(SO).debug) $(BIN).debug $(BINS:.exe=.exe.debug)


install-only:
	mkdir -vp $(INSTDIR)
	$(CP) $(BIN) \
		$(PROJDIR)/fmedia.conf $(PROJDIR)/help*.txt $(PROJDIR)/CHANGES.txt \
		$(PROJDIR)/LICENSE \
		$(INSTDIR)/
	$(CP) $(PROJDIR)/README.md $(INSTDIR)/README.txt

ifeq ($(OS),win)
	$(CP) ./fmedia-gui.exe \
		$(PROJDIR)/src/gui-winapi/fmedia.gui \
		$(PROJDIR)/src/gui-winapi/gui_lang_*.txt \
		$(PROJDIR)/src/gui-winapi/theme.conf \
		$(INSTDIR)/
	unix2dos $(INSTDIR)/*.txt $(INSTDIR)/*.conf $(INSTDIR)/*.gui $(INSTDIR)/LICENSE

else ifeq ($(OS),linux)
	$(CP) $(PROJDIR)/src/gui-gtk/fmedia.gui \
		$(PROJDIR)/src/gui-gtk/gui_lang_*.txt \
		$(PROJDIR)/src/gui-gtk/fmedia.desktop \
		$(PROJDIR)/res/fmedia.ico \
		$(PROJDIR)/res/play.ico \
		$(PROJDIR)/res/stop.ico \
		$(PROJDIR)/res/prev.ico \
		$(PROJDIR)/res/next.ico \
		$(INSTDIR)/
endif

	chmod 644 $(INSTDIR)/*
	chmod 755 $(INSTDIR)/$(BIN)

	mkdir -vp $(INSTDIR)/mod
	chmod 755 $(INSTDIR)/mod
	$(CP) \
		*.$(SO) \
		$(ALIB3)/libALAC-ff.$(SO) \
		$(ALIB3)/libDynamicAudioNormalizer-ff.$(SO) \
		$(ALIB3)/libfdk-aac-ff.$(SO) \
		$(ALIB3)/libFLAC-ff.$(SO) \
		$(ALIB3)/libMAC-ff.$(SO) \
		$(ALIB3)/libmp3lame-ff.$(SO) \
		$(ALIB3)/libmpc-ff.$(SO) \
		$(ALIB3)/libmpg123-ff.$(SO) \
		$(ALIB3)/libopus-ff.$(SO) \
		$(ALIB3)/libsoxr-ff.$(SO) \
		$(ALIB3)/libvorbis-ff.$(SO) $(ALIB3)/libvorbisenc-ff.$(SO) $(ALIB3)/libogg-ff.$(SO) \
		$(ALIB3)/libwavpack-ff.$(SO) \
		$(INSTDIR)/mod
	chmod 644 $(INSTDIR)/mod/*


copy-bins:
	$(CP) \
		*.$(SO) \
		$(INSTDIR)/mod


installd: build
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) install-only

install: build
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) strip
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) install-only


BINS_NODEPS := $(BIN) core.$(SO) net.$(SO) afilter.$(SO) plist.$(SO) \
	$(BIN_CONTAINERS) $(OS_BINS) \
	wav.$(SO)

build-nodeps: $(BINS_NODEPS)

install-nodeps: build-nodeps

	mkdir -vp $(INSTDIR)
	$(CP) $(BINS_NODEPS) \
		$(PROJDIR)/fmedia.conf $(PROJDIR)/help*.txt $(PROJDIR)/CHANGES.txt \
		$(INSTDIR)/
	$(CP) $(PROJDIR)/README.md $(INSTDIR)/README.txt

ifneq ($(OS),win)
	chmod 644 $(INSTDIR)/*
	chmod 755 $(INSTDIR)/fmedia

else

	$(CP) ./fmedia-gui.exe \
		$(PROJDIR)/src/gui-winapi/fmedia.gui \
		$(INSTDIR)/
	unix2dos $(INSTDIR)/*.txt $(INSTDIR)/*.conf $(INSTDIR)/*.gui
	chmod 644 $(INSTDIR)/*
endif

	chmod 755 $(INSTDIR)/$(BIN) $(INSTDIR)/*.$(SO)

package:
	rm -f $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS).$(PACK_EXT) $(INSTDIR)
	$(PACK) $(PROJ)-$(VER)-$(OS)-$(ARCH_OS)-debug.$(PACK_EXT) ./*.debug

install-package: install
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) package
