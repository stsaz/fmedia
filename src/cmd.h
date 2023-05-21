/**
Copyright (c) 2016 Simon Zolin */

#include <util/array.h>


typedef struct fmed_cmd {
	ffarr in_files; //char*[]
	fftask tsk_start;

	byte repeat_all;
	byte list_random;
	char *trackno;

	uint playdev_name;
	uint captdev_name;
	uint lbdev_name;
	ushort play_buf_len; //msec

	struct {
	uint out_format;
	uint out_rate;
	byte out_channels;
	};

	uint pl_heal_idx;

	byte rec;
	ushort capture_buf_len; //msec
	byte mix;
	byte tags;
	byte info;
	uint seek_time;
	uint until_time;
	uint split_time;
	uint prebuffer;
	float start_level; //dB
	float stop_level; //dB
	uint stop_level_time; //msec
	uint stop_level_mintime; //msec
	uint64 fseek;
	ffstr meta, meta_from_filename;
	ffslice include_files; //ffstr[]
	ffslice exclude_files; //ffstr[]
	char *include_files_data, *exclude_files_data;

	float gain;
	float auto_attenuate;
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
	char *http_ctl_options;
	byte bground;
	byte bgchild;
	char *conf_fn;
	byte notui;
	byte gui;
	byte print_time;
	byte cue_gaps;
	char *playlist_heal;

	ffstr outfn;
	char *outfnz;
	byte overwrite;
	byte out_copy;
	byte preserve_date;
	byte parallel;
	byte edittags;

	ffstr dummy;

	uint until_plback_end :1;
} fmed_cmd;

static inline int cmd_init(fmed_cmd *cmd)
{
	cmd->vorbis_qual = -255;
	cmd->aac_qual = (uint)-1;
	cmd->mpeg_qual = 0xffff;
	cmd->flac_complevel = 0xff;

	cmd->captdev_name = (uint)-1;
	cmd->lbdev_name = (uint)-1;
	cmd->volume = 100;
	cmd->cue_gaps = 255;
	return 0;
}

static inline void cmd_destroy(fmed_cmd *cmd)
{
	if (cmd == NULL)
		return;

	FFARR_FREE_ALL_PTR(&cmd->in_files, ffmem_free, char*);
	ffmem_free(cmd->outfnz);

	ffstr_free(&cmd->meta);
	ffstr_free(&cmd->meta_from_filename);
	ffmem_safefree(cmd->aac_profile);
	ffmem_safefree(cmd->trackno);
	ffmem_safefree(cmd->conf_fn);

	ffmem_safefree(cmd->globcmd_pipename);
	ffstr_free(&cmd->globcmd);
	ffmem_free(cmd->http_ctl_options);
	ffslice_free(&cmd->include_files);
	ffslice_free(&cmd->exclude_files);
	ffmem_free(cmd->include_files_data);
	ffmem_free(cmd->exclude_files_data);
	ffmem_free(cmd->playlist_heal);
}
