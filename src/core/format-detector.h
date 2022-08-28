/** fmedia: detect file format from file data
2020, Simon Zolin */

enum FILE_FORMAT {
	FILE_UNK,
	FILE_AVI,
	FILE_CAF,
	FILE_FLAC,
	FILE_MKV,
	FILE_MP3,
	FILE_MP4,
	FILE_OGG,
	FILE_WAV,
	FILE_WV,
};

static const char file_ext[][5] = {
	"avi",
	"caf",
	"flac",
	"mkv",
	"mp3",
	"mp4",
	"ogg",
	"wav",
	"wv",
};

/** Detect file format by first several bytes
len: >=12
Return enum FILE_FORMAT */
static inline int file_format_detect(const void *data, ffsize len)
{
	const ffbyte *d = data;
	if (len >= 12) {
		// byte id[4]; // "RIFF"
		// byte size[4];
		// byte wave[4]; // "WAVE"
		if (!ffmem_cmp(&d[0], "RIFF", 4)
			&& !ffmem_cmp(&d[8], "WAVE", 4))
			return FILE_WAV;
	}

	if (len >= 12) {
		// byte id[4]; // "RIFF"
		// byte size[4];
		// byte wave[4]; // "AVI "
		if (!ffmem_cmp(&d[0], "RIFF", 4)
			&& !ffmem_cmp(&d[8], "AVI ", 4))
			return FILE_AVI;
	}

	if (len >= 10) {
		// id[4] // "wvpk"
		// size[4]
		// ver[2] // "XX 04"
		if (!ffmem_cmp(&d[0], "wvpk", 4)
			&& d[9] == 0x04)
			return FILE_WV;
	}

	if (len >= 8) {
		// byte size[4];
		// byte type[4]; // "ftyp"
		if (!ffmem_cmp(&d[4], "ftyp", 4)
			&& ffint_be_cpu32_ptr(&d[0]) <= 255)
			return FILE_MP4;
	}

	if (len >= 8) {
		// char caff[4]; // "caff"
		// ffbyte ver[2]; // =1
		// ffbyte flags[2]; // =0
		if (!ffmem_cmp(d, "caff\x00\x01\x00\x00", 8))
			return FILE_CAF;
	}

	if (len >= 5) {
		// byte sync[4]; // "OggS"
		// byte ver; // 0x0
		if (!ffmem_cmp(&d[0], "OggS", 4)
			&& d[4] == 0)
			return FILE_OGG;
	}

	if (len >= 5) {
		// byte sig[4]; // "fLaC"
		// byte last_type; // [1] [7]
		if (!ffmem_cmp(&d[0], "fLaC", 4)
			&& (d[4] & 0x7f) < 9)
			return FILE_FLAC;
	}

	if (len >= 5) {
		// ID3v2 (.mp3)
		// byte id3[3]; // "ID3"
		// byte ver[2]; // e.g. 0x3 0x0
		if (!ffmem_cmp(&d[0], "ID3", 3)
			&& d[3] <= 9
			&& d[4] <= 9)
			return FILE_MP3;
	}

	if (len >= 4) {
		// byte id[4] // 1a45dfa3
		if (!ffmem_cmp(d, "\x1a\x45\xdf\xa3", 4))
			return FILE_MKV;
	}

	if (len >= 2) {
		// byte sync1; // 0xff
		// byte sync2_ver_layer_noprotect; // [3]=0x7 [2]=0x3 [2]=0x1 [1]
		if (d[0] == 0xff && (d[1] & 0xe0) == 0xe0
			&& (d[1] & 0x18) == 0x18
			&& (d[1] & 0x06) == 2)
			return FILE_MP3;
	}

	return FILE_UNK;
}

static void* fdetcr_open(fmed_filt *d)
{
	return FMED_FILT_DUMMY;
}
static void fdetcr_close(void *ctx)
{
}

static int fdetcr_process(void *ctx, fmed_filt *d)
{
	int r = file_format_detect(d->data_in.ptr, d->data_in.len);
	if (r == FILE_UNK) {
		d->flags |= FMED_E_UNKIFMT;
		errlog1(d->trk, "unknown file format");
		return FMED_RERR;
	}

	ffstr ext = FFSTR_INITZ(file_ext[r-1]);
	dbglog0("detected format: %S", &ext);

	const fmed_modinfo *mi = core->getmod2(FMED_MOD_INEXT, ext.ptr, ext.len);
	if (mi == NULL || ffsz_eq(mi->name, "#core.format-detector")) {
		errlog1(d->trk, "no module configured for file format '%S'", &ext);
		return FMED_RERR;
	}
	d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, mi->name);

	d->data_out = d->data_in;
	return FMED_RDONE;
}

const fmed_filter _fmed_format_detector = {
	&fdetcr_open, &fdetcr_process, &fdetcr_close
};
