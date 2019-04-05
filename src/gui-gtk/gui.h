/** GUI based on GTK+ v3.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <FF/gui-gtk/gtk.h>
#include <FFOS/thread.h>


#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(...)  fmed_dbglog(core, NULL, "gui", __VA_ARGS__)
#define errlog(...)  fmed_errlog(core, NULL, "gui", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "gui", __VA_ARGS__)


struct gui_wmain {
	ffui_wnd wmain;
	ffui_menu mm;
	ffui_label lpos;
	ffui_trkbar tpos;
	ffui_view vlist;
};

struct gui_wabout {
	ffui_wnd wabout;
	ffui_label labout, lurl;
};

struct gtrk;
typedef struct ggui {
	uint state;
	uint load_err;
	const fmed_queue *qu;
	const fmed_track *track;
	ffthd th;
	struct gtrk *curtrk;
	int focused;

	struct {
		uint seek_step_delta,
			seek_leap_delta;
	} conf;

	struct gui_wmain wmain;
	struct gui_wabout wabout;
	ffui_menu mfile;
	ffui_menu mlist;
	ffui_menu mplay;
	ffui_menu mhelp;
} ggui;

extern const fmed_core *core;
extern ggui *gg;

enum ACTION {
	A_NONE,

	A_QUIT,

	A_PLAY,
	A_SEEK,
	A_STOP,
	A_STOP_AFTER,
	A_NEXT,
	A_PREV,
	A_FFWD,
	A_RWND,
	A_LEAP_FWD,
	A_LEAP_BACK,

	A_SELECTALL,
	A_CLEAR,

	A_ABOUT,

	A_ONCLOSE,
	A_ONDROPFILE,
};

void corecmd_add(uint cmd, void *udata);

void wmain_init();
void wmain_newtrack(fmed_que_entry *ent, uint time_total);
void wmain_fintrack();
void wmain_update(uint playtime, uint time_total);
void wmain_ent_add(const ffstr *fn);
void wmain_ent_added(void *param);
void wmain_ent_removed(uint idx);

void wabout_init();
