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
	double maxdb;

	uint goback :1
		, rec :1
		, conversion :1
		;
} tui;

static struct tui_conf_t {
	byte echo_off;
} tui_conf;

enum {
	SEEK_STEP = 5 * 1000,
	SEEK_STEP_MED = 15 * 1000,
	SEEK_STEP_LARGE = 60 * 1000,

	VOL_STEP = 5,
	VOL_MAX = 125,
	VOL_LO = /*-*/48,
	VOL_HI = 6,

	MINDB = 40,
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
	CMD_SHOWTAGS,

	CMD_QUIT,

	CMD_MASK = 0xff,

	_CMD_F3 = 1 << 27, //use 'cmdfunc3'
	_CMD_CURTRK = 1 << 28,
	_CMD_CORE = 1 << 29,
	_CMD_PLAYONLY = 1 << 30,
	_CMD_F1 = 1 << 31, //use 'cmdfunc1'
};

typedef void (*cmdfunc)(tui *t, uint cmd);
typedef void (*cmdfunc3)(tui *t, uint cmd, void *udata);
typedef void (*cmdfunc1)(uint cmd);

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
static int tui_config(ffpars_ctx *conf);
static const fmed_filter fmed_tui = {
	&tui_open, &tui_process, &tui_close, &tui_config
};

static void tui_print_peak(tui *t, fmed_filt *d);
static void tui_info(tui *t, fmed_filt *d);
static int tui_cmdloop(void *param);
static void tui_help(uint cmd);
static void tui_rmfile(tui *t, uint cmd);
static void tui_vol(tui *t, uint cmd);
static void tui_seek(tui *t, uint cmd, void *udata);

struct key;
static void tui_corecmd(void *param);
static void tui_corecmd_add(const struct key *k, void *udata);


static const ffpars_arg tui_conf_args[] = {
	{ "echo_off",	FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct tui_conf_t, echo_off) },
};


const fmed_mod* fmed_getmod_tui(const fmed_core *_core)
{
	core = _core;
	return &fmed_tui_mod;
}


static const void* tui_iface(const char *name)
{
	if (!ffsz_cmp(name, "tui")) {
		tui_conf.echo_off = 1;
		return &fmed_tui;
	}
	return NULL;
}

static int tui_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		gt = ffmem_tcalloc1(gtui);
		fflk_init(&gt->lktrk);
		gt->vol = 100;
		if (NULL == (gt->qu = core->getmod("#queue.queue")))
			return 1;
		if (NULL == (gt->track = core->getmod("#core.track")))
			return 1;

		{
		uint attr = FFSTD_LINEINPUT;
		if (tui_conf.echo_off)
			attr |= FFSTD_ECHO;
		ffstd_attr(ffstdin, attr, 0);
		}

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

	uint attr = FFSTD_LINEINPUT;
	if (tui_conf.echo_off)
		attr |= FFSTD_ECHO;
	ffstd_attr(ffstdin, attr, attr);

	ffmem_safefree(gt);
}


static int tui_config(ffpars_ctx *conf)
{
	ffpars_setargs(conf, &tui_conf, tui_conf_args, FFCNT(tui_conf_args));
	return 0;
}

static void* tui_open(fmed_filt *d)
{
	tui *t = ffmem_tcalloc1(tui);
	if (t == NULL)
		return NULL;
	t->lastpos = (uint)-1;

	int type = fmed_getval("type");
	if (type == FMED_TRK_TYPE_REC) {
		t->rec = 1;
		t->maxdb = -MINDB;
	}

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

	if (FMED_PNULL != d->track->getvalstr(d->trk, "output"))
		t->conversion = 1;
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

static void tui_addtags(tui *t, fmed_que_entry *qent, ffarr *buf)
{
	ffstr name, *val;
	for (uint i = 0;  NULL != (val = gt->qu->meta(qent, i, &name, 0));  i++) {
		ffstr_catfmt(buf, "%S\t%S\n", &name, val);
	}
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

	total_time = ((int64)t->total_samples != FMED_NULL) ? ffpcm_time(t->total_samples, t->sample_rate) : 0;
	tmsec = (uint)(total_time / 1000);
	t->total_time_sec = tmsec;

	tsize = d->track->getval(d->trk, "total_size");
	if ((int64)tsize == FMED_NULL)
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
		, (int)((d->track->getval(d->trk, "bitrate") + 500) / 1000)
		, fmt.sample_rate
		, ffpcm_bits(fmt.format)
		, ffpcm_channelstr(fmt.channels));

	if (1 == core->getval("show_tags")) {
		tui_addtags(t, qent, &t->buf);
	}

	ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
	t->buf.len = 0;
}

static void tui_seek(tui *t, uint cmd, void *udata)
{
	int64 pos = ffpcm_time(gt->track->getval(t->trk, "current_position"), t->sample_rate);
	uint by;
	switch ((size_t)udata & FFKEY_MODMASK) {
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
	if (cmd == CMD_SEEKRIGHT)
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
	if ((cmd & CMD_MASK) == CMD_DELFILE) {
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

static void tui_print_peak(tui *t, fmed_filt *d)
{
	int pk;
	if (FMED_NULL == (pk = (int)fmed_getval("pcm_peak")))
		return;

	double db = ((double)pk) / 100;
	if (db < -MINDB)
		db = -MINDB;
	if (t->maxdb < db)
		t->maxdb = db;
	size_t pos = ((MINDB + db) / MINDB) * 10;
	ffstr_catfmt(&t->buf, "  [%*c%*c] %.02FdB / %.02FdB  "
		, pos, '='
		, (size_t)(10 - pos), '.'
		, db, t->maxdb);
}

static int tui_process(void *ctx, fmed_filt *d)
{
	tui *t = ctx;
	int64 playpos;
	uint playtime;
	uint dots = 70;
	int pk, val;

	if (ctx == FMED_FILT_DUMMY)
		return FMED_RFIN;

	uint nback = (uint)t->buf.len;

	if (t->goback) {
		t->goback = 0;
		return FMED_RMORE;
	}

	if (FMED_NULL != (val = fmed_popval("meta-changed"))) {
		tui_info(t, d);
	}

	if (core->loglev & FMED_LOG_DEBUG)
		nback = 0;

	if (FMED_NULL == (playpos = fmed_getval("current_position")))
		playpos = t->played_samples;
	playtime = (uint)(ffpcm_time(playpos, t->sample_rate) / 1000);
	if (playtime == t->lastpos) {

		if (t->rec
			&& FMED_NULL != (pk = (int)fmed_getval("pcm_peak"))) {
			double db = ((double)pk) / 100;
			if (db < -40)
				db = -40;
			if (t->maxdb < db)
				t->maxdb = db;
		}

		goto done;
	}
	t->lastpos = playtime;

	if ((int64)t->total_samples == FMED_NULL
		|| (uint64)playpos >= t->total_samples) {

		t->buf.len = 0;
		ffstr_catfmt(&t->buf, "%*c%u:%02u"
			, (size_t)nback, '\b'
			, playtime / 60, playtime % 60);

		if (t->rec) {
			tui_print_peak(t, d);
		}

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

static void tui_op(uint cmd)
{
	switch (cmd) {
	case CMD_STOP:
		gt->track->cmd((void*)-1, FMED_TRACK_STOPALL);
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

	case CMD_SHOWTAGS: {
		tui *t = gt->curtrk;
		if (t == NULL)
			break;
		t->buf.len = 0;
		tui_addtags(t, (void*)gt->track->getval(t->trk, "queue_item"), &t->buf);
		ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
		t->buf.len = 0;
		break;
	}
	}
}


struct key {
	uint key;
	uint cmd;
	void *func; // cmdfunc | cmdfunc1
};

static struct key hotkeys[] = {
	{ ' ',	CMD_PLAY | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'D',	CMD_DELFILE | _CMD_CURTRK | _CMD_CORE,	&tui_rmfile },
	{ 'd',	CMD_RM | _CMD_CURTRK | _CMD_CORE,	&tui_rmfile },
	{ 'h',	_CMD_F1,	&tui_help },
	{ 'i',	CMD_SHOWTAGS | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'n',	CMD_NEXT | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'p',	CMD_PREV | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'q',	CMD_QUIT | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 's',	CMD_STOP | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ FFKEY_UP,	CMD_VOLUP | _CMD_CURTRK | _CMD_CORE | _CMD_PLAYONLY,	&tui_vol },
	{ FFKEY_DOWN,	CMD_VOLDOWN | _CMD_CURTRK | _CMD_CORE | _CMD_PLAYONLY,	&tui_vol },
	{ FFKEY_RIGHT,	CMD_SEEKRIGHT | _CMD_F3 | _CMD_CORE | _CMD_PLAYONLY,	&tui_seek },
	{ FFKEY_LEFT,	CMD_SEEKLEFT | _CMD_F3 | _CMD_CORE | _CMD_PLAYONLY,	&tui_seek },
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

struct corecmd {
	fftask tsk;
	const struct key *k;
	void *udata;
};

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

static void tui_corecmd(void *param)
{
	struct corecmd *c = param;

	if ((c->k->cmd & _CMD_PLAYONLY) && gt->curtrk != NULL && gt->curtrk->conversion)
		goto done;

	if (c->k->cmd & _CMD_F1) {
		cmdfunc1 func1 = (void*)c->k->func;
		func1(c->k->cmd & CMD_MASK);

	} else if ((c->k->cmd & _CMD_F3) && gt->curtrk != NULL) {
		cmdfunc3 func3 = (void*)c->k->func;
		func3(gt->curtrk, c->k->cmd & CMD_MASK, c->udata);

	} else if ((c->k->cmd & _CMD_CURTRK) && gt->curtrk != NULL) {
		cmdfunc func = (void*)c->k->func;
		func(gt->curtrk, c->k->cmd & CMD_MASK);
	}

done:
	ffmem_free(c);
}

static void tui_corecmd_add(const struct key *k, void *udata)
{
	struct corecmd *c = ffmem_tcalloc1(struct corecmd);
	if (c == NULL) {
		syserrlog(core, NULL, "tui", "alloc");
		return;
	}
	c->tsk.handler = &tui_corecmd;
	c->tsk.param = c;
	c->udata = udata;
	c->k = k;
	core->task(&c->tsk, FMED_TASK_POST);
}

static int tui_cmdloop(void *param)
{
	ffstd_ev ev = {0};
	int r;

	for (;;) {
		r = ffstd_event(ffstdin, &ev);
		if (r == 0)
			continue;
		else if (r < 0)
			break;

		uint key = ffstd_key(ev.data, &ev.datalen);

		const struct key *k = key2cmd(key);
		if (k == NULL)
			continue;

		if (k->cmd & _CMD_CORE) {
			void *udata = NULL;
			if (k->cmd & _CMD_F3)
				udata = (void*)(size_t)key;
			tui_corecmd_add(k, udata);

		} else if (k->cmd & _CMD_F1) {
			cmdfunc1 func1 = (void*)k->func;
			func1(k->cmd & ~_CMD_F1);
		}
	}
	return 0;
}
