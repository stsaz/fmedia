/**
Copyright (c) 2016 Simon Zolin */

#include <FF/gui/winapi.h>
#include <FF/gui/loader.h>
#include <FF/data/conf.h>
#include <FFOS/thread.h>
#include <FFOS/semaphore.h>


typedef struct gui_wmain {
	ffui_wnd wmain;
	ffui_menu mm;
	ffui_btn bpause
		, bstop
		, bprev
		, bnext;
	ffui_label lpos;
	ffui_tab tabs;
	ffui_trkbar tpos
		, tvol;
	ffui_view vlist;
	ffui_paned pntop
		, pnpos
		, pntabs
		, pnlist;
	ffui_stbar stbar;
	ffui_trayicon tray_icon;
	ffui_icon ico;
	ffui_icon ico_rec;

	int actv_tab;
} gui_wmain;

typedef struct gui_trk gui_trk;

struct guiconf {
	byte list_repeat;
	float auto_attenuate_ceiling;
};

struct gui_wabout;
struct gui_wconvert;
struct gui_wdevlist;
struct gui_wfilter;
struct gui_wgoto;
struct gui_winfo;
struct gui_wlog;
struct gui_wplayprops;
struct gui_wrec;
struct gui_wuri;
extern const ffui_ldr_ctl wabout_ctls[];
extern const ffui_ldr_ctl wconvert_ctls[];
extern const ffui_ldr_ctl wdevlist_ctls[];
extern const ffui_ldr_ctl wfilter_ctls[];
extern const ffui_ldr_ctl wgoto_ctls[];
extern const ffui_ldr_ctl winfo_ctls[];
extern const ffui_ldr_ctl wlog_ctls[];
extern const ffui_ldr_ctl wplayprops_ctls[];
extern const ffui_ldr_ctl wrec_ctls[];
extern const ffui_ldr_ctl wuri_ctls[];

typedef struct ggui {
	fflock lktrk, lklog;
	gui_trk *curtrk; //currently playing track
	const fmed_queue *qu;
	const fmed_track *track;
	ffsem sem;
	uint load_err;
	int vol;
	int sort_col;

	uint go_pos;
	ffarr filenames; //char*[]
	void *rec_trk;

	uint theme_index;
	ffarr themes; //struct theme[]

	ffui_menu mfile
		, mlist
		, mplay
		, mrec
		, mconvert
		, mhelp
		, mtray
		, mlist_popup;
	ffui_dialog dlg;

	gui_wmain wmain;
	struct gui_wabout *wabout;
	struct gui_wconvert *wconvert;
	struct gui_wdevlist *wdev;
	struct gui_wfilter *wfilter;
	struct gui_wgoto *wgoto;
	struct gui_winfo *winfo;
	struct gui_wlog *wlog;
	struct gui_wplayprops *wplayprops;
	struct gui_wrec *wrec;
	struct gui_wuri *wuri;

	ffthd th;

	ffarr ghks; //struct ghk_ent[]

	byte seek_step_delta;
	byte seek_leap_delta;
	byte minimize_to_tray;
	byte status_tray;
	byte autosave_playlists;
	byte theme_startup;
	byte list_random;
	byte sel_after_cur; // automatically select a playlist entry for the active track
	ushort list_col_width[16];
	uint list_actv_trk_idx;
	uint list_scroll_pos;
	uint fdel_method; // enum FDEL_METHOD
	uint min_tray :1
		, list_filter :1
		, sort_reverse :1
		, devlist_rec :1 // whether 'device list' window is opened for capture devices
		;
	int itab_convert; // index of "conversion" tab;  -1:none
	int fav_pl; // Favorites playlist index;  -1:none
	struct guiconf conf;
} ggui;

extern const fmed_core *core;
extern ggui *gg;

enum FDEL_METHOD {
	FDEL_DEFAULT,
	FDEL_RENAME,
};

enum {
	DLG_FILT_INPUT,
	DLG_FILT_OUTPUT,
	DLG_FILT_PLAYLISTS,
};

#define GUI_USERCONF  "fmedia.gui.conf"
#define FMED_USERCONF  "fmedia-user.conf"
#define GUI_PLIST_NAME  "list%u.m3u8"
#define GUI_FAV_NAME  "fav.m3u8"

enum ST {
	ST_PLAYING = 1,
	ST_PAUSED,
};

struct gui_trk {
	uint state;
	uint lastpos;
	uint sample_rate;
	uint total_time_sec;

	fmed_filt *d;
	void *trk;
	fmed_que_entry *qent;

	uint goback :1
		, conversion :1;
};


enum CMDS {
	PLAY = 1,
	A_PLAY_PAUSE,
	A_PLAY_STOP,
	A_PLAY_STOP_AFTER,
	A_PLAY_NEXT,
	A_PLAY_PREV,
	A_PLAY_REPEAT,

	A_PLAY_SEEK,
	A_PLAY_FFWD,
	A_PLAY_RWND,
	A_PLAY_LEAP_FWD,
	A_PLAY_LEAP_BACK,
	A_PLAY_GOTO,
	A_PLAY_GOPOS,
	A_PLAY_SETGOPOS,

	A_PLAY_VOL,
	A_PLAY_VOLUP,
	A_PLAY_VOLDOWN,
	A_PLAY_VOLRESET,
	A_SHOW_PROPS,

	SEEKING,
	GOTO_SHOW,

// file operations:
	A_FILE_SHOWINFO,
	A_FILE_SHOWPCM,
	A_FILE_COPYFILE,
	A_FILE_COPYFN,
	A_FILE_SHOWDIR,
	A_FILE_DELFILE,

// playlist operations:
	A_LIST_NEW,
	A_LIST_CLOSE,
	A_LIST_SEL,
	A_LIST_SAVELIST,
	A_LIST_REMOVE,
	A_LIST_RANDOM,
	A_LIST_SORTRANDOM,
	A_LIST_RMDEAD,
	A_LIST_CLEAR,
	A_LIST_READMETA,
	A_LIST_SELALL,
	A_LIST_SELINVERT,
	A_LIST_SEL_AFTER_CUR,
	A_LIST_SORT,

	REC,
	REC_SETS,
	PLAYREC,
	MIXREC,
	SHOWRECS,
	DEVLIST_SHOWREC,
	DEVLIST_SHOW,
	DEVLIST_SELOK,

	SHOWCONVERT,
	SETCONVPOS_SEEK,
	SETCONVPOS_UNTIL,
	OUTBROWSE,
	CONVERT,
	CVT_SETS_EDIT,

	OPEN,
	ADD,
	ADDURL,
	TO_NXTLIST,
	INFOEDIT,
	FILTER_SHOW,
	FILTER_APPLY,
	FILTER_RESET,

	FAV_ADD,
	FAV_SHOW,

	SETTHEME, // 0xffffff00: mask for menu item index

	HIDE,
	SHOW,
	QUIT,
	ABOUT,
	CHECKUPDATE,

	CONF_EDIT,
	USRCONF_EDIT,
	FMEDGUI_EDIT,
	THEMES_EDIT,
	README_SHOW,
	CHANGES_SHOW,
	OPEN_HOMEPAGE,

	URL_ADD,
	URL_CLOSE,

	//private:
	ONCLOSE,
	LIST_DISPINFO,
	CVT_SETS_EDITDONE,
	CVT_ACTIVATE,
};

typedef void (*cmdfunc0)(void);
typedef void (*cmdfunc)(uint id);
typedef void (*cmdfunc2)(gui_trk *g, uint id);
typedef void (*cmdfudata)(uint id, void *udata);
typedef union {
	cmdfunc0 f0;
	cmdfunc f;
	cmdfudata fudata;
} cmdfunc_u;

enum CMDFLAGS {
	F1 = 0,
	F0 = 1,
	CMD_FUDATA = 2,
	CMD_FCORE = 0x10,
};

struct cmd {
	uint cmd;
	uint flags; // enum CMDFLAGS
	void *func; // cmdfunc*
};

extern const struct cmd cmd_add;

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n);

void* gui_getctl(void *udata, const ffstr *name);
void gui_media_open(uint id);
void gui_media_add1(const char *fn);
enum {
	ADDF_CHECKTYPE = 1, // don't add items of unsupported type
	ADDF_NOUPDATE = 2,
};
void gui_media_add2(const char *fn, int pl, uint flags);
void gui_media_showpcm(void);
/** Perform 'delete' operation on the specified items.
ents: fmed_que_entry*[]
Return 0 if all files were deleted. */
int gui_files_del(const ffarr *ents);

void gui_themes_read(void);
void gui_themes_add(uint def);
void gui_themes_destroy(void);
void gui_theme_set(int idx);

void wmain_init(void);
void gui_runcmd(const struct cmd *cmd, void *udata);
void gui_corecmd_op(uint cmd, void *udata);
void gui_corecmd_add(const struct cmd *cmd, void *udata);
void corecmd_addfunc(fftask_handler func, void *udata);
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid);
void wmain_update(uint playtime, uint time_total);
int gui_setmeta(gui_trk *g, fmed_que_entry *qent);
void gui_conv_progress(gui_trk *g);
void gui_clear(void);
void gui_status(const char *s, size_t len);
void wmain_status(const char *fmt, ...);
void gui_media_added(fmed_que_entry *ent);
void gui_rec(uint cmd);
char* gui_usrconf_filename(void);
char* gui_userpath(const char *fn);
void gui_upd_check(void);
void usrconf_write(void);
void fav_save(void);
void wmain_list_cols_width_write(ffconfw *conf);
void wmain_rec_started();
void wmain_rec_stopped();
void list_update(uint idx, int delta);

enum GUI_FILT {
	GUI_FILT_URL = 1,
	GUI_FILT_META = 2,
};
/**
@flags: enum GUI_FILT. */
void gui_filter(const ffstr *text, uint flags);

enum {
	GUI_TAB_CONVERT = 1,
	GUI_TAB_NOSEL = 2,
	GUI_TAB_FAV = 4,
};
int gui_newtab(uint flags);

void gui_que_new(void);
void gui_que_del(void);
void gui_que_sel(void);
void gui_showque(uint i);

double gui_getvol(void);
void gui_seek(uint cmd);

void wconvert_init();
void wconvert_destroy();
void wconv_show(uint show);
void gui_setconvpos(uint cmd);
int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
int wconvert_conf_writeval(ffstr *line, ffconfw *conf);

void wrec_init();
void wrec_destroy();
void rec_setdev(int idev);
int gui_conf_rec(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
void wrec_show(uint show);
int gui_rec_addsetts(void *trk);
int wrec_conf_writeval(ffstr *line, ffconfw *conf);
void wrec_showrecdir();

void wdev_init();
void wdev_show(uint show, uint cmd);

void winfo_init();
void winfo_show(uint show);

void wgoto_init();
void wgoto_show(uint show, uint pos);

void wuri_init();
void wuri_show(uint show);

void wfilter_init();
void wfilter_show(uint show);

void wabout_init();
void wabout_show(uint show);

void wplayprops_init();
void wplayprops_show(uint show);

void wlog_init();
void wlog_show(uint show);


enum CVTF {
	CVTF_EMPTY = 0x010000, //allow empty value
	CVTF_FLT = 0x020000,
	CVTF_STR = 0x040000,
	CVTF_MSEC = 0x080000,
	CVTF_FLT10 = 0x100000,
	CVTF_FLT100 = 0x200000,
	CVTF_NEWGRP = 0x400000,
};

#define SETT_EMPTY_INT  ((int)0x80000000)

struct cvt_set {
	uint settname;
	const char *name;
	const char *desc;
	uint flags; //enum CVTF
};

int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst);
int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist);

extern const char* const repeat_str[3];

#define dbglog0(...)  fmed_dbglog(core, NULL, "gui", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "gui", __VA_ARGS__)
#define syserrlog0(...)  fmed_syserrlog(core, NULL, "gui", __VA_ARGS__)
