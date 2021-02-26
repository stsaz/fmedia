/** GUI based on GTK+ v3.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <FF/gui-gtk/gtk.h>
#include <FFOS/thread.h>
#include <FFOS/semaphore.h>


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
#define FMED_USERCONF  "fmedia-user.conf"
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
	ffui_tab tabs;
	ffui_view vlist;
	ffui_ctl stbar;
	ffui_trayicon tray_icon;

	fmed_que_entry *active_qent;
};

struct gtrk;
struct gui_conf {
	float auto_attenuate_ceiling;
	uint seek_step_delta,
		seek_leap_delta;
	byte autosave_playlists;
	byte list_random;
	byte list_repeat;
	ushort list_col_width[16];
	uint list_actv_trk_idx;
	uint list_scroll_pos;
	char *ydl_format;
	char *ydl_outdir;
};

struct gui_wabout;
struct gui_wcmd;
struct gui_wconvert;
struct gui_wdload;
struct gui_winfo;
struct gui_wlog;
struct gui_wplayprops;
struct gui_wrename;
struct gui_wuri;
extern const ffui_ldr_ctl wabout_ctls[];
extern const ffui_ldr_ctl wcmd_ctls[];
extern const ffui_ldr_ctl wconvert_ctls[];
extern const ffui_ldr_ctl wdload_ctls[];
extern const ffui_ldr_ctl winfo_ctls[];
extern const ffui_ldr_ctl wlog_ctls[];
extern const ffui_ldr_ctl wplayprops_ctls[];
extern const ffui_ldr_ctl wrename_ctls[];
extern const ffui_ldr_ctl wuri_ctls[];

typedef struct ggui {
	ffsem sem;
	uint load_err;
	const fmed_queue *qu;
	const fmed_track *track;
	ffthd th;
	struct gtrk *curtrk;
	int focused;
	uint vol; //0..MAXVOL
	uint go_pos;
	uint tabs_counter;
	char *home_dir;
	void *subps;

	struct gui_conf conf;

	struct gui_wabout *wabout;
	struct gui_wcmd *wcmd;
	struct gui_wconvert *wconvert;
	struct gui_wdload *wdload;
	struct gui_winfo *winfo;
	struct gui_wlog *wlog;
	struct gui_wmain wmain;
	struct gui_wplayprops *wplayprops;
	struct gui_wrename *wrename;
	struct gui_wuri *wuri;
	ffui_dialog dlg;
	ffui_menu mfile;
	ffui_menu mlist;
	ffui_menu mplay;
	ffui_menu mconvert;
	ffui_menu mhelp;
} ggui;

extern const fmed_core *core;
extern ggui *gg;

enum ACTION {
	A_NONE,

	A_LIST_ADDFILE,
	A_LIST_ADDURL,
	A_DLOAD_SHOW,
	A_FILE_SHOWPCM,
	A_FILE_SHOWINFO,
	A_FILE_SHOWDIR,
	A_SHOW_RENAME,
	A_FILE_DELFILE,
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
	A_PLAY_REPEAT,
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
	A_SHOW_PROPS,

	A_LIST_NEW,
	A_LIST_DEL,
	A_LIST_SEL,
	A_LIST_READMETA,
	A_LIST_SAVE,
	A_LIST_SELECTALL,
	A_LIST_REMOVE,
	A_LIST_RMDEAD,
	A_LIST_CLEAR,
	A_LIST_RANDOM,
	A_LIST_SORTRANDOM,

	A_SHOWCONVERT,
	A_CONV_SET_SEEK,
	A_CONV_SET_UNTIL,

	A_ABOUT,
	A_CMD_SHOW,
	A_CONF_EDIT,
	A_USRCONF_EDIT,
	A_FMEDGUI_EDIT,
	A_README_SHOW,
	A_CHANGES_SHOW,

	A_CONVERT,
	A_CONVOUTBROWSE,
	A_CONV_EDIT,

	A_URL_ADD,

	A_DLOAD_SHOWFMT,
	A_DLOAD_DL,

	A_PLAYPROPS_EDIT,

	A_CMD_EXEC,
	A_CMD_FILTER,

	A_RENAME,

// private:
	A_ONCLOSE,
	A_ONDROPFILE,
	LOADLISTS,
	LIST_DISPINFO,
	_A_PLAY_REPEAT,
	_A_LIST_RANDOM,
	A_CONV_DISP,
	A_PLAYPROPS_DISP,
	A_CMD_DISP,
};

// GUI-core:
void corecmd_add(uint cmd, void *udata);
void corecmd_addfunc(fftask_handler func, void *udata);
void ctlconf_write(void);
void usrconf_write(void);
void gui_showtextfile(uint id);
void gui_list_sel(uint idx);

// Main:
void wmain_init();
void wmain_cmd(int id);
void wmain_newtrack(fmed_que_entry *ent, uint time_total, fmed_filt *d);
void wmain_fintrack();
void wmain_update(uint playtime, uint time_total);
void wmain_ent_added(uint idx);
void wmain_ent_removed(uint idx);
void wmain_status(const char *fmt, ...);
enum {
	TAB_CONVERT = 1,
};
ffuint wmain_tab_new(ffuint flags);
void wmain_list_clear();
void wmain_list_cols_width_write(ffconfw *conf);
void wmain_list_update(uint idx, int delta);
void wmain_list_set(uint idx, int delta);
void wmain_list_select(ffuint idx);

// Dialogs:

int conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
void wconvert_init();
void wconv_destroy();
void wconv_show(uint show);
void wconv_setdata(int id, uint pos);
int wconvert_conf_writeval(ffstr *line, ffconfw *conf);

void wlog_init();
void wlog_run();
void wlog_show(uint show);
void wlog_destroy();

void wabout_init();
void wabout_show(uint show);

void wuri_init();
void wuri_show(uint show);

void winfo_init();
void winfo_show(uint show, uint idx);

void wcmd_init();
void wcmd_show(uint show);

void wplayprops_init();
void wplayprops_show(uint show);

void wdload_init();
void wdload_show(uint show);
void wdload_destroy();
int wdload_conf_writeval(ffstr *line, ffconfw *conf);

void wrename_init();
void wrename_show(uint show);

extern const char* const repeat_str[3];
