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

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_meta(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	dbglog0("%s: enter", __func__);
	fmed_track_obj *t = fx->track->create(0, NULL);
	fmed_track_info *ti = fx->track->conf(t);

	const char *fn = jni_sz_js(jfilepath);
	ti->in_filename = fn;
	ti->input_info = 1;

	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "core.file");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "fmt.detector");
	fx->track->cmd(t, FMED_TRACK_FILT_ADD, "ctl");

	fx->track->cmd(t, FMED_TRACK_START);

	{
	jobjectArray jas = jni_jsa_sza(env, ti->meta.ptr, ti->meta.len);
	jni_sz_free(fn, jfilepath);
	fx->track->cmd(t, FMED_TRACK_STOP);
	dbglog0("%s: exit", __func__);
	return jas;
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
