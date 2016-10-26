/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>


typedef struct fmed_cmd {
	const fmed_log *log;
	ffstr root;
	struct { FFARR(char*) } in_files;
	fftask tsk_start;

	ffbool repeat_all;
	char *trackno;

	uint playdev_name;
	uint captdev_name;

	struct {
	uint out_format;
	uint out_rate;
	byte out_channels;
	};

	ffbool rec;
	ffbool mix;
	ffbool tags;
	ffbool info;
	uint seek_time;
	uint until_time;
	uint64 fseek;
	ffstr meta;

	float gain;
	byte volume;
	byte pcm_peaks;
	byte pcm_crc;

	float vorbis_qual;
	uint aac_qual;
	ushort mpeg_qual;
	byte flac_complevel;
	ffbool stream_copy;

	ffbool notui;
	ffbool gui;
	ffbool print_time;
	ffbool debug;
	byte cue_gaps;

	ffstr outfn;
	ffstr outdir;
	ffbool overwrite;
	ffbool out_copy;
	ffbool preserve_date;
} fmed_cmd;
