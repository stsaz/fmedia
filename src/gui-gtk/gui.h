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


enum {
	MAXVOL = 125,
};

#define CTL_CONF_FN  "fmedia.gui.conf"
#define AUTOPLIST_FN  "list%u.m3u8"

struct gui_wmain {
	ffui_wnd wmain;
	ffui_menu mm;
	ffui_btn bpause
		, bstop
		, bprev
		, bnext;
	ffui_label lpos;
	ffui_trkbar tvol;
	ffui_trkbar tpos;
	ffui_view vlist;
	ffui_ctl stbar;
	ffui_trayicon tray_icon;
};

struct gui_wabout {
	ffui_wnd wabout;
	ffui_label labout, lurl;
};

struct gui_wuri {
	ffui_wnd wuri;
	ffui_edit turi;
	ffui_btn bok;
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
	uint vol; //0..MAXVOL
	uint go_pos;

	struct {
		uint seek_step_delta,
			seek_leap_delta;
		byte autosave_playlists;
	} conf;

	struct gui_wmain wmain;
	struct gui_wabout wabout;
	struct gui_wuri wuri;
	ffui_dialog dlg;
	ffui_menu mfile;
	ffui_menu mlist;
	ffui_menu mplay;
	ffui_menu mhelp;
} ggui;

extern const fmed_core *core;
extern ggui *gg;

enum ACTION {
	A_NONE,

	A_LIST_ADDFILE,
	A_LIST_ADDURL,
	A_SHOWPCM,
	A_DELFILE,
	A_SHOW,
	A_HIDE,
	A_QUIT,

	A_PLAY,
	A_PLAYPAUSE,
	A_SEEK,
	A_STOP,
	A_STOP_AFTER,
	A_NEXT,
	A_PREV,
	A_FFWD,
	A_RWND,
	A_LEAP_FWD,
	A_LEAP_BACK,
	A_SETGOPOS,
	A_GOPOS,
	A_VOL,
	A_VOLUP,
	A_VOLDOWN,
	A_VOLRESET,

	A_LIST_SAVE,
	A_LIST_SELECTALL,
	A_LIST_REMOVE,
	A_LIST_RMDEAD,
	A_LIST_CLEAR,
	A_LIST_RANDOM,

	A_ABOUT,

	A_URL_ADD,

	A_ONCLOSE,
	A_ONDROPFILE,
	LOADLISTS,
};

void corecmd_add(uint cmd, void *udata);
void ctlconf_write(void);

void wmain_init();
void wmain_newtrack(fmed_que_entry *ent, uint time_total);
void wmain_fintrack();
void wmain_update(uint playtime, uint time_total);
void wmain_ent_add(const ffstr *fn);
void wmain_ent_added(void *param);
void wmain_ent_removed(uint idx);
void wmain_status(const char *fmt, ...);
void list_rmitems();

void wabout_init();
void wuri_init();
