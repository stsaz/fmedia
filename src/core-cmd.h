/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>


typedef struct fmed_cmd {
	const fmed_log *log;
	ffarr in_files; //char*[]
	fftask tsk_start;

	byte repeat_all;
	char *trackno;

	uint playdev_name;
	uint captdev_name;
	uint lbdev_name;

	struct {
	uint out_format;
	uint out_rate;
	byte out_channels;
	};

	byte rec;
	byte mix;
	byte tags;
	byte info;
	uint seek_time;
	uint until_time;
	uint64 fseek;
	ffstr meta;

	float gain;
	byte volume;
	byte pcm_peaks;
	byte pcm_crc;
	byte dynanorm;

	float vorbis_qual;
	uint opus_brate;
	uint aac_qual;
	char *aac_profile;
	ushort mpeg_qual;
	byte flac_complevel;
	byte stream_copy;

	ffstr globcmd;
	char *globcmd_pipename;
	byte bground;
	byte bgchild;
	char *conf_fn;
	byte notui;
	byte gui;
	byte print_time;
	byte debug;
	byte cue_gaps;

	ffstr outfn;
	byte overwrite;
	byte out_copy;
	byte preserve_date;

	ffstr dummy;
} fmed_cmd;
