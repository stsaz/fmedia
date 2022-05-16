/**
Copyright (c) 2015 Simon Zolin
*/

#include <util/string.h>
#include <afilter/pcm.h>
#include <math.h>


static const char pcm_fmtstr[][9] = {
	"float32",
	"float64",
	"int16",
	"int24",
	"int24-4",
	"int32",
	"int8",
};
static const ushort pcm_fmt[] = {
	FFPCM_FLOAT,
	FFPCM_FLOAT64,
	FFPCM_16,
	FFPCM_24,
	FFPCM_24_4,
	FFPCM_32,
	FFPCM_8,
};

const char* ffpcm_fmtstr(uint fmt)
{
	int r = ffarrint16_find(pcm_fmt, FFCNT(pcm_fmt), fmt);
	if (r < 0) {
		FF_ASSERT(0);
		return "";
	}
	return pcm_fmtstr[r];
}

int ffpcm_fmt(const char *sfmt, size_t len)
{
	int r = ffs_findarr3(pcm_fmtstr, sfmt, len);
	if (r < 0)
		return -1;
	return pcm_fmt[r];
}


static const char pcm_channelstr[][7] = {
	"1",
	"2",
	"5.1",
	"7.1",
	"left",
	"mono",
	"right",
	"stereo",
};
static const byte pcm_channels[] = {
	1,
	2,
	6,
	8,
	0x10 | 1,
	1,
	0x20 | 1,
	2,
};

int ffpcm_channels(const char *s, size_t len)
{
	int r = ffs_findarr3(pcm_channelstr, s, len);
	if (r < 0)
		return -1;
	return pcm_channels[r];
}


static const char *const _ffpcm_channelstr[] = {
	"mono", "stereo",
	"3-channel", "4-channel", "5-channel",
	"5.1", "6.1", "7.1"
};

const char* ffpcm_channelstr(uint channels)
{
	return _ffpcm_channelstr[ffmin(channels - 1, FFCNT(_ffpcm_channelstr) - 1)];
}


#define max8f  (128.0)

#ifdef FF_AMD64
#include <emmintrin.h> //SSE2
#endif

/** Convert FP number to integer. */
static FFINL int ffint_ftoi(double d)
{
	int r;

#if defined FF_AMD64
	r = _mm_cvtsd_si32(_mm_load_sd(&d));

#elif defined FF_X86 && !defined FF_MSVC
	__asm__ volatile("fistpl %0"
		: "=m"(r)
		: "t"(d)
		: "st");

#else
	r = (int)((d < 0) ? d - 0.5 : d + 0.5);
#endif

	return r;
}

static FFINL short _ffpcm_flt_8(float f)
{
	double d = f * max8f;
	if (d < -max8f)
		return -0x80;
	else if (d > max8f - 1)
		return 0x7f;
	return ffint_ftoi(d);
}

#define _ffpcm_8_flt(sh)  ((float)(sh) * (1 / max8f))

#define max16f  (32768.0)

static FFINL int _ffint_lim16(int i)
{
	if (i < -0x8000)
		i = -0x8000;
	else if (i > 0x7fff)
		i = 0x7fff;
	return i;
}

static FFINL short _ffpcm_flt_16le(double f)
{
	double d = f * max16f;
	if (d < -max16f)
		return -0x8000;
	else if (d > max16f - 1)
		return 0x7fff;
	return ffint_ftoi(d);
}

#define max24f  (8388608.0)

static FFINL int _ffpcm_flt_24(double f)
{
	double d = f * max24f;
	if (d < -max24f)
		return -0x800000;
	else if (d > max24f - 1)
		return 0x7fffff;
	return ffint_ftoi(d);
}

#define _ffpcm_24_flt(n)  ((double)(n) * (1 / max24f))

#define max32f  (2147483648.0)

static FFINL int _ffpcm_flt_32(double d)
{
	d *= max32f;
	if (d < -max32f)
		return -0x80000000;
	else if (d > max32f - 1)
		return 0x7fffffff;
	return ffint_ftoi(d);
}

#define _ffpcm_32_flt(n)  ((double)(n) * (1 / max32f))

static FFINL double _ffpcm_limf(double d)
{
	if (d > 1.0)
		return 1.0;
	else if (d < -1.0)
		return -1.0;
	return d;
}

union pcmdata {
	char *b;
	short *sh;
	int *in;
	float *f;
	char **pb;
	short **psh;
	int **pin;
	float **pf;
	double **pd;
};

/** Set non-interleaved array from interleaved data. */
static char** pcm_setni(void **ni, void *b, uint fmt, uint nch)
{
	for (uint i = 0;  i != nch;  i++) {
		ni[i] = (char*)b + i * ffpcm_bits(fmt) / 8;
	}
	return (char**)ni;
}


void ffpcm_mix(const ffpcmex *pcm, void *stm1, const void *stm2, size_t samples)
{
	size_t i;
	uint ich, nch = pcm->channels, step = 1;
	void *ini[8], *oni[8];
	union pcmdata u1, u2;

	u1.sh = (short*)stm1;
	u2.sh = (short*)stm2;

	if (pcm->ileaved) {
		u1.pb = pcm_setni(ini, u1.b, pcm->format, nch);
		u2.pb = pcm_setni(oni, u2.b, pcm->format, nch);
		step = nch;
	}

	switch (pcm->format) {
	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u1.psh[ich][i * step] = (short)_ffint_lim16(u1.psh[ich][i * step] + u2.psh[ich][i * step]);
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u1.pf[ich][i * step] = _ffpcm_limf(u1.pf[ich][i * step] + u2.pf[ich][i * step]);
			}
		}
		break;
	}
}

enum CHAN_MASK {
	CHAN_FL = 1,
	CHAN_FR = 2,
	CHAN_FC = 4,
	CHAN_LFE = 8,
	CHAN_BL = 0x10,
	CHAN_BR = 0x20,
	CHAN_SL = 0x40,
	CHAN_SR = 0x80,
};

/** Return channel mask by channels number. */
static uint chan_mask(uint channels)
{
	uint m;
	switch (channels) {
	case 1:
		m = CHAN_FC; break;
	case 2:
		m = CHAN_FL | CHAN_FR; break;
	case 6:
		m = CHAN_FL | CHAN_FR | CHAN_FC | CHAN_LFE | CHAN_BL | CHAN_BR; break;
	case 8:
		m = CHAN_FL | CHAN_FR | CHAN_FC | CHAN_LFE | CHAN_BL | CHAN_BR | CHAN_SL | CHAN_SR; break;
	default:
		return -1;
	}
	return m;
}

/** Set gain level for all used channels. */
static int chan_fill_gain_levels(double level[8][8], uint imask, uint omask)
{
	enum {
		FL,
		FR,
		FC,
		LFE,
		BL,
		BR,
		SL,
		SR,
	};

	const double sqrt1_2 = 0.70710678118654752440; // =1/sqrt(2)

	uint equal = imask & omask;
	for (uint c = 0;  c != 8;  c++) {
		if (equal & FF_BIT32(c))
			level[c][c] = 1;
	}

	uint unused = imask & ~omask;

	if (unused & CHAN_FL) {

		if (omask & CHAN_FC) {
			// front stereo -> front center
			level[FC][FL] = sqrt1_2;
			level[FC][FR] = sqrt1_2;

		} else
			return -1;
	}

	if (unused & CHAN_FC) {

		if (omask & CHAN_FL) {
			// front center -> front stereo
			level[FL][FC] = sqrt1_2;
			level[FR][FC] = sqrt1_2;

		} else
			return -1;
	}

	if (unused & CHAN_LFE) {
	}

	if (unused & CHAN_BL) {

		if (omask & CHAN_FL) {
			// back stereo -> front stereo
			level[FL][BL] = sqrt1_2;
			level[FR][BR] = sqrt1_2;

		} else if (omask & CHAN_FC) {
			// back stereo -> front center
			level[FC][BL] = sqrt1_2*sqrt1_2;
			level[FC][BR] = sqrt1_2*sqrt1_2;

		} else
			return -1;
	}

	if (unused & CHAN_SL) {

		if (omask & CHAN_FL) {
			// side stereo -> front stereo
			level[FL][SL] = sqrt1_2;
			level[FR][SR] = sqrt1_2;

		} else if (omask & CHAN_FC) {
			// side stereo -> front center
			level[FC][SL] = sqrt1_2*sqrt1_2;
			level[FC][SR] = sqrt1_2*sqrt1_2;

		} else
			return -1;
	}

	// now gain level can be >1.0, so we normalize it
	for (uint oc = 0;  oc != 8;  oc++) {
		if (!ffbit_test32(&omask, oc))
			continue;

		double sum = 0;
		for (uint ic = 0;  ic != 8;  ic++) {
			sum += level[oc][ic];
		}
		if (sum != 0) {
			for (uint ic = 0;  ic != 8;  ic++) {
				level[oc][ic] /= sum;
			}
		}

		FFDBG_PRINTLN(10, "channel #%u: %F %F %F %F %F %F %F %F"
			, oc
			, level[oc][0], level[oc][1], level[oc][2], level[oc][3]
			, level[oc][4], level[oc][5], level[oc][6], level[oc][7]);
	}

	return 0;
}

/** Mix (upmix, downmix) channels.
ochan: Output channels number
odata: Output data; float, interleaved

Supported layouts:
1: FC
2: FL+FR
5.1: FL+FR+FC+LFE+BL+BR
7.1: FL+FR+FC+LFE+BL+BR+SL+SR

Examples:

5.1 -> 1:
	FC = FL*0.7 + FR*0.7 + FC*1 + BL*0.5 + BR*0.5

5.1 -> 2:
	FL = FL*1 + FC*0.7 + BL*0.7
	FR = FR*1 + FC*0.7 + BR*0.7
*/
static int chan_mix(uint ochan, void *odata, const ffpcmex *inpcm, const void *idata, size_t samples)
{
	union pcmdata in, out;
	double level[8][8] = {}; // gain level [OUT] <- [IN]
	void *ini[8];
	uint istep, ostep; // intervals between samples of the same channel
	uint ic, oc, ocstm; // channel counters
	uint imask, omask; // channel masks
	size_t i;

	imask = chan_mask(inpcm->channels);
	omask = chan_mask(ochan);
	if (imask == 0 || omask == 0)
		return -1;

	if (0 != chan_fill_gain_levels(level, imask, omask))
		return -1;

	if (samples == 0)
		return 0;

	// set non-interleaved input array
	istep = 1;
	in.pb = (void*)idata;
	if (inpcm->ileaved) {
		pcm_setni(ini, (void*)idata, inpcm->format, inpcm->channels);
		in.pb = (void*)ini;
		istep = inpcm->channels;
	}

	// set interleaved output array
	out.f = odata;
	ostep = ochan;

	ocstm = 0;
	switch (inpcm->format) {
	case FFPCM_16:
		for (oc = 0;  oc != 8;  oc++) {

			if (!ffbit_test32(&omask, oc))
				continue;

			for (i = 0;  i != samples;  i++) {
				double sum = 0;
				uint icstm = 0;
				for (ic = 0;  ic != 8;  ic++) {
					if (!ffbit_test32(&imask, ic))
						continue;
					sum += _ffpcm_16le_flt(in.psh[icstm][i * istep]) * level[oc][ic];
					icstm++;
				}
				out.f[ocstm + i * ostep] = _ffpcm_limf(sum);
			}

			if (++ocstm == ochan)
				break;
		}
		break;

	case FFPCM_32:
		for (oc = 0;  oc != 8;  oc++) {

			if (!ffbit_test32(&omask, oc))
				continue;

			for (i = 0;  i != samples;  i++) {
				double sum = 0;
				uint icstm = 0;
				for (ic = 0;  ic != 8;  ic++) {
					if (!ffbit_test32(&imask, ic))
						continue;
					sum += _ffpcm_32_flt(in.pin[icstm][i * istep]) * level[oc][ic];
					icstm++;
				}
				out.f[ocstm + i * ostep] = _ffpcm_limf(sum);
			}

			if (++ocstm == ochan)
				break;
		}
		break;

	case FFPCM_FLOAT:
		for (oc = 0;  oc != 8;  oc++) {

			if (!ffbit_test32(&omask, oc))
				continue;

			for (i = 0;  i != samples;  i++) {
				double sum = 0;
				uint icstm = 0;
				for (ic = 0;  ic != 8;  ic++) {
					if (!ffbit_test32(&imask, ic))
						continue;
					sum += in.pf[icstm][i * istep] * level[oc][ic];
					icstm++;
				}
				out.f[ocstm + i * ostep] = _ffpcm_limf(sum);
			}

			if (++ocstm == ochan)
				break;
		}
		break;

	default:
		return -1;
	}

	return 0;
}

#define CASE(f1, f2) \
	(f1 << 16) | (f2 & 0xffff)

/*
If channels don't match, do channel conversion:
 . upmix/downmix: mix appropriate channels with each other.  Requires additional memory buffer.
 . mono: copy data for 1 channel only, skip other channels

If format and "interleaved" flags match for both input and output, just copy the data.
Otherwise, process each channel and sample in a loop.

non-interleaved: data[0][..] - left,  data[1][..] - right
interleaved: data[0,2..] - left */
int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples)
{
	size_t i;
	uint ich, nch = inpcm->channels, in_ileaved = inpcm->ileaved;
	union pcmdata from, to;
	void *tmpptr = NULL;
	int r = -1;
	void *ini[8], *oni[8];
	uint istep = 1, ostep = 1;
	uint ifmt;

	from.sh = (void*)in;
	ifmt = inpcm->format;

	to.sh = out;

	if (inpcm->channels > 8 || (outpcm->channels & FFPCM_CHMASK) > 8)
		goto done;

	if (inpcm->sample_rate != outpcm->sample_rate)
		goto done;

	if (inpcm->channels != outpcm->channels) {

		nch = outpcm->channels & FFPCM_CHMASK;

		if (nch == 1 && (outpcm->channels & ~FFPCM_CHMASK) != 0) {
			uint ch = ((outpcm->channels & ~FFPCM_CHMASK) >> 4) - 1;
			if (ch > 1)
				goto done;

			if (!inpcm->ileaved) {
				from.psh = from.psh + ch;

			} else {
				ini[0] = from.b + ch * ffpcm_bits(inpcm->format) / 8;
				from.pb = (void*)ini;
				istep = inpcm->channels;
				in_ileaved = 0;
			}

		} else if ((outpcm->channels & ~FFPCM_CHMASK) == 0) {
			if (NULL == (tmpptr = ffmem_alloc(samples * nch * sizeof(float))))
				goto done;

			if (0 != chan_mix(nch, tmpptr, inpcm, in, samples))
				goto done;

			if (outpcm->ileaved) {
				from.b = tmpptr;
				in_ileaved = 1;

			} else {
				pcm_setni(ini, tmpptr, FFPCM_FLOAT, nch);
				from.pb = (void*)ini;
				istep = nch;
				in_ileaved = 0;
			}
			ifmt = FFPCM_FLOAT;

		} else
			goto done; // this channel conversion is not supported
	}

	if (ifmt == outpcm->format && istep == 1) {
		// input & output formats are the same, try to copy data directly

		if (in_ileaved != outpcm->ileaved && nch == 1) {
			if (samples == 0)
			{}
			else if (!in_ileaved) {
				// non-interleaved input mono -> interleaved input mono
				from.b = from.pb[0];
			} else {
				// interleaved input mono -> non-interleaved input mono
				ini[0] = from.b;
				from.pb = (void*)ini;
			}
			in_ileaved = outpcm->ileaved;
		}

		if (in_ileaved == outpcm->ileaved) {
			if (samples == 0)
				;
			else if (in_ileaved) {
				// interleaved input -> interleaved output
				ffmemcpy(to.b, from.b, samples * ffpcm_size(ifmt, nch));
			} else {
				// non-interleaved input -> non-interleaved output
				for (ich = 0;  ich != nch;  ich++) {
					ffmemcpy(to.pb[ich], from.pb[ich], samples * ffpcm_bits(ifmt)/8);
				}
			}
			r = 0;
			goto done;
		}
	}

	if (in_ileaved) {
		from.pb = pcm_setni(ini, from.b, ifmt, nch);
		istep = nch;
	}

	if (outpcm->ileaved) {
		to.pb = pcm_setni(oni, to.b, outpcm->format, nch);
		ostep = nch;
	}

	switch (CASE(ifmt, outpcm->format)) {

// int8
	case CASE(FFPCM_8, FFPCM_8):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep] = from.pb[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_8, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = (int)from.pb[ich][i * istep] * 0x100;
			}
		}
		break;

// int16
	case CASE(FFPCM_16, FFPCM_8):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep] = from.psh[ich][i * istep] / 0x100;
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = from.psh[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], (int)from.psh[ich][i * istep] * 0x100);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], (int)from.psh[ich][i * istep] * 0x100);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = (int)from.psh[ich][i * istep] * 0x10000;
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_16le_flt(from.psh[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = _ffpcm_16le_flt(from.psh[ich][i * istep]);
			}
		}
		break;

// int24
	case CASE(FFPCM_24, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = ffint_le_cpu24_ptr(&from.pb[ich][i * istep * 3]) / 0x100;
			}
		}
		break;


	case CASE(FFPCM_24, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffmemcpy(&to.pb[ich][i * ostep * 3], &from.pb[ich][i * istep * 3], 3);
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffmemcpy(&to.pb[ich][i * ostep * 4 + 1], &from.pb[ich][i * istep * 3], 3);
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = ffint_le_cpu24_ptr(&from.pb[ich][i * istep * 3]) * 0x100;
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_24_flt(ffint_ltoh24s(&from.pb[ich][i * istep * 3]));
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = _ffpcm_24_flt(ffint_ltoh24s(&from.pb[ich][i * istep * 3]));
			}
		}
		break;

// int32
	case CASE(FFPCM_32, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = from.pin[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], from.pin[ich][i * istep] / 0x100);
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], from.pin[ich][i * istep] / 0x100);
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = from.pin[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_32_flt(from.pin[ich][i * istep]);
			}
		}
		break;

// float32
	case CASE(FFPCM_FLOAT, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = _ffpcm_flt_16le(from.pf[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], _ffpcm_flt_24(from.pf[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], _ffpcm_flt_24(from.pf[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = _ffpcm_flt_32(from.pf[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = from.pf[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = from.pf[ich][i * istep];
			}
		}
		break;

// float64
	case CASE(FFPCM_FLOAT64, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = _ffpcm_flt_16le(from.pd[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], _ffpcm_flt_24(from.pd[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = _ffpcm_flt_32(from.pd[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = from.pd[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = from.pd[ich][i * istep];
			}
		}
		break;

	default:
		goto done;
	}
	r = 0;

done:
	ffmem_safefree(tmpptr);
	return r;
}

#undef CASE


int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples)
{
	uint i, ich, step = 1, nch = pcm->channels;
	void *ini[8], *oni[8];
	union pcmdata from, to;

	if (gain == 1)
		return 0;

	if (pcm->channels > 8)
		return -1;

	from.sh = (void*)in;
	to.sh = out;

	if (pcm->ileaved) {
		from.pb = pcm_setni(ini, from.b, pcm->format, nch);
		to.pb = pcm_setni(oni, to.b, pcm->format, nch);
		step = nch;
	}

	switch (pcm->format) {
	case FFPCM_8:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * step] = _ffpcm_flt_8(_ffpcm_8_flt(from.pb[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * step] = _ffpcm_flt_16le(_ffpcm_16le_flt(from.psh[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&from.pb[ich][i * step * 3]);
				ffint_htol24(&to.pb[ich][i * step * 3], _ffpcm_flt_24(_ffpcm_24_flt(n) * gain));
			}
		}
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * step] = _ffpcm_flt_32(_ffpcm_32_flt(from.pin[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * step] = from.pf[ich][i * step] * gain;
			}
		}
		break;

	case FFPCM_FLOAT64:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * step] = from.pd[ich][i * step] * gain;
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}


int ffpcm_peak(const ffpcmex *fmt, const void *data, size_t samples, double *maxpeak)
{
	double max_f = 0.0;
	uint max_sh = 0;
	uint ich, nch = fmt->channels, step = 1;
	size_t i;
	void *ni[8];
	union pcmdata d;
	d.sh = (void*)data;

	if (fmt->channels > 8)
		return 1;

	if (fmt->ileaved) {
		d.pb = pcm_setni(ni, d.b, fmt->format, nch);
		step = nch;
	}

	switch (fmt->format) {

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				uint sh = ffabs(d.psh[ich][i * step]);
				if (max_sh < sh)
					max_sh = sh;
			}
		}
		max_f = _ffpcm_16le_flt(max_sh);
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&d.pb[ich][i * step * 3]);
				uint u = ffabs(n);
				if (max_sh < u)
					max_sh = u;
			}
		}
		max_f = _ffpcm_24_flt(max_sh);
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_le_cpu32_ptr(&d.pin[ich][i * step]);
				uint u = ffabs(n);
				if (max_sh < u)
					max_sh = u;
			}
		}
		max_f = _ffpcm_32_flt(max_sh);
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				double f = ffabs(d.pf[ich][i * step]);
				if (max_f < f)
					max_f = f;
			}
		}
		break;

	default:
		return 1;
	}

	*maxpeak = max_f;
	return 0;
}

ssize_t ffpcm_process(const ffpcmex *fmt, const void *data, size_t samples, ffpcm_process_func func, void *udata)
{
	double f;
	union pcmdata d;
	void *ni[8];
	uint i, ich, nch = fmt->channels, step = 1, u;

	d.sh = (void*)data;

	if (fmt->channels > 8)
		return -1;

	if (fmt->ileaved) {
		d.pb = pcm_setni(ni, d.b, fmt->format, nch);
		step = nch;
	}

	switch (fmt->format) {

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u = ffabs(d.psh[ich][i * step]);
				f = _ffpcm_16le_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&d.pb[ich][i * step * 3]);
				u = ffabs(n);
				f = _ffpcm_24_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_le_cpu32_ptr(&d.pin[ich][i * step]);
				u = ffabs(n);
				f = _ffpcm_32_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				f = ffabs(d.pf[ich][i * step]);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	default:
		return -2;
	}

	return -1;

done:
	return i;
}
