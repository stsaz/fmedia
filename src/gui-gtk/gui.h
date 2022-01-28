/** GUI based on GTK+ v3.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <FF/gui-gtk/gtk.h>
#include <FFOS/thread.h>
#include <FFOS/semaphore.h>
#include <FFOS/signal.h>


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

enum FILE_DEL_METHOD {
	FDM_TRASH,
	FDM_RENAME,
};

struct gtrk;
struct gui_conf {
	float auto_attenuate_ceiling;
	uint seek_step_delta,
		seek_leap_delta;
	byte autosave_playlists;
	uint file_delete_method; // enum FILE_DEL_METHOD
	byte list_random;
	byte list_repeat;
	ushort list_col_width[16];
	uint list_col_width_idx;
	uint list_actv_trk_idx;
	uint list_scroll_pos;
	char *ydl_format;
	char *ydl_outdir;
	char *editor_path;
};

struct gui_wabout;
struct gui_wcmd;
struct gui_wconvert;
struct gui_wdload;
struct gui_winfo;
struct gui_wlog;
struct gui_wmain;
struct gui_wplayprops;
struct gui_wrename;
struct gui_wuri;
extern const ffui_ldr_ctl wabout_ctls[];
extern const ffui_ldr_ctl wcmd_ctls[];
extern const ffui_ldr_ctl wconvert_ctls[];
extern const ffui_ldr_ctl wdload_ctls[];
extern const ffui_ldr_ctl winfo_ctls[];
extern const ffui_ldr_ctl wlog_ctls[];
extern const ffui_ldr_ctl wmain_ctls[];
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
	int is_kde;

	ffkqsig kqsig;
	ffkevent sigtask;

	struct gui_conf conf;

	struct gui_wabout *wabout;
	struct gui_wcmd *wcmd;
	struct gui_wconvert *wconvert;
	struct gui_wdload *wdload;
	struct gui_winfo *winfo;
	struct gui_wlog *wlog;
	struct gui_wmain *wmain;
	struct gui_wplayprops *wplayprops;
	struct gui_wrename *wrename;
	struct gui_wuri *wuri;
	ffui_dialog dlg;
	ffui_menu mfile;
	ffui_menu mlist;
	ffui_menu mplay;
	ffui_menu mconvert;
	ffui_menu mhelp;
	ffui_menu mexplorer;
	ffui_menu mpopup;
} ggui;

extern const fmed_core *core;
extern ggui *gg;

enum ACTION {
#include "actions.h"
};

// GUI-core:
void corecmd_add(uint cmd, void *udata);
void corecmd_addfunc(fftask_handler func, void *udata);
void ctlconf_write(void);
void usrconf_write(void);
void gui_showtextfile(uint id);
void gui_list_sel(uint idx);
struct params_urls_add_play {
	ffvec v;
	int play;
};

// Main:
void wmain_init();
void wmain_destroy();
void wmain_show();
void wmain_cmd(int id);
void wmain_newtrack(fmed_que_entry *ent, uint time_total, fmed_filt *d);
void wmain_fintrack();
void wmain_update(uint playtime, uint time_total);
void wmain_update_convert(fmed_que_entry *plid, uint playtime, uint time_total);
void wmain_ent_added(uint idx);
void wmain_ent_removed(uint idx);
void wmain_status(const char *fmt, ...);
enum {
	TAB_CONVERT = 1,
};
ffuint wmain_tab_new(ffuint flags);
int wmain_tab_active();
void wmain_list_clear();
void wmain_list_cols_width_write(ffconfw *conf);
void wmain_list_update(uint idx, int delta);
void wmain_list_set(uint idx, int delta);
void wmain_list_select(ffuint idx);
ffarr4* wmain_list_getsel();
ffarr4* wmain_list_getsel_send();
int wmain_list_scroll_vert();
int wmain_exp_conf(fmed_conf *fc, void *obj, fmed_conf_ctx *ctx);
int wmain_exp_conf_writeval(ffstr *line, ffconfw *conf);

// Dialogs:

int conf_convert(fmed_conf *fc, void *obj, fmed_conf_ctx *ctx);
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
void wdload_subps_onsig(struct ffsig_info *info, int exit_code);

void wrename_init();
void wrename_show(uint show);

extern const char* const repeat_str[3];
