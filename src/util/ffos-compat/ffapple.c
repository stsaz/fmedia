/**
Copyright (c) 2017 Simon Zolin
*/

#include "types.h"
#include <FFOS/dir.h>
#include <FFOS/socket.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>
#include "asyncio.h"

#include <sys/wait.h>
#include <mach-o/dyld.h>


ssize_t ffaio_fwrite(ffaio_filetask *ft, const void *data, size_t len, uint64 off, ffaio_handler handler)
{
	(void)handler;
	return fffile_pwrite(ft->kev.fd, data, len, off);
}

ssize_t ffaio_fread(ffaio_filetask *ft, void *data, size_t len, uint64 off, ffaio_handler handler)
{
	(void)handler;
	return fffile_pread(ft->kev.fd, data, len, off);
}
