/**
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>


extern fmed_core *core;
extern const fmed_track _fmed_track;


extern void core_job_enter(uint id, size_t *ctx);

extern ffbool core_job_shouldyield(uint id, size_t *ctx);

extern ffbool core_ismainthr(void);
