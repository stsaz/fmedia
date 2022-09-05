/** ff: command-line arguments parser
2020, Simon Zolin
*/

/*
ffcmdarg_from_line ffcmdarg_from_linew
ffcmdarg_init
ffcmdarg_fin
ffcmdarg_parse
*/

/* Format:
val0 -s val1 --long2 val3 --long=val2
*/

#pragma once
#include <ffbase/string.h>
#include <ffbase/stringz.h>

/** Convert command line to a NULL-terminated array (argv).
Unquote if necessary:
 "a b c" -> a b c
 a"b"c -> abc
argc: [Output] N of arguments
Return argv -- free with ffmem_free() */
static inline char** ffcmdarg_from_line(const char *cmdline, int *argc)
{
	const char *s = cmdline;
	ffuint i, n = 0, q = 0, f = 0;
	for (i = 0;;  i++) {
		if (s[i] == '"') {
			q = !q;
			f = 1;

		} else if (s[i] == ' ' && !q) {
			if (f) {
				n++;
				f = 0;
			}

		} else if (s[i] == '\0') {
			if (f) {
				n++;
			}
			break;

		} else {
			f = 1;
		}
	}

	char **av = (char**)ffmem_alloc((n+1)*sizeof(void*) + i);
	if (av == NULL)
		return NULL;
	char *d = (char*)av + (n+1)*sizeof(void*);
	av[0] = d;
	n = 0, q = 0, f = 0;
	for (i = 0;;  i++) {
		if (s[i] == '"') {
			q = !q;
			f = 1;

		} else if (s[i] == ' ' && !q) {
			if (f) {
				*d++ = '\0';
				n++;
				av[n] = d;
				f = 0;
			}

		} else if (s[i] == '\0') {
			if (f) {
				*d = '\0';
				n++;
			}
			break;

		} else {
			*d++ = s[i];
			f = 1;
		}
	}

	av[n] = NULL;
	*argc = n;
	return av;
}

#ifdef FF_WIN
static inline char** ffcmdarg_from_linew(const wchar_t *cmdline, int *argc)
{
	char *s = ffsz_alloc_wtou(cmdline);
	char **r = ffcmdarg_from_line(s, argc);
	ffmem_free(s);
	return r;
}
#endif

typedef struct ffcmdarg {
	ffuint state;
	ffuint iarg;
	ffstr val;
	ffstr longval;

	const char **argv;
	ffuint argc;
} ffcmdarg;

enum FFCMDARG_R {
	FFCMDARG_DONE,
	FFCMDARG_RVAL, // any stand-alone value
	FFCMDARG_RKEYSHORT, // -s
	FFCMDARG_RKEYLONG, // --long
	FFCMDARG_RKEYVAL, // --long=VAL
};

/** Initialize reader, skipping the first argument (program name) */
static inline void ffcmdarg_init(ffcmdarg *p, const char **argv, ffuint argc)
{
	ffmem_zero_obj(p);
	p->argv = argv + 1;
	p->argc = argc - 1;
}

static inline int ffcmdarg_fin(ffcmdarg *p)
{
	return 0;
}

/** Get next argument
Return enum FFCMDARG_R */
static inline int ffcmdarg_parse(ffcmdarg *p, ffstr *dst)
{
	enum { I_KV, I_VAL, };
	ffstr s;
	switch (p->state) {
	case I_KV:
		if (p->iarg >= p->argc)
			return FFCMDARG_DONE;

		ffstr_setz(&s, p->argv[p->iarg]);

		if (s.ptr[0] == '-') {

			if (s.ptr[1] == '-') {
				ffssize pos = ffstr_splitby(&s, '=', &s, &p->longval);
				if (pos >= 0)
					p->state = I_VAL;
				else
					p->iarg++;
				p->val = s;
				*dst = p->val;
				return FFCMDARG_RKEYLONG;
			}

			p->iarg++;
			p->val = s;
			*dst = p->val;
			return FFCMDARG_RKEYSHORT;
		}

		p->iarg++;
		p->val = s;
		*dst = p->val;
		return FFCMDARG_RVAL;

	case I_VAL:
		p->state = I_KV;
		p->iarg++;
		p->val = p->longval;
		ffstr_null(&p->longval);
		*dst = p->val;
		return FFCMDARG_RKEYVAL;
	}

	return -1;
}
