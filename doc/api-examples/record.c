/** fmedia: Record to .wav using core.so
2022, Simon Zolin

* Works on all supported platforms.
* Uses default audio device and default audio format from fmedia.conf.
* Starts recording to "rec.wav" file in the current working directory.
* Prints logs from fmedia.
* Stops recording on Ctrl+C.
* Terminates with assert() on error.

Note:
The executable file MUST be inside `fmedia-1/` directory
 for core.so to correctly find other modules in its `mod/` directory.
This limitation should be removed in the future.
*/

#include <fmedia.h>
#include <FFOS/path.h>
#include <FFOS/signal.h>
#include <FFOS/ffos-extern.h>
#include <assert.h>

fmed_core *core;
const fmed_track *track;
fftask tsk_stop;
const char *out_file_path = "rec.wav";

// Called every time fmedia has something to print to log.
// The function is called from a core worker thread.
void std_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	ffuint cap = FF_COUNT(buf) - FFS_LEN("\n");
	ffstr s = FFSTR_INITN(buf, 0);

	ffstr_addfmt(&s, cap, "%s [%s] %s: ", ld->stime, ld->level, ld->module);
	ffstr_addfmtv(&s, cap, ld->fmt, ld->va);
	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": %E", fferr_last());
	s.ptr[s.len++] = '\n';

	ffstd_write(ffstdout, s.ptr, s.len);
}
const fmed_log std_logger = { std_log };

// Here we can create tracks for fmedia to execute.
// The function is called from a core worker thread.
void start_jobs(void *udata)
{
	// Get interface for operating with audio tracks
	track = core->getmod("#core.track");

	// Create a new recording track
	// We don't need to free or unref our 'trk' pointer
	fmed_track_obj *trk;
	assert(NULL != (trk = track->create(FMED_TRK_TYPE_REC, NULL)));

	//Set output file path
	fmed_track_info *ti = track->conf(trk);
	ti->out_filename = ffsz_dup(out_file_path);

	// Issue the command to start recording
	assert(0 == track->cmd(trk, FMED_TRACK_START));

	fflog("Recording to %s...", out_file_path);
	// The recording will start after the function exits
}

// The function is called from a core worker thread.
void stop_tracks(void *param)
{
	// FMED_TRACK_STOPALL_EXIT command correctly stops all active tracks
	//  (giving them enough time to finalize their work),
	//  then sends FMED_STOP to fmedia core.
	// After some time our "core->sig(FMED_START)" will return.
	fflog("Stopping...");
	track->cmd((void*)-1, FMED_TRACK_STOPALL_EXIT);
}

void ctrlc_handler(struct ffsig_info *info)
{
	switch (info->sig) {
	case FFSIG_INT:
		fftask_set(&tsk_stop, stop_tracks, NULL);
		core->task(&tsk_stop, FMED_TASK_POST);
		break;
	}
}

int main(int argc, char **argv)
{
	// Prepare paths to core.so and fmedia.conf
	char buf[4096];
	const char *exe;
	assert(NULL != (exe = ffps_filename(buf, sizeof(buf), argv[0])));
	ffstr path;
	ffpath_splitpath(exe, ffsz_len(exe), &path, NULL);
	char *core_so = ffsz_allocfmt("%S/core.%s", &path, FFDL_EXT);
	char *fmedia_conf = ffsz_allocfmt("%S/fmedia.conf", &path);

	// Open fmedia core library
	ffdl dl;
	assert(NULL != (dl = ffdl_open(core_so, 0)));

	// Import functions from core
	fmed_core* (*core_init)(char **argv, char **env);
	void (*core_free)(void);
	core_init = (void*)ffdl_addr(dl, "core_init");
	core_free = (void*)ffdl_addr(dl, "core_free");

	// Get core interface
	assert(NULL != (core = core_init(argv, environ)));

	// Set our logger
	core->cmd(FMED_SETLOG, &std_logger);

	// Load configuration file
	assert(0 == core->cmd(FMED_CONF, fmedia_conf));

	// Initialize core
	assert(0 == core->sig(FMED_OPEN));

	// Add the task which the core will execute
	fftask tsk = {};
	fftask_set(&tsk, start_jobs, NULL);
	core->task(&tsk, FMED_TASK_POST);

	// Properly handle Ctrl+C from user
	int signal = FFSIG_INT;
	assert(0 == ffsig_subscribe(ctrlc_handler, &signal, 1));

	// Begin processing queued operations
	// The function returns only after FMED_STOP command is received
	core->sig(FMED_START);

	// Free core's private data and close the library
	if (core != NULL) {
		core_free();
	}
	ffdl_close(dl);
	ffmem_free(core_so);
	ffmem_free(fmedia_conf);
	return 0;
}
