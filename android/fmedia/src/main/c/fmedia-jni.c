/** fmedia/Android
2022, Simon Zolin */

#include <fmedia.h>

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, NULL, __VA_ARGS__)

#include "log.h"
#include "ctl.h"
#include "jni-helper.h"
#include <util/fntree.h>
#include <FFOS/perf.h>

typedef struct fmedia_ctx fmedia_ctx;
struct fmedia_ctx {
	const fmed_track *track;
};
static struct fmedia_ctx *fx;

extern const fmed_core *core;
fmed_core* core_init();
extern void core_destroy();

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_init(JNIEnv *env, jobject thiz)
{
	FF_ASSERT(fx == NULL);
	fx = ffmem_new(fmedia_ctx);
	fmed_core *core = core_init();
	core->loglev = FMED_LOG_INFO;
#ifdef FF_DEBUG
	core->loglev = FMED_LOG_DEBUG;
#endif
	core->log = adrd_log;
	core->logv = adrd_logv;
	fx->track = core->getmod("core.track");
}

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_destroy(JNIEnv *env, jobject thiz)
{
	core_destroy();
	ffmem_free(fx);
	fx = NULL;
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

	*ffvec_pushT(info, char*) = ffsz_dup("format");
	*ffvec_pushT(info, char*) = ffsz_allocfmt("%ukbps %s %uHz %s"
		, (ti->audio.bitrate + 500) / 1000
		, ti->audio.decoder
		, ti->audio.fmt.sample_rate
		, channel_str(ti->audio.fmt.channels));

	return msec;
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_meta(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	fftime t1 = fftime_monotonic();
	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);

	const char *fn = jni_sz_js(jfilepath);
	ti->in_filename = fn;
	ti->input_info = 1;

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.detector");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");

	fx->track->cmd(t, FMED_TRACK_START);

	ffvec info = {};
	uint64 msec = info_add(&info, ti);

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

	jclass jc = jni_class_obj(thiz);
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
	e--;

	static const char errstr[][30] = {
		"Input file doesn't exist", // FMED_E_NOSRC
		"Output file already exists", // FMED_E_DSTEXIST
		"Unknown input file format", // FMED_E_UNKIFMT
	};
	const char *s = "Unknown";
	if (e < FF_COUNT(errstr))
		s = errstr[e];
	return s;
}

JNIEXPORT jstring JNICALL
Java_com_github_stsaz_fmedia_Fmedia_streamCopy(JNIEnv *env, jobject thiz, jstring jiname, jstring joname, jstring jfrom, jstring jto, jint flags)
{
	dbglog0("%s: enter", __func__);
	fftime t1 = fftime_monotonic();

	const char *ifn = jni_sz_js(jiname)
		, *ofn = jni_sz_js(joname)
		, *from = jni_sz_js(jfrom)
		, *to = jni_sz_js(jto);

	const char *error = NULL;
	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);
	ti->type = FMED_TRK_TYPE_CONVERT;
	ti->stream_copy = 1;

	ti->in_filename = ifn;

	ti->out_filename = ffsz_dup(ofn);
	ti->out_preserve_date = !!(flags & 1);
	ti->out_overwrite = !!(flags & 2);

	if (0 != msec_apos(from, (int64*)&ti->audio.seek)) {
		error = "Bad 'from' value";
		goto end;
	}
	ti->seek_req = 1;

	if (0 != msec_apos(to, &ti->audio.until)) {
		error = "Bad 'until' value";
		goto end;
	}

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.detector");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");
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
	const char *status = (error == NULL) ? "SUCCESS!" : "ERROR: ";
	if (error == NULL)
		error = "";
	char *res = ffsz_allocfmt("%s%s [%,U msec]"
		, status, error, fftime_to_msec(&t2));
	jstring js = jni_js_sz(res);
	ffmem_free(res);
	dbglog0("%s: exit", __func__);
	return js;
	}
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_listDirRecursive(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	jobjectArray jas;
	ffdirscan ds = {};
	fntree_block *root = NULL;
	ffvec v = {};
	fffd f = FFFILE_NULL;
	char *fpath = NULL;
	const char *fn = jni_sz_js(jfilepath);

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

		*ffvec_pushT(&v, char*) = fpath;
		fpath = NULL;
	}

end:
	ffmem_free(fpath);
	fffile_close(f);
	ffdirscan_close(&ds);
	fntree_free_all(root);
	jni_sz_free(fn, jfilepath);

	jas = jni_jsa_sza(env, v.ptr, v.len);

	char **ps;
	FFSLICE_WALK(&v, ps) {
		ffmem_free(*ps);
	}
	ffvec_free(&v);

	dbglog0("%s: exit", __func__);
	return jas;
}

static jobjectArray pl_load(JNIEnv *env, ffvec *buf)
{
	ffvec_addchar(buf, '\0');
	buf->len--;

	ffstr d = *(ffstr*)buf;
	ffsize i = 0;
	while (d.len != 0) {
		ffstr ln;
		ffstr_splitby(&d, '\n', &ln, &d);
		if (ln.len != 0)
			i++;
	}

	jobjectArray jas = jni_joa(i, jni_class(JNI_CSTR));
	i = 0;
	d = *(ffstr*)buf;
	while (d.len != 0) {
		ffstr ln;
		ffstr_splitby(&d, '\n', &ln, &d);
		if (ln.len == 0)
			continue;

		ln.ptr[ln.len] = '\0';
		jstring js = jni_js_sz(ln.ptr);
		jni_joa_i_set(jas, i, js);
		jni_local_unref(js);
		i++;
	}

	return jas;
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_playlistLoadData(JNIEnv *env, jobject thiz, jbyteArray jdata)
{
	dbglog0("%s: enter", __func__);
	ffvec buf = jni_vec_jba(env, jdata);
	jobjectArray jas = pl_load(env, &buf);
	ffvec_free(&buf);
	dbglog0("%s: exit", __func__);
	return jas;
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_playlistLoad(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	ffvec buf = {};
	const char *fn = jni_sz_js(jfilepath);
	fffile_readwhole(fn, &buf, 16*1024*1024);
	jni_sz_free(fn, jfilepath);
	jobjectArray jas = pl_load(env, &buf);
	ffvec_free(&buf);
	dbglog0("%s: exit", __func__);
	return jas;
}

JNIEXPORT jboolean JNICALL
Java_com_github_stsaz_fmedia_Fmedia_playlistSave(JNIEnv *env, jobject thiz, jstring jfilepath, jobjectArray jlist)
{
	dbglog0("%s: enter", __func__);
	ffvec buf = {};
	ffsize n = jni_arr_len(jlist);
	for (ffsize i = 0;  i != n;  i++) {
		jstring js = jni_joa_i(jlist, i);
		const char *sz = jni_sz_js(js);
		ffvec_addsz(&buf, sz);
		ffvec_addchar(&buf, '\n');
		jni_sz_free(sz, js);
		jni_local_unref(js);
	}

	const char *fn = jni_sz_js(jfilepath);
	char *tmp = ffsz_allocfmt("%s.tmp", fn);
	int r = fffile_writewhole(tmp, buf.ptr, buf.len, 0);
	if (r != 0)
		goto end;

	r = fffile_rename(tmp, fn);

end:
	jni_sz_free(fn, jfilepath);
	ffmem_free(tmp);
	ffvec_free(&buf);
	dbglog0("%s: exit", __func__);
	return (r == 0);
}
