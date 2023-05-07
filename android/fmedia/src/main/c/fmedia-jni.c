/** fmedia/Android
2022, Simon Zolin */

#include <fmedia.h>

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, NULL, __VA_ARGS__)
#define syswarnlog0(...)  fmed_syswarnlog(core, NULL, NULL, __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, NULL, __VA_ARGS__)

#include "log.h"
#include "ctl.h"
#include "jni-helper.h"
#include <util/fntree.h>
#include <util/path.h>
#include <FFOS/perf.h>
#include <FFOS/ffos-extern.h>

typedef struct fmedia_ctx fmedia_ctx;
struct fmedia_ctx {
	const fmed_track *track;
	const fmed_queue *qu;
	const fmed_mod *qumod;
	JavaVM *jvm;
	jmethodID Fmedia_Callback_on_finish;
	uint list_filter;
};
static struct fmedia_ctx *fx;
static JavaVM *jvm;

extern const fmed_core *core;
fmed_core* core_init();
extern void core_destroy();

static int file_trash(const char *trash_dir, const char *fn);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);
static int qu_add_dir_r(const char *fn, int qi);
static void qu_load_file(const char *fn, int qi);

JNIEXPORT jint JNI_OnLoad(JavaVM *_jvm, void *reserved)
{
	jvm = _jvm;
	return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_init(JNIEnv *env, jobject thiz)
{
	FF_ASSERT(fx == NULL);
	fx = ffmem_new(fmedia_ctx);
	fx->jvm = jvm;
	fmed_core *core = core_init();
	core->loglev = FMED_LOG_INFO;
#ifdef FMED_DEBUG
	core->loglev = FMED_LOG_DEBUG;
#endif
	core->log = adrd_log;
	core->logv = adrd_logv;
	fx->track = core->getmod("core.track");

	fx->qumod = fmed_getmod_queue(core);
	fx->qumod->sig(FMED_SIG_INIT);
	fx->qu = core->getmod("core.queue");
	dbglog0("%s: exit", __func__);
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_destroy(JNIEnv *env, jobject thiz)
{
	dbglog0("%s: enter", __func__);
	fx->qumod->destroy();
	core_destroy();
	ffmem_free(fx);
	fx = NULL;
	dbglog0("%s: exit", __func__);
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_setCodepage(JNIEnv *env, jobject thiz, jstring jcodepage)
{
	const char *sz = jni_sz_js(jcodepage);
	fmed_core *_core = (fmed_core*)core;
	if (ffsz_eq(sz, "cp1251"))
		_core->props->codepage = FFUNICODE_WIN1251;
	else if (ffsz_eq(sz, "cp1252"))
		_core->props->codepage = FFUNICODE_WIN1252;
	jni_sz_free(sz, jcodepage);
}

static const char* channel_str(uint channels)
{
	static const char _channel_str[][8] = {
		"mono", "stereo",
		"3.0", "4.0", "5.0",
		"5.1", "6.1", "7.1"
	};
	channels = ffmin(channels - 1, FF_COUNT(_channel_str) - 1);
	return _channel_str[channels];
}

static uint64 info_add(ffvec *info, const fmed_track_info *ti)
{
	*ffvec_pushT(info, char*) = ffsz_dup("url");
	*ffvec_pushT(info, char*) = ffsz_dup(ti->in_filename);

	*ffvec_pushT(info, char*) = ffsz_dup("size");
	*ffvec_pushT(info, char*) = ffsz_allocfmt("%UKB", ti->input.size / 1024);

	*ffvec_pushT(info, char*) = ffsz_dup("file time");
	char mtime[100];
	ffdatetime dt = {};
	fftime_split1(&dt, &ti->in_mtime);
	int r = fftime_tostr1(&dt, mtime, sizeof(mtime)-1, FFTIME_YMD);
	mtime[r] = '\0';
	*ffvec_pushT(info, char*) = ffsz_dup(mtime);

	uint64 msec = 0;
	if (ti->audio.fmt.sample_rate != 0)
		msec = ffpcm_time(ti->audio.total, ti->audio.fmt.sample_rate);
	uint sec = msec / 1000;
	*ffvec_pushT(info, char*) = ffsz_dup("length");
	*ffvec_pushT(info, char*) = ffsz_allocfmt("%u:%02u.%03u (%,U samples)"
		, sec / 60, sec % 60, (int)(msec % 1000)
		, (int64)ti->audio.total);

	return msec;
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_meta(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	fftime t1 = fftime_monotonic();
	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);
	jclass jc = jni_class_obj(thiz);

	ti->input_info = 1;

	const char *fn = jni_sz_js(jfilepath);
	ti->in_filename = fn;
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.detector");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");

	fx->track->cmd(t, FMED_TRACK_START);

	ffvec info = {};
	uint64 msec = info_add(&info, ti);

	char *format = ffsz_allocfmt("%ukbps %s %uHz %s %s"
		, (ti->audio.bitrate + 500) / 1000
		, ti->audio.decoder
		, ti->audio.fmt.sample_rate
		, channel_str(ti->audio.fmt.channels)
		, ffpcm_fmtstr(ti->audio.fmt.format));
	jni_obj_sz_set(env, thiz, jni_field(jc, "info", JNI_TSTR), format);
	*ffvec_pushT(&info, char*) = ffsz_dup("format");
	*ffvec_pushT(&info, char*) = format;

	// info += ti->meta
	char **it;
	FFSLICE_WALK(&ti->meta, it) {
		*ffvec_pushT(&info, char*) = *it;
		*it = NULL;
	}

	// * disable picture tag conversion attempt to UTF-8
	// * store artist & title
	const char *artist = "", *title = "";
	it = info.ptr;
	for (uint i = 0;  i != info.len;  i += 2) {
		if (ffsz_eq(it[i], "picture")) {
			char *val = it[i+1];
			val[0] = '\0';

		} else if (ffsz_eq(it[i], "artist")) {
			if (artist[0] == '\0')
				artist = it[i+1];

		} else if (ffsz_eq(it[i], "title")) {
			if (title[0] == '\0')
				title = it[i+1];
		}
	}

	jfieldID jf;

	// this.length_msec = ...
	jf = jni_field(jc, "length_msec", JNI_TLONG);
	jni_obj_long_set(thiz, jf, msec);

	// this.artist = ...
	jf = jni_field(jc, "artist", JNI_TSTR);
	jni_obj_sz_set(env, thiz, jf, artist);

	// this.title = ...
	jf = jni_field(jc, "title", JNI_TSTR);
	jni_obj_sz_set(env, thiz, jf, title);

	fx->track->cmd(t, FMED_TRACK_STOP);
	jni_sz_free(fn, jfilepath);

	fftime t2 = fftime_monotonic();
	fftime_sub(&t2, &t1);
	*ffvec_pushT(&info, char*) = ffsz_dup("[meta prepare time]");
	*ffvec_pushT(&info, char*) = ffsz_allocfmt("%,U msec", fftime_to_msec(&t2));

	jobjectArray jas = jni_jsa_sza(env, info.ptr, info.len);

	FFSLICE_WALK(&info, it) {
		ffmem_free(*it);
	}
	ffvec_free(&info);

	dbglog0("%s: exit", __func__);
	return jas;
}

/** Parse seek/until audio position string: [[h:]m:]s[.ms] */
static int msec_apos(const char *apos, int64 *msec)
{
	ffdatetime dt;
	fftime t;
	ffstr s = FFSTR_INITZ(apos);
	if (s.len == 0)
		return 0;
	if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
		return -1;
	fftime_join1(&t, &dt);
	*msec = fftime_to_msec(&t);
	return 0;
}

/** Get error message from FMED_E value */
static const char* trk_errstr(uint e)
{
	if (e == 0)
		return NULL;

	if (e & FMED_E_SYS)
		return fferr_strptr(e & ~FMED_E_SYS);

	e--;
	static const char errstr[][30] = {
		"Input file doesn't exist", // FMED_E_NOSRC
		"Output file already exists", // FMED_E_DSTEXIST
		"Unknown input file format", // FMED_E_UNKIFMT
		"Incompatible data formats", // FMED_E_INCOMPATFMT
	};
	const char *s = "Unknown";
	if (e < FF_COUNT(errstr))
		s = errstr[e];
	return s;
}

#define F_DATE_PRESERVE  1
#define F_OVERWRITE  2

JNIEXPORT jint JNICALL
Java_com_github_stsaz_fmedia_Fmedia_convert(JNIEnv *env, jobject thiz, jstring jiname, jstring joname, jint flags)
{
	dbglog0("%s: enter", __func__);
	fftime t1 = fftime_monotonic();
	jclass jc = jni_class_obj(thiz);
	jstring jfrom = jni_obj_jo(thiz, jni_field(jc, "from_msec", JNI_TSTR));
	jstring jto = jni_obj_jo(thiz, jni_field(jc, "to_msec", JNI_TSTR));

	const char *ifn = jni_sz_js(jiname)
		, *ofn = jni_sz_js(joname)
		, *from = jni_sz_js(jfrom)
		, *to = jni_sz_js(jto);

	const char *error = NULL;
	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);
	ti->type = FMED_TRK_TYPE_CONVERT;
#ifdef FMED_DEBUG
	ti->print_time = 1;
#endif
	ti->stream_copy = jni_obj_bool(thiz, jni_field(jc, "copy", JNI_TBOOL));

	ti->in_filename = ifn;
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");

	if (0 != msec_apos(from, (int64*)&ti->audio.seek)) {
		error = "Please set correct 'from' value";
		goto end;
	}
	if ((int64)ti->audio.seek != FMED_NULL)
		ti->seek_req = 1;
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.detector");

	if (0 != msec_apos(to, &ti->audio.until)) {
		error = "Please set correct 'until' value";
		goto end;
	}
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "afilter.until");

	ti->out_preserve_date = !!(flags & F_DATE_PRESERVE);
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");

	ti->audio.convfmt.sample_rate = jni_obj_int(thiz, jni_field(jc, "sample_rate", JNI_TINT));
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "afilter.autoconv");

	// Add output format filter according to the file extension of user-specified output file
	ffstr ext;
	ffpath_split3(ofn, ffsz_len(ofn), NULL, NULL, &ext);
	if (ext.len == 0) {
		error = "Please set output file extension";
		goto end;
	}
	const char *fname = (void*)core->cmd(FMED_OFILTER_BYEXT, ext.ptr);
	if (fname == NULL) {
		error = "Output file extension isn't supported";
		goto end;
	}
	if (ffstr_eqz(&ext, "mp3") && !ti->stream_copy) {
		error = ".mp3 output requires Stream Copy";
		goto end;
	}
	if (!(ti->stream_copy && ffstr_eqz(&ext, "mp3"))) { // Note: fmt.mp3-copy is added directly by fmt.mp3
		ti->aac.quality = jni_obj_int(thiz, jni_field(jc, "aac_quality", JNI_TINT));
		fx->track->cmd(t, FMED_TRACK_FILT_ADD, fname);
	}

	ti->out_filename = ffsz_dup(ofn);
	ti->out_overwrite = !!(flags & F_OVERWRITE);
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.filew");

	fx->track->cmd(t, FMED_TRACK_START);

end:
	{
	int e = ti->error;
	fx->track->cmd(t, FMED_TRACK_STOP);

	jni_sz_free(ifn, jiname);
	jni_sz_free(ofn, joname);
	jni_sz_free(from, jfrom);
	jni_sz_free(to, jto);

	fftime t2 = fftime_monotonic();
	fftime_sub(&t2, &t1);

	if (error == NULL)
		error = trk_errstr(e);
	else
		e = -1;

	const char *status = (error == NULL) ? "SUCCESS!" : "ERROR: ";
	if (error == NULL)
		error = "";
	char *res = ffsz_allocfmt("%s%s [%,U msec]"
		, status, error, fftime_to_msec(&t2));
	jni_obj_sz_set(env, thiz, jni_field(jc, "result", JNI_TSTR), res);
	ffmem_free(res);
	dbglog0("%s: exit", __func__);
	return e;
	}
}

#define RECF_EXCLUSIVE  1
#define RECF_POWER_SAVE  2

static void* rectrk_open(fmed_track_info *ti)
{
	return ti;
}

static void rectrk_close(void *ctx)
{
	fmed_track_info *ti = ctx;
	JNIEnv *env;
	int r = jni_attach(fx->jvm, &env);
	if (r != 0) {
		errlog0("jni_attach: %d", r);
		goto end;
	}
	if (ti->flags & FMED_FFINISHED) {
		jobject jo = ti->finsig_data;
		jni_call_void(jo, fx->Fmedia_Callback_on_finish);
	}

end:
	jni_global_unref(ti->finsig_data);
	jni_detach(jvm);
}

static int rectrk_process(void *ctx, fmed_track_info *ti)
{
	return FMED_RDONE;
}

static const fmed_filter rectrk_mon = { rectrk_open, rectrk_process, rectrk_close };


enum {
	REC_AACLC = 0,
	REC_AACHE = 1,
	REC_AACHE2 = 2,
	REC_FLAC = 3,
};

JNIEXPORT jlong JNICALL
Java_com_github_stsaz_fmedia_Fmedia_recStart(JNIEnv *env, jobject thiz, jstring joname, jint buf_len_msec, jint gain_db100, jint fmt, jint q, jint until_sec, jint flags, jobject jcb)
{
	dbglog0("%s: enter", __func__);
	int e = -1;
	const char *oname = jni_sz_js(joname);

	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);
	ti->type = FMED_TRK_TYPE_REC;
#ifdef FMED_DEBUG
	ti->print_time = 1;
#endif

	fx->Fmedia_Callback_on_finish = jni_func(jni_class_obj(jcb), "on_finish", "()V");
	ti->finsig_data = jni_global_ref(jcb);
	fx->track->cmd(t, FMED_TRACK_FILT_ADDF, FMED_TRACK_FILT_ADD, "mon", &rectrk_mon);

	ti->ai_exclusive = !!(flags & RECF_EXCLUSIVE);
	ti->ai_power_save = !!(flags & RECF_POWER_SAVE);
	ti->a_in_buf_time = buf_len_msec;
	if (NULL == (void*)fx->track->cmd(t, FMED_TRACK_FILT_ADD, "aaudio.in")) {
		goto end;
	}

	if (until_sec != 0) {
		ti->audio.until = (uint)until_sec*1000;
		fx->track->cmd(t, FMED_TRACK_FILT_ADD, "afilter.until");
	}

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");

	if (gain_db100 != 0) {
		ti->audio.gain = gain_db100;
		fx->track->cmd(t, FMED_TRACK_FILT_ADD, "afilter.gain");
	}

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "afilter.autoconv");

	ffstr ext;
	ffpath_split3(oname, ffsz_len(oname), NULL, NULL, &ext);
	if (ext.len == 0) {
		errlog0("Please set output file extension");
		goto end;
	}
	const char *fname = (void*)core->cmd(FMED_OFILTER_BYEXT, ext.ptr);
	if (fname == NULL) {
		errlog0("Output file extension isn't supported");
		goto end;
	}

	if (q != 0)
		ti->aac.quality = (uint)q;

	uint enc = flags & 0x0f;
	switch (enc) {
	case REC_AACHE:
		ffstr_setz(&ti->aac.profile, "HE"); break;
	case REC_AACHE2:
		ffstr_setz(&ti->aac.profile, "HEv2"); break;
	}

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, fname);

	ti->out_filename = ffsz_dup(oname);
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.filew");

	fx->track->cmd(t, FMED_TRACK_XSTART);
	e = 0;

end:
	jni_sz_free(oname, joname);
	if (e != 0) {
		fx->track->cmd(t, FMED_TRACK_STOP);
		t = NULL;
	}
	dbglog0("%s: exit", __func__);
	return (jlong)t;
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_recStop(JNIEnv *env, jobject thiz, jlong trk)
{
	if (trk == 0) return;

	dbglog0("%s: enter", __func__);
	fmed_track_obj *t = (void*)trk;
	fx->track->cmd(t, FMED_TRACK_STOP);
	dbglog0("%s: exit", __func__);
}

static int qu_idx(jlong q)
{
	return q - 1;
}

JNIEXPORT jlong JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quNew(JNIEnv *env, jobject thiz)
{
	fx->qu->cmdv(FMED_QUE_NEW);
	uint n = fx->qu->cmdv(FMED_QUE_N_LISTS);
	if (n == 1)
		fx->qu->cmdv(FMED_QUE_SEL, 0);
	return n;
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quDestroy(JNIEnv *env, jobject thiz, jlong q)
{
	int qi = qu_idx(q);
	fx->qu->cmdv(FMED_QUE_DEL, qi);
}

#define QUADD_RECURSE  1

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quAdd(JNIEnv *env, jobject thiz, jlong q, jobjectArray jurls, jint flags)
{
	dbglog0("%s: enter", __func__);
	int qi = qu_idx(q);
	jstring js = NULL;
	const char *fn = NULL;
	ffsize n = jni_arr_len(jurls);
	for (uint i = 0;  i != n;  i++) {
		jni_sz_free(fn, js);
		js = jni_joa_i(jurls, i);
		fn = jni_sz_js(js);

		if (flags & QUADD_RECURSE) {
			if (0 == qu_add_dir_r(fn, qi))
				continue;
		}

		ffstr ext;
		ffpath_splitname(fn, ffsz_len(fn), NULL, &ext);
		if (ffstr_eqz(&ext, "m3u8")
			|| ffstr_eqz(&ext, "m3u")) {
			int qi_prev = fx->qu->cmdv(FMED_QUE_SEL, qi);
			qu_load_file(fn, qi);
			fx->qu->cmdv(FMED_QUE_SEL, qi_prev);
			continue;
		}

		fmed_que_entry e = {};
		ffstr_setz(&e.url, fn);
		fx->qu->cmdv(FMED_QUE_ADD2, qi, &e);
	}

	jni_sz_free(fn, js);
	dbglog0("%s: exit", __func__);
}

static int qu_add_dir_r(const char *fn, int qi)
{
	dbglog0("%s: enter", __func__);
	ffdirscan ds = {};
	fntree_block *root = NULL;
	fffd f = FFFILE_NULL;
	char *fpath = NULL;
	int rc = -1;

	if (0 != ffdirscan_open(&ds, fn, 0))
		goto end;

	ffstr path = FFSTR_INITZ(fn);
	if (NULL == (root = fntree_from_dirscan(path, &ds, 0)))
		goto end;
	ffdirscan_close(&ds);

	fntree_block *blk = root;
	fntree_cursor cur = {};
	for (;;) {
		fntree_entry *e;
		if (NULL == (e = fntree_cur_next_r_ctx(&cur, &blk)))
			break;

		ffstr path = fntree_path(blk);
		ffstr name = fntree_name(e);
		ffmem_free(fpath);
		fpath = ffsz_allocfmt("%S/%S", &path, &name);

		fffile_close(f);
		if (FFFILE_NULL == (f = fffile_open(fpath, FFFILE_READONLY)))
			continue;

		fffileinfo fi;
		if (0 != fffile_info(f, &fi))
			continue;

		if (fffile_isdir(fffileinfo_attr(&fi))) {

			ffmem_zero_obj(&ds);
			ds.fd = f;
			f = FFFILE_NULL;
			if (0 != ffdirscan_open(&ds, NULL, FFDIRSCAN_USEFD))
				continue;

			ffstr_setz(&path, fpath);
			if (NULL == (blk = fntree_from_dirscan(path, &ds, 0)))
				continue;
			ffdirscan_close(&ds);

			fntree_attach(e, blk);
			continue;
		}

		fmed_que_entry qe = {};
		ffstr_setz(&qe.url, fpath);
		fx->qu->cmdv(FMED_QUE_ADD2, qi, &qe);

		ffmem_free(fpath); fpath = NULL;
	}

	rc = 0;

end:
	ffmem_free(fpath);
	fffile_close(f);
	ffdirscan_close(&ds);
	fntree_free_all(root);

	dbglog0("%s: exit", __func__);
	return rc;
}

JNIEXPORT jstring JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quEntry(JNIEnv *env, jobject thiz, jlong q, jint i)
{
	int qi = qu_idx(q);
	fmed_que_entry *e = (void*)fx->qu->cmdv(FMED_QUE_ITEM, qi, (ffsize)i);
	const char *url = "";
	if (e != NULL)
		url = e->url.ptr;
	return jni_js_sz(url);
}

#define QUCOM_CLEAR  1
#define QUCOM_REMOVE_I  2
#define QUCOM_COUNT  3

JNIEXPORT jint JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quCmd(JNIEnv *env, jobject thiz, jlong q, jint cmd, jint i)
{
	int qi = qu_idx(q);
	switch (cmd) {
	case QUCOM_CLEAR:
		fx->qu->cmdv(FMED_QUE_SEL, qi);
		fx->qu->cmdv(FMED_QUE_CLEAR);
		break;

	case QUCOM_REMOVE_I: {
		fmed_que_entry *e = (void*)fx->qu->cmdv(FMED_QUE_ITEM, qi, (ffsize)i);
		fx->qu->cmdv(FMED_QUE_RM, e);
		break;
	}

	case QUCOM_COUNT:
		return fx->qu->cmdv(FMED_QUE_COUNT2, qi);
	}
	return 0;
}

enum {
	QUFILTER_URL = 1,
	QUFILTER_META = 2,
};

JNIEXPORT jint JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quFilter(JNIEnv *env, jobject thiz, jlong q, jstring jfilter, jint flags)
{
	uint nfilt = 0;
	dbglog0("%s: enter", __func__);
	const char *filterz = jni_sz_js(jfilter);
	ffstr filter = FFSTR_INITZ(filterz);

	if (filter.len == 0) {
		fx->list_filter = 0;
		fx->qu->cmdv(FMED_QUE_DEL_FILTERED);
		return -1;
	}

	if (!fx->list_filter && filter.len < 2)
		return -1; // too small filter text

	fx->qu->cmdv(FMED_QUE_NEW_FILTERED);

	fmed_que_entry *e = NULL;
	for (;;) {

		if (0 == fx->qu->cmdv(FMED_QUE_LIST_NOFILTER, &e, 0))
			break;
		uint inc = 0;

		if ((flags & QUFILTER_URL)
			&& -1 != ffstr_ifindstr(&e->url, &filter)) {
			inc = 1;

		} else if (flags & QUFILTER_META) {

			ffstr *meta, name;
			for (uint i = 0;  NULL != (meta = fx->qu->meta(e, i, &name, 0));  i++) {
				if (meta == FMED_QUE_SKIP)
					continue;
				if (-1 != ffstr_ifindstr(meta, &filter)) {
					inc = 1;
					break;
				}
			}
		}

		if (inc) {
			fx->qu->cmdv(FMED_QUE_ADD_FILTERED, e);
			nfilt++;
		}
	}

	fx->list_filter = 1;

	jni_sz_free(filterz, jfilter);
	dbglog0("%s: exit", __func__);
	return nfilt;
}

static void qu_load_file(const char *fn, int qi)
{
	fmed_track_obj *t = fx->track->create(FMED_TRK_TYPE_EXPAND, NULL);
	fmed_track_info *ti = fx->track->conf(t);

	ti->in_filename = fn;
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.m3u");

	fx->track->cmd(t, FMED_TRACK_START);
	fx->track->cmd(t, FMED_TRACK_STOP);
}

JNIEXPORT jint JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quLoad(JNIEnv *env, jobject thiz, jlong q, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	const char *fn = jni_sz_js(jfilepath);
	int qi = qu_idx(q);
	int qi_prev = fx->qu->cmdv(FMED_QUE_SEL, qi);
	qu_load_file(fn, qi);

	fx->qu->cmdv(FMED_QUE_SEL, qi_prev);

	jni_sz_free(fn, jfilepath);
	dbglog0("%s: exit", __func__);
	return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quSave(JNIEnv *env, jobject thiz, jlong q, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	const char *fn = jni_sz_js(jfilepath);
	int qi = qu_idx(q);
	fx->qu->cmdv(FMED_QUE_SAVE, qi, fn);
	jni_sz_free(fn, jfilepath);
	dbglog0("%s: exit", __func__);
	return 1;
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_quList(JNIEnv *env, jobject thiz, jlong q)
{
	int qi = qu_idx(q);
	fx->qu->cmdv(FMED_QUE_SEL, qi);
	uint n = fx->qu->cmdv(FMED_QUE_COUNT2, qi);
	jobjectArray jsa = jni_joa(n, jni_class(JNI_CSTR));
	fmed_que_entry *e = NULL;
	uint i = 0;
	while (fx->qu->cmdv(FMED_QUE_LIST, &e)) {
		jstring js = jni_js_sz(e->url.ptr);
		jni_joa_i_set(jsa, i, js);
		jni_local_unref(js);
		i++;
	}
	return jsa;
}

static int file_trash(const char *trash_dir, const char *fn)
{
	int rc = -1;
	ffstr name;
	ffpath_splitpath(fn, ffsz_len(fn), NULL, &name);
	char *trash_fn = ffsz_allocfmt("%s/%S", trash_dir, &name);

	uint flags = 1|2;
	for (;;) {

		if (fffile_exists(trash_fn)) {
			if (!(flags & 2))
				goto end;
			fftime now;
			fftime_now(&now);
			ffmem_free(trash_fn);
			trash_fn = ffsz_allocfmt("%s/%S-%xu", trash_dir, &name, (uint)now.sec);
			flags &= ~2;
			continue;
		}

		if (0 != fffile_rename(fn, trash_fn)) {
			syswarnlog0("move to trash: %s", trash_fn);
			int e = fferr_last();
			if (fferr_notexist(e) && (flags & 1)) {
				ffdir_make(trash_dir);
				flags &= ~1;
				continue;
			}
			goto end;
		}

		dbglog0("moved to trash: %s", fn);
		rc = 0;
		break;
	}

end:
	ffmem_free(trash_fn);
	return rc;
}

static char* trash_dir_abs(JNIEnv *env, jobjectArray jsa, const char *trash_dir_rel, const char *fn)
{
	char *trash_dir = NULL;
	// Select the storage root of the file to be moved
	jstring jstg = NULL;
	const char *stg = NULL;
	uint n = jni_arr_len(jsa);
	for (uint i = 0;  i != n;  i++) {
		jni_sz_free(stg, jstg);
		jni_local_unref(jstg);
		jstg = jni_joa_i(jsa, i);
		stg = jni_sz_js(jstg);
		if (ffsz_matchz(fn, stg)
			&& stg[0] != '\0' && fn[ffsz_len(stg)] == '/') {
			// e.g. "/storage/emulated/0/Music/file.mp3" starts with "/storage/emulated/0"
			trash_dir = ffsz_allocfmt("%s/%s", stg, trash_dir_rel);
			break;
		}
	}
	jni_sz_free(stg, jstg);
	jni_local_unref(jstg);
	return trash_dir;
}

JNIEXPORT jstring JNICALL
Java_com_github_stsaz_fmedia_Fmedia_trash(JNIEnv *env, jobject thiz, jstring jtrash_dir, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	jclass jc = jni_class_obj(thiz);
	const char *error = "";
	const char *trash_dir_rel = jni_sz_js(jtrash_dir);
	const char *fn = jni_sz_js(jfilepath);

	// Select the storage root of the file to be moved
	jobjectArray jsa = jni_obj_jo(thiz, jni_field(jc, "storage_paths", JNI_TARR JNI_TSTR));
	char *trash_dir = trash_dir_abs(env, jsa, trash_dir_rel, fn);

	if (trash_dir != NULL
		&& 0 != file_trash(trash_dir, fn))
		error = fferr_strptr(fferr_last());

	jni_sz_free(fn, jfilepath);
	jni_sz_free(trash_dir_rel, jtrash_dir);
	ffmem_free(trash_dir);

	jstring js = jni_js_sz(error);
	dbglog0("%s: exit", __func__);
	return js;
}

JNIEXPORT jstring JNICALL
Java_com_github_stsaz_fmedia_Fmedia_fileMove(JNIEnv *env, jobject thiz, jstring jfilepath, jstring jtarget_dir)
{
	dbglog0("%s: enter", __func__);
	const char *fn = jni_sz_js(jfilepath);
	const char *tgt_dir = jni_sz_js(jtarget_dir);
	const char *error = "";

	ffstr fns = FFSTR_INITZ(fn);
	ffstr name;
	ffpath_splitpath_str(fns, NULL, &name);
	char *newfn = ffsz_allocfmt("%s/%S", tgt_dir, &name);
	if (fffile_exists(newfn))
		error = "file already exists";
	else if (0 != fffile_rename(fn, newfn))
		error = fferr_strptr(fferr_last());

	ffmem_free(newfn);
	jni_sz_free(fn, jfilepath);
	jni_sz_free(tgt_dir, jtarget_dir);

	jstring js = jni_js_sz(error);
	dbglog0("%s: exit", __func__);
	return js;
}
