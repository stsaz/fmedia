#include <FFOS/time.h>

#ifdef FF_WIN

static FFINL int ffclk_get(fftime *result) {
	LARGE_INTEGER val;
	QueryPerformanceCounter(&val);
	result->sec = val.QuadPart;
	return 0;
}

FF_EXTN void ffclk_totime(fftime *t);

static FFINL void ffclk_diff(const fftime *start, fftime *diff)
{
	diff->sec -= start->sec;
	ffclk_totime(diff);
}

#else

/** Get system-specific clock value (unrelated to UTC time). */
static FFINL int ffclk_get(fftime *result) {
	struct timespec ts;
	int r = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (r == 0)
		fftime_fromtimespec(result, &ts);
	else
		fftime_null(result);
	return r;
}

/** Convert the value returned by ffclk_get() to fftime. */
#define ffclk_totime(t)

/** Get clock difference and convert to fftime. */
static FFINL void ffclk_diff(const fftime *start, fftime *stop)
{
	stop->sec -= start->sec;
	stop->nsec -= start->nsec;
	if ((int)stop->nsec < 0) {
		stop->nsec += 1000000000;
		stop->sec--;
	}
}

#endif

#define ffclk_gettime(t) \
do { \
	ffclk_get(t); \
	ffclk_totime(t); \
} while(0)


#define fftmr  fftimer
#define FF_BADTMR  FFTIMER_NULL
#define fftmr_create  fftimer_create
#define fftmr_read  fftimer_consume
#define fftmr_start  fftimer_start
#define fftmr_stop  fftimer_stop
#define fftmr_close  fftimer_close
