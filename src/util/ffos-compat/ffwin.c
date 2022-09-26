/**
Copyright (c) 2013 Simon Zolin
*/

#include "types.h"
#include <FFOS/dir.h>
#include <FFOS/time.h>
#include <FFOS/thread.h>
#include <FFOS/timer.h>
#include <FFOS/process.h>
#include "asyncio.h"
#include "atomic.h"
#include <FFOS/semaphore.h>
#include <FFOS/dylib.h>
#include <FFOS/pipe.h>
#include <FFOS/file.h>

#include <psapi.h>
#include <time.h>
#ifndef FF_MSVC
#include <stdio.h>
#endif
#include <winternl.h>
#include <ntstatus.h>


int _ffaio_events(ffaio_task *t, const ffkqu_entry *e)
{
	int ev = 0;

	if (e->lpOverlapped == &t->wovl)
		ev |= FFKQU_WRITE;
	else
		ev |= FFKQU_READ;

	return ev;
}

ssize_t ffaio_fwrite(ffaio_filetask *ft, const void *data, size_t len, uint64 off, ffaio_handler handler)
{
	BOOL b;

	if (ft->kev.pending) {
		ft->kev.pending = 0;
		return ffio_result(&ft->kev.ovl);
	}

	ffmem_tzero(&ft->kev.ovl);
	ft->kev.ovl.Offset = (uint)off;
	ft->kev.ovl.OffsetHigh = (uint)(off >> 32);
	b = WriteFile(ft->kev.fd, data, FF_TOINT(len), NULL, &ft->kev.ovl);

	if (0 != fferr_ioret(b))
		return -1;

	if (!ft->kev.faio_direct)
		return ffio_result(&ft->kev.ovl);

	ft->kev.pending = 1;
	ft->kev.handler = handler;
	fferr_set(EAGAIN);
	return -1;
}

ssize_t ffaio_fread(ffaio_filetask *ft, void *data, size_t len, uint64 off, ffaio_handler handler)
{
	BOOL b;

	if (ft->kev.pending) {
		ft->kev.pending = 0;
		int r = ffio_result(&ft->kev.ovl);
		if (r < 0 && fferr_last() == ERROR_HANDLE_EOF)
			return 0;
		return r;
	}

	ffmem_tzero(&ft->kev.ovl);
	ft->kev.ovl.Offset = (uint)off;
	ft->kev.ovl.OffsetHigh = (uint)(off >> 32);
	b = ReadFile(ft->kev.fd, data, FF_TOINT(len), NULL, &ft->kev.ovl);

	if (0 != fferr_ioret(b)) {
		if (fferr_last() == ERROR_HANDLE_EOF)
			return 0;
		return -1;
	}

	if (!ft->kev.faio_direct)
		return ffio_result(&ft->kev.ovl);

	ft->kev.pending = 1;
	ft->kev.handler = handler;
	fferr_set(EAGAIN);
	return -1;
}
