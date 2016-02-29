/** Terminal UI.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FFOS/thread.h>
#include <FFOS/error.h>


struct tui;

typedef struct gtui {
	const fmed_queue *qu;
	const fmed_track *track;

	fflock lktrk;
	struct tui *curtrk;

	uint vol;
	fftask cmdtask;

	ffthd th;
} gtui;

static gtui *gt;

typedef struct tui {
	uint state;
	void *trk;
	uint64 total_samples;
	uint64 played_samples;
	uint lastpos;
	uint sample_rate;
	uint sampsize;
	uint total_time_sec;
	ffstr3 buf;

	fflock lkcmds;
	ffarr cmds; // queued commands from gtui.  tcmd[]

	uint goback :1;
} tui;

enum {
	SEEK_STEP = 5 * 1000,
	SEEK_STEP_MED = 15 * 1000,
	SEEK_STEP_LARGE = 60 * 1000,

	VOL_STEP = 5,
	VOL_MAX = 125,
	VOL_LO = /*-*/48,
	VOL_HI = 6,
};

enum CMDS {
	CMD_PLAY,
	CMD_STOP,
	CMD_NEXT,
	CMD_PREV,
	CMD_SEEKRIGHT,
	CMD_SEEKLEFT,
	CMD_VOL,
	CMD_VOLUP,
	CMD_VOLDOWN,

	CMD_RM,
	CMD_DELFILE,

	CMD_QUIT,

	_CMD_F1 = 1 << 31, //use 'cmdfunc1'
};

typedef void (*cmdfunc)(tui *t, uint cmd);
typedef void (*cmdfunc1)(uint cmd);

typedef struct tcmd {
	uint cmd; //enum CMDS
	cmdfunc func;
} tcmd;

static const fmed_core *core;

//FMEDIA MODULE
static const void* tui_iface(const char *name);
static int tui_sig(uint signo);
static void tui_destroy(void);
static const fmed_mod fmed_tui_mod = {
	&tui_iface, &tui_sig, &tui_destroy
};

static void* tui_open(fmed_filt *d);
static int tui_process(void *ctx, fmed_filt *d);
static void tui_close(void *ctx);
static const fmed_filter fmed_tui = {
	&tui_open, &tui_process, &tui_close
};

static void tui_info(tui *t, fmed_filt *d);
static int tui_cmdloop(void *param);
static void tui_help(uint cmd);
static void tui_addtask(uint cmd);
static void tui_task(void *param);
static void tui_rmfile(tui *t, uint cmd);
static void tui_vol(tui *t, uint cmd);
static void tui_seek(tui *t, uint cmd);
static void tui_addcmd(cmdfunc func, uint cmd);


#ifdef FF_WIN
enum {
	FFKEY_UP = 0x100,
	FFKEY_DOWN,
	FFKEY_RIGHT,
	FFKEY_LEFT,

	FFKEY_CTRL = 0x1 << 24,
	FFKEY_SHIFT = 0x2 << 24,
	FFKEY_ALT = 0x4 << 24,
	FFKEY_MODMASK = FFKEY_CTRL | FFKEY_SHIFT | FFKEY_ALT,
};

static int ffstd_key(const char *data, size_t *len)
{
	*len = 1;
	return data[0];
}

#endif


const fmed_mod* fmed_getmod_tui(const fmed_core *_core)
{
	core = _core;
	return &fmed_tui_mod;
}


static const void* tui_iface(const char *name)
{
	if (!ffsz_cmp(name, "tui"))
		return &fmed_tui;
	return NULL;
}

static int tui_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		gt = ffmem_tcalloc1(gtui);
		fflk_init(&gt->lktrk);
		gt->vol = 100;
		gt->cmdtask.handler = &tui_task;
		if (NULL == (gt->qu = core->getmod("#queue.queue")))
			return 1;
		if (NULL == (gt->track = core->getmod("#core.track")))
			return 1;

		ffstd_keypress(ffstdin, 1);
		ffstd_echo(ffstdin, 0);
		if (FFTHD_INV == (gt->th = ffthd_create(&tui_cmdloop, NULL, 0))) {
			return 1;
		}
		break;
	}
	return 0;
}

static void tui_destroy(void)
{
	if (gt == NULL)
		return;
	if (gt->th != FFTHD_INV)
		ffthd_detach(gt->th);
	ffstd_echo(ffstdin, 1);
	ffstd_keypress(ffstdin, 0);
	ffmem_safefree(gt);
}


static void* tui_open(fmed_filt *d)
{
	tui *t = ffmem_tcalloc1(tui);
	if (t == NULL)
		return NULL;
	fflk_init(&t->lkcmds);
	t->lastpos = (uint)-1;

	t->total_samples = d->track->getval(d->trk, "total_samples");
	tui_info(t, d);

	if (FMED_NULL != d->track->getval(d->trk, "input_info")) {
		tui_close(t);
		return FMED_FILT_DUMMY;
	}

	t->trk = d->trk;
	fflk_lock(&gt->lktrk);
	gt->curtrk = t;
	fflk_unlock(&gt->lktrk);

	if (gt->vol != 100)
		tui_vol(t, CMD_VOL);
	return t;
}

static void tui_close(void *ctx)
{
	tui *t = ctx;

	if (ctx == FMED_FILT_DUMMY)
		return;

	if (t == gt->curtrk) {
		fflk_lock(&gt->lktrk);
		gt->curtrk = NULL;
		fflk_unlock(&gt->lktrk);
	}
	ffarr_free(&t->buf);
	ffmem_free(t);
}

static void tui_info(tui *t, fmed_filt *d)
{
	uint64 total_time, tsize;
	uint tmsec;
	const char *input;
	ffstr *tstr, artist = {0}, title = {0};
	ffpcm fmt;
	fmed_que_entry *qent;

	fmed_getpcm(d, &fmt);
	t->sample_rate = fmt.sample_rate;
	t->sampsize = ffpcm_size1(&fmt);

	if (FMED_PNULL == (input = d->track->getvalstr(d->trk, "input")))
		return;

	qent = (void*)fmed_getval("queue_item");

	total_time = (t->total_samples != FMED_NULL) ? ffpcm_time(t->total_samples, t->sample_rate) : 0;
	tmsec = (uint)(total_time / 1000);
	t->total_time_sec = tmsec;

	tsize = d->track->getval(d->trk, "total_size");
	if (tsize == FMED_NULL)
		tsize = 0;

	if (NULL != (tstr = gt->qu->meta_find(qent, "artist", -1)))
		artist = *tstr;

	if (NULL != (tstr = gt->qu->meta_find(qent, "title", -1)))
		title = *tstr;

	t->buf.len = 0;
	ffstr_catfmt(&t->buf, "\n\"%S - %S\" %s %.02F MB, %u:%02u.%03u (%,U samples), %u kbps, %u Hz, %u bit, %s\n\n"
		, &artist, &title
		, input
		, (double)tsize / (1024 * 1024)
		, tmsec / 60, tmsec % 60, (uint)(total_time % 1000)
		, t->total_samples
		, (int)(d->track->getval(d->trk, "bitrate") / 1000)
		, fmt.sample_rate
		, ffpcm_bits(fmt.format)
		, ffpcm_channelstr(fmt.channels));

	if (1 == core->getval("show_tags")) {
		uint i;
		ffstr name, *val;
		for (i = 0;  NULL != (val = gt->qu->meta(qent, i, &name, 0));  i++) {
			ffstr_catfmt(&t->buf, "%S\t%S\n", &name, val);
		}
	}

	ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
	t->buf.len = 0;
}

static void tui_seek(tui *t, uint cmd)
{
	int64 pos = ffpcm_time(gt->track->getval(t->trk, "current_position"), t->sample_rate);
	uint by;
	switch (cmd & FFKEY_MODMASK) {
	case 0:
		by = SEEK_STEP;
		break;
	case FFKEY_ALT:
		by = SEEK_STEP_MED;
		break;
	case FFKEY_CTRL:
		by = SEEK_STEP_LARGE;
		break;
	default:
		return;
	}
	if ((cmd & ~FFKEY_MODMASK) == CMD_SEEKRIGHT)
		pos += by;
	else
		pos = ffmax(pos - by, 0);
	gt->track->setval(t->trk, "seek_time", pos);
	gt->track->setval(t->trk, "snd_output_clear", 1);
	t->goback = 1;
}

static void tui_vol(tui *t, uint cmd)
{
	int db;

	switch (cmd & ~FFKEY_MODMASK) {
	case CMD_VOLUP:
		gt->vol = ffmin(gt->vol + VOL_STEP, VOL_MAX);
		break;

	case CMD_VOLDOWN:
		gt->vol = ffmax((int)gt->vol - VOL_STEP, 0);
		break;
	}

	if (gt->vol <= 100)
		db = ffpcm_vol2db(gt->vol, VOL_LO) * 100;
	else
		db = ffpcm_vol2db_inc(gt->vol - 100, VOL_MAX - 100, VOL_HI) * 100;
	gt->track->setval(t->trk, "gain", db);
}

static void tui_rmfile(tui *t, uint cmd)
{
	fmed_que_entry *qtrk = (void*)gt->track->getval(t->trk, "queue_item");
	if ((cmd & ~FFKEY_MODMASK) == CMD_DELFILE) {
		ffarr fn = {0};
		if (0 != ffstr_catfmt(&fn, "%S.deleted%Z", &qtrk->url)
			&& 0 == fffile_rename(qtrk->url.ptr, fn.ptr))
			gt->qu->cmd(FMED_QUE_RM, qtrk);
		else
			syserrlog(core, t->trk, "tui", "%s", "can't rename file");
		ffarr_free(&fn);
	} else
		gt->qu->cmd(FMED_QUE_RM, qtrk);
}

static int tui_process(void *ctx, fmed_filt *d)
{
	tui *t = ctx;
	int64 playpos;
	uint playtime;
	uint dots = 70;

	if (ctx == FMED_FILT_DUMMY)
		return FMED_RFIN;

	uint nback = (uint)t->buf.len;

	if (t->cmds.len != 0) {
		uint i;
		fflk_lock(&t->lkcmds);
		const tcmd *pcmd = (void*)t->cmds.ptr;
		for (i = 0;  i != t->cmds.len;  i++) {
			pcmd[i].func(t, pcmd[i].cmd);
		}
		t->cmds.len = 0;
		fflk_unlock(&t->lkcmds);

		if (t->goback) {
			t->goback = 0;
			return FMED_RMORE;
		}
	}

	if (core->loglev & FMED_LOG_DEBUG)
		nback = 0;

	if (FMED_NULL == (playpos = fmed_getval("current_position")))
		playpos = t->played_samples;
	playtime = (uint)(ffpcm_time(playpos, t->sample_rate) / 1000);
	if (playtime == t->lastpos)
		goto done;
	t->lastpos = playtime;

	if (t->total_samples == FMED_NULL
		|| (uint64)playpos >= t->total_samples) {

		t->buf.len = 0;
		ffstr_catfmt(&t->buf, "%*c%u:%02u"
			, (size_t)nback, '\b'
			, playtime / 60, playtime % 60);
		goto print;
	}

	t->buf.len = 0;
	ffstr_catfmt(&t->buf, "%*c[%*c%*c] %u:%02u / %u:%02u"
		, (size_t)nback, '\b'
		, (size_t)(playpos * dots / t->total_samples), '='
		, (size_t)(dots - (playpos * dots / t->total_samples)), '.'
		, playtime / 60, playtime % 60
		, t->total_time_sec / 60, t->total_time_sec % 60);

print:
	fffile_write(ffstderr, t->buf.ptr, t->buf.len);
	t->buf.len -= nback; //don't count the number of '\b'

done:
	t->played_samples += d->datalen / t->sampsize;

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}

static void tui_addtask(uint cmd)
{
	gt->cmdtask.param = (void*)(size_t)cmd;
	core->task(&gt->cmdtask, FMED_TASK_POST);
}

static void tui_task(void *param)
{
	uint cmd = (uint)(size_t)param;
	switch (cmd) {
	case CMD_STOP:
		gt->track->cmd(NULL, FMED_TRACK_STOPALL);
		break;

	case CMD_NEXT:
		gt->track->cmd(NULL, FMED_TRACK_STOPALL);
		gt->qu->cmd(FMED_QUE_NEXT, NULL);
		break;

	case CMD_PREV:
		gt->track->cmd(NULL, FMED_TRACK_STOPALL);
		gt->qu->cmd(FMED_QUE_PREV, NULL);
		break;

	case CMD_PLAY:
		if (gt->curtrk == NULL)
			gt->qu->cmd(FMED_QUE_PLAY, NULL);
		break;

	case CMD_QUIT:
		gt->track->cmd(NULL, FMED_TRACK_STOPALL_EXIT);
		break;
	}
}


struct key {
	uint key;
	uint cmd;
	void *func; // cmdfunc | cmdfunc1
};

static struct key hotkeys[] = {
	{ ' ',	CMD_PLAY | _CMD_F1,	&tui_addtask },
	{ 'D',	CMD_DELFILE,	&tui_rmfile },
	{ 'd',	CMD_RM,	&tui_rmfile },
	{ 'h',	_CMD_F1,	&tui_help },
	{ 'n',	CMD_NEXT | _CMD_F1,	&tui_addtask },
	{ 'p',	CMD_PREV | _CMD_F1,	&tui_addtask },
	{ 'q',	CMD_QUIT | _CMD_F1,	&tui_addtask },
	{ 's',	CMD_STOP | _CMD_F1,	&tui_addtask },
	{ FFKEY_UP,	CMD_VOLUP,	&tui_vol },
	{ FFKEY_DOWN,	CMD_VOLDOWN,	&tui_vol },
	{ FFKEY_RIGHT,	CMD_SEEKRIGHT,	&tui_seek },
	{ FFKEY_LEFT,	CMD_SEEKLEFT,	&tui_seek },
};

static const struct key* key2cmd(int key)
{
	size_t i, start = 0;
	uint k = (key & ~FFKEY_MODMASK), n = FFCNT(hotkeys);
	while (start != n) {
		i = start + (n - start) / 2;
		if (k == hotkeys[i].key) {
			return &hotkeys[i];
		} else if (k < hotkeys[i].key)
			n = i;
		else
			start = i + 1;
	}
	return NULL;
}

static void tui_help(uint cmd)
{
	char buf[4096];
	ssize_t n;
	char *fn;
	fffd f;

	if (NULL == (fn = core->getpath(FFSTR("help-tui.txt"))))
		return;

	f = fffile_open(fn, O_RDONLY | O_NOATIME);
	ffmem_free(fn);
	if (f == FF_BADFD)
		return;
	n = fffile_read(f, buf, sizeof(buf));
	fffile_close(f);
	if (n > 0)
		fffile_write(ffstdout, buf, n);
}

static void tui_addcmd(cmdfunc func, uint cmd)
{
	fflk_lock(&gt->lktrk);
	if (gt->curtrk == NULL)
		goto end;

	fflk_lock(&gt->curtrk->lkcmds);
	struct tcmd *pcmd = ffarr_push(&gt->curtrk->cmds, struct tcmd);
	if (pcmd != NULL) {
		pcmd->cmd = cmd;
		pcmd->func = func;
	}
	fflk_unlock(&gt->curtrk->lkcmds);

end:
	fflk_unlock(&gt->lktrk);
}

static int tui_cmdloop(void *param)
{
	char buf[128];

	for (;;) {
		ssize_t r = fffile_read(ffstdin, buf, sizeof(buf));
		if (r <= 0)
			break;
		size_t len = r;
		int key = ffstd_key(buf, &len);

		const struct key *k = key2cmd(key);
		if (k == NULL)
			continue;

		if (k->cmd & _CMD_F1) {
			cmdfunc1 func1 = (void*)k->func;
			func1(k->cmd & ~_CMD_F1);

		} else {
			tui_addcmd((void*)k->func, (k->cmd & ~_CMD_F1) | (key & FFKEY_MODMASK));
		}
	}
	return 0;
}
