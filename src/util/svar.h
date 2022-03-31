#pragma once

enum FFSVAR {
	FFSVAR_TEXT,
	FFSVAR_S,
};

/** Process input string of the format "...text $var text...".
Return enum FFSVAR. */
static inline int svar_split(ffstr *in, ffstr *out)
{
	if (in->ptr[0] != '$') {
		ffssize pos = ffstr_findchar(in, '$');
		ffstr_set(out, in->ptr, pos);
		ffstr_shift(in, pos);
		return FFSVAR_TEXT;
	}

	ffsize i;
	for (i = 1 /*skip $*/;  i != in->len;  i++) {
		if (!ffchar_isname(in->ptr[i]))
			break;
	}
	ffstr_set(out, in->ptr+1, i-1);
	ffstr_shift(in, i);
	return FFSVAR_S;
}
