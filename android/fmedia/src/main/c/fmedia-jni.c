/** fmedia/Android
2022, Simon Zolin */

#include <fmedia.h>
const fmed_core *core;
#include "core.h"
#include "log.h"
#include "file-input.h"
#include "track.h"
#include "jni-helper.h"
#include <util/path.h>

typedef struct fmedia_ctx fmedia_ctx;
struct fmedia_ctx {
	int dummy;
};
static struct fmedia_ctx *fx;

JNIEXPORT void JNICALL
Java_com_github_stsaz_fmedia_Fmedia_init(JNIEnv *env, jobject thiz)
{
	fx = ffmem_new(fmedia_ctx);
	core_init();
	_core.loglev = FMED_LOG_INFO;
	// _core.loglev = FMED_LOG_DEBUG;
	_core.log = adrd_log;
	_core.logv = adrd_logv;
	// ffmap_free(fx);
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_meta(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	struct track_ctx *t = trk_create();

	const char *fn = jni_sz_str(jfilepath);
	ffstr ext;
	ffpath_split3(fn, ffsz_len(fn), NULL, NULL, &ext);
	const fmed_filter *f = core_filter(ext);
	if (f == NULL)
		goto end;

	t->ti.in_filename = fn;
	t->ti.input_info = 1;
	t->filters[0].iface = &file_input;
	t->filters[0].first = 1;
	t->filters[1].iface = f;

	trk_process(t);

end:
	{
	jobjectArray jas = jni_astr_asz(env, t->meta.ptr, t->meta.len);
	jni_sz_free(fn, jfilepath);
	trk_free(t);
	return jas;
	}
}

JNIEXPORT jobjectArray JNICALL
Java_com_github_stsaz_fmedia_Fmedia_playlistLoad(JNIEnv *env, jobject thiz, jstring jfilepath)
{
	ffvec buf = {};
	const char *fn = jni_sz_str(jfilepath);
	int r = fffile_readwhole(fn, &buf, 16*1024*1024);
	jni_sz_free(fn, jfilepath);

	ffvec_addchar(&buf, '\0');
	buf.len--;

	ffstr d = *(ffstr*)&buf;
	ffsize i = 0;
	while (d.len != 0) {
		ffstr ln;
		ffstr_splitby(&d, '\n', &ln, &d);
		if (ln.len != 0)
			i++;
	}

	jobjectArray jas = jni_arr(i, jni_class(JNI_CSTR));
	i = 0;
	d = *(ffstr*)&buf;
	while (d.len != 0) {
		ffstr ln;
		ffstr_splitby(&d, '\n', &ln, &d);
		if (ln.len == 0)
			continue;

		ln.ptr[ln.len] = '\0';
		jstring js = jni_str_sz(ln.ptr);
		jni_arr_obj_set(jas, i, js);
		jni_local_unref(js);
		i++;
	}

	ffvec_free(&buf);
	return jas;
}

JNIEXPORT jboolean JNICALL
Java_com_github_stsaz_fmedia_Fmedia_playlistSave(JNIEnv *env, jobject thiz, jstring jfilepath, jobjectArray jlist)
{
	ffvec buf = {};
	ffsize n = jni_arr_len(jlist);
	for (ffsize i = 0;  i != n;  i++) {
		jstring js = jni_arr_obj(jlist, i);
		const char *sz = jni_sz_str(js);
		ffvec_addsz(&buf, sz);
		ffvec_addchar(&buf, '\n');
		jni_sz_free(sz, js);
		jni_local_unref(js);
	}

	const char *fn = jni_sz_str(jfilepath);
	char *tmp = ffsz_allocfmt("%s.tmp", fn);
	int r = fffile_writewhole(tmp, buf.ptr, buf.len, 0);
	if (r != 0)
		goto end;

	r = fffile_rename(tmp, fn);

end:
	jni_sz_free(fn, jfilepath);
	ffmem_free(tmp);
	ffvec_free(&buf);
	return (r == 0);
}
