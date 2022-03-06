/**
Copyright (c) 2015 Simon Zolin
*/

#include "wohandler.h"
#include "string.h"
#include <FFOS/mem.h>
#include <FFOS/error.h>


enum {
	THDCMD_RUN
	, THDCMD_QUIT
	, THDCMD_WAIT
};

static int FFTHDCALL _ffwoh_evt_handler(void *param);


ffwoh* ffwoh_create(void)
{
	ffwoh *oh = ffmem_tcalloc1(ffwoh);
	if (oh == NULL)
		return NULL;

	oh->wake_evt = CreateEvent(NULL, 0, 0, NULL);
	oh->wait_evt = CreateEvent(NULL, 0, 0, NULL);
	if (oh->wake_evt == NULL || oh->wait_evt == NULL)
		goto fail;

	oh->hdls[oh->count++] = oh->wake_evt;

	oh->thd = ffthd_create(&_ffwoh_evt_handler, oh, 0);
	if (oh->thd == NULL)
		goto fail;

	fflk_setup();
	fflk_init(&oh->lk);
	return oh;

fail:
	ffwoh_free(oh);
	return NULL;
}

void ffwoh_free(ffwoh *oh)
{
	if (oh->wake_evt != NULL) {
		ffatom_swap(&oh->cmd, THDCMD_QUIT);
		SetEvent(oh->wake_evt);
	}

	if (oh->thd != NULL)
		ffthd_join(oh->thd, (uint)-1, NULL);

	if (oh->wake_evt != NULL)
		CloseHandle(oh->wake_evt);
	if (oh->wait_evt != NULL)
		CloseHandle(oh->wait_evt);

	ffmem_free(oh);
}

int ffwoh_add(ffwoh *oh, HANDLE h, ffwoh_handler_t handler, void *udata)
{
	fflk_lock(&oh->lk);
	if (oh->count == MAXIMUM_WAIT_OBJECTS || oh->count == (uint)-1) {
		if (oh->count == MAXIMUM_WAIT_OBJECTS)
			fferr_set(EOVERFLOW);
		else
			fferr_set(oh->thderr);
		fflk_unlock(&oh->lk);
		return 1;
	}
	oh->items[oh->count].handler = handler;
	oh->items[oh->count].udata = udata;
	oh->hdls[oh->count++] = h;
	fflk_unlock(&oh->lk);
	SetEvent(oh->wake_evt);
	return 0;
}

void ffwoh_rm(ffwoh *oh, HANDLE h)
{
	uint i;
	ffbool is_wrker;

	for (i = 1 /*skip wake_evt*/;  i < oh->count;  i++) {
		if (oh->hdls[i] != h)
			continue;

		is_wrker = (ffthd_curid() == oh->tid);

		fflk_lock(&oh->lk);
		if (!is_wrker) {
			ffatom_swap(&oh->cmd, THDCMD_WAIT);
			SetEvent(oh->wake_evt);

			//wait until the worker thread returns from the kernel
			WaitForSingleObject(oh->wait_evt, INFINITE);
			//now the worker thread waits until oh->lk is unlocked
		}

		oh->count--;
		if (i != oh->count) {
			//move the last element into a hole
			ffmemcpy(oh->items + i, oh->items + oh->count, sizeof(struct ffwoh_item));
			ffmemcpy(oh->hdls + i, oh->hdls + oh->count, sizeof(oh->hdls[0]));
		}
		fflk_unlock(&oh->lk);
		break;
	}
}

static int FFTHDCALL _ffwoh_evt_handler(void *param)
{
	ffwoh *oh = param;
	FF_WRITEONCE(oh->tid, ffthd_curid());

	for (;;) {
		uint count = oh->count; //oh->count may be incremented
		DWORD i = WaitForMultipleObjects(count, oh->hdls, 0, INFINITE);

		if (i >= WAIT_OBJECT_0 + count) {
			fflk_lock(&oh->lk);
			oh->count = (uint)-1;
			oh->thderr = (i == WAIT_FAILED) ? fferr_last() : 0;
			fflk_unlock(&oh->lk);
			return 1;
		}

		if (i == 0) {
			//wake_evt has signaled
			ssize_t cmd = ffatom_swap(&oh->cmd, THDCMD_RUN);
			if (cmd == THDCMD_QUIT)
				break;

			else if (cmd == THDCMD_WAIT) {
				SetEvent(oh->wait_evt);
				fflk_lock(&oh->lk); //wait until the arrays are modified
				fflk_unlock(&oh->lk);
			}

			continue;
		}

		oh->items[i].handler(oh->items[i].udata);
	}

	return 0;
}
