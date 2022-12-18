/**
Target architecture, OS;  base types;  base macros.
Copyright (c) 2013 Simon Zolin
*/

#ifdef _WIN32
	#define UNICODE
	#define _UNICODE
#endif

#ifndef _FFOS_BASE_H
#include <FFOS/base.h>
#endif

#ifndef FF_VER
#define FF_VER

#if defined FF_UNIX
	#include <stdlib.h>
	#include <unistd.h>
	#include <string.h>

	enum {
		FF_BADFD = -1
	};

	typedef unsigned char byte;
	typedef unsigned short ushort;
	typedef unsigned int uint;

#elif defined FF_WIN

#ifdef _MSC_VER
	#define FF_MSVC
#endif

#define FF_BADFD  INVALID_HANDLE_VALUE

//byte;
typedef unsigned short ushort;
typedef unsigned int uint;

#ifdef FF_MSVC
	#define FF_EXP  __declspec(dllexport)
	#define FF_IMP  __declspec(dllimport)

	#ifndef _SIZE_T_DEFINED
		typedef SIZE_T size_t;
		#define _SIZE_T_DEFINED
	#endif
	typedef SSIZE_T ssize_t;

	#define FF_FUNC __FUNCTION__

	#define va_copy(vadst, vasrc)  vadst = vasrc

	#define ffint_bswap16  _byteswap_ushort
	#define ffint_bswap32  _byteswap_ulong
	#define ffint_bswap64  _byteswap_uint64

#endif

#endif

#ifdef __cplusplus
	#define FF_EXTN extern "C"
#else
	#define FF_EXTN extern
#endif


#define FF_SAFECLOSE(obj, def, func)\
do { \
	if (obj != def) { \
		func(obj); \
		obj = def; \
	} \
} while (0)


enum FFDBG_T {
	FFDBG_MEM = 0x10,
	FFDBG_KEV = 0x20,
	FFDBG_TIMER = 0x40,
	FFDBG_PARSE = 0x100,
};

/** Print FF debug messages.
@t: enum FFDBG_T + level. */
extern int ffdbg_print(int t, const char *fmt, ...);

#define FFDBG_PRINT(...)
#define FFDBG_PRINTLN(...)

#include "error-compat.h"
#include "file-compat.h"
#include "mem-compat.h"
#include "number-compat.h"
#include <FFOS/process.h>
#include "process-compat.h"
#include <FFOS/socket.h>
#include "socket-compat.h"
#include <FFOS/queue.h>
#include "queue-compat.h"
#include "thread-compat.h"

#define ffrnd_seed  ffrand_seed
#define ffrnd_get  ffrand_get

#ifdef FF_UNIX
static inline void fftime_fromtimespec(fftime *t, const struct timespec *ts)
{
	*t = fftime_from_timespec(ts);
}
#endif
#define fftime_mcs  fftime_to_usec
#define fftime_ms  fftime_to_msec

#define ffwreg  ffwinreg
#define ffwreg_val  ffwinreg_val
#define ffwreg_info  ffwinreg_info
#define FFWREG_BADKEY  FFWINREG_NULL
#define ffwreg_open  ffwinreg_open
#define ffwreg_close  ffwinreg_close
#define ffwreg_isstr  ffwinreg_isstr
#define ffwreg_write  ffwinreg_write
#define ffwreg_writestr  ffwinreg_writestr
#define ffwreg_writeint  ffwinreg_writeint
#define ffwreg_del  ffwinreg_del
#define ffwreg_read  ffwinreg_read

#endif
