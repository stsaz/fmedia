/**
Copyright (c) 2016 Simon Zolin */

#include <FF/gui/winapi.h>
#include <FFOS/thread.h>


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
} gui_wmain;

typedef struct gui_wconvert {
	ffui_wnd wconvert;
	ffui_menu mmconv;
	ffui_label lfn, lsets;
	ffui_combx eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnsets;
	ffui_paned pnout;
} gui_wconvert;

typedef struct gui_wrec {
	ffui_wnd wrec;
	ffui_menu mmrec;
	ffui_label lfn, lsets;
	ffui_edit eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnsets;
	ffui_paned pnout;
} gui_wrec;

typedef struct gui_wdev {
	ffui_wnd wnd;
	ffui_view vdev;
	ffui_btn bok;
	ffui_paned pn;
} gui_wdev;

typedef struct gui_winfo {
	ffui_wnd winfo;
	ffui_view vinfo;
	ffui_paned pninfo;
} gui_winfo;

typedef struct gui_wgoto {
	ffui_wnd wgoto;
	ffui_edit etime;
	ffui_btn bgo;
} gui_wgoto;

typedef struct gui_wabout {
	ffui_wnd wabout;
	ffui_img ico;
	ffui_label labout;
	ffui_label lurl;
} gui_wabout;

typedef struct gui_wlog {
	ffui_wnd wlog;
	ffui_paned pnlog;
	ffui_edit tlog;
} gui_wlog;

typedef struct gui_wuri {
	ffui_wnd wuri;
	ffui_edit turi;
	ffui_btn bok;
	ffui_btn bcancel;
	ffui_paned pnuri;
} gui_wuri;

typedef struct gui_wfilter {
	ffui_wnd wnd;
	ffui_edit ttext;
	ffui_chbox cbfilename;
	ffui_btn breset;
	ffui_paned pntext;
} gui_wfilter;

typedef struct gui_trk gui_trk;

typedef struct cvt_sets_t {
	uint init :1;

	char *output;

	union {
	int vorbis_quality;
	float vorbis_quality_f;
	};
	uint opus_bitrate;
	int opus_frsize;
	int opus_bandwidth;
	int mpg_quality;
	int aac_quality;
	int aac_bandwidth;
	int flac_complevel;
	int flac_md5;
	int stream_copy;

	int format;
	int conv_pcm_rate;
	int channels;
	union {
	int gain;
	float gain_f;
	};
	char *meta;
	int seek;
	int until;

	int overwrite;
	int out_preserve_date;
} cvt_sets_t;

void cvt_sets_destroy(cvt_sets_t *sets);

typedef struct rec_sets_t {
	uint init :1;

	char *output;

	union {
	int vorbis_quality;
	float vorbis_quality_f;
	};
	uint opus_bitrate;
	int mpg_quality;
	int aac_quality;
	int flac_complevel;

	int lpbk_devno;
	int devno;
	int format;
	int sample_rate;
	int channels;
	union {
	int gain;
	float gain_f;
	};
	int until;
} rec_sets_t;

void rec_sets_destroy(rec_sets_t *sets);

typedef struct ggui {
	fflock lktrk, lklog;
	gui_trk *curtrk; //currently playing track
	const fmed_queue *qu;
	const fmed_track *track;
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
	gui_wconvert wconvert;
	gui_wrec wrec;
	gui_wdev wdev;
	gui_winfo winfo;
	gui_wgoto wgoto;
	gui_wabout wabout;
	gui_wlog wlog;
	gui_wuri wuri;
	gui_wfilter wfilter;

	ffthd th;
	cvt_sets_t conv_sets;
	rec_sets_t rec_sets;

	ffarr ghks; //struct ghk_ent[]

	byte seek_step_delta;
	byte seek_leap_delta;
	byte minimize_to_tray;
	byte status_tray;
	byte autosave_playlists;
	byte theme_startup;
	uint wconv_init :1
		, wrec_init :1
		, min_tray :1
		, list_filter :1
		, list_random :1
		, sort_reverse :1
		;
	uint state;
	int itab_convert; // index of "conversion" tab;  -1:none
} ggui;

const fmed_core *core;
ggui *gg;

enum {
	DLG_FILT_INPUT,
	DLG_FILT_OUTPUT,
	DLG_FILT_PLAYLISTS,
};

#define GUI_USERCONF  "fmedia.gui.conf"
#define FMED_USERCONF  "fmedia-user.conf"
#define GUI_PLIST_NAME  "list%u.m3u8"

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
	PAUSE,
	STOP,
	STOP_AFTER,
	NEXT,
	PREV,

	SEEK,
	SEEKING,
	FFWD,
	RWND,
	LEAP_FWD,
	LEAP_BACK,
	GOTO_SHOW,
	GOTO,
	GOPOS,
	SETGOPOS,

	VOL,
	VOLUP,
	VOLDOWN,
	VOLRESET,

	REC,
	REC_SETS,
	PLAYREC,
	MIXREC,
	SHOWRECS,
	DEVLIST_SHOWREC,
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
	QUE_NEW,
	QUE_DEL,
	QUE_SEL,
	SAVELIST,
	REMOVE,
	RANDOM,
	LIST_RMDEAD,
	CLEAR,
	SELALL,
	SELINVERT,
	SORT,
	TO_NXTLIST,
	SHOWDIR,
	COPYFN,
	COPYFILE,
	DELFILE,
	SHOWINFO,
	SHOWPCM,
	INFOEDIT,
	FILTER_SHOW,
	FILTER_APPLY,
	FILTER_RESET,
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

const struct cmd cmd_add;

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n);


void* gui_getctl(void *udata, const ffstr *name);
void gui_media_add1(const char *fn);
enum {
	ADDF_CHECKTYPE = 1, // don't add items of unsupported type
};
void gui_media_add2(const char *fn, uint flags);
void gui_media_showpcm(void);

void gui_themes_read(void);
void gui_themes_add(uint def);
void gui_themes_destroy(void);
void gui_theme_set(int idx);

void wmain_init(void);
void gui_runcmd(const struct cmd *cmd, void *udata);
void gui_corecmd_op(uint cmd, void *udata);
void gui_corecmd_add(const struct cmd *cmd, void *udata);
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid);
int gui_setmeta(gui_trk *g, fmed_que_entry *qent);
void gui_conv_progress(gui_trk *g);
void gui_clear(void);
void gui_status(const char *s, size_t len);
void gui_media_added(fmed_que_entry *ent);
void gui_media_removed(uint i);
void gui_rec(uint cmd);
char* gui_usrconf_filename(void);
char* gui_userpath(const char *fn);
void gui_upd_check(void);

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
};
int gui_newtab(uint flags);

void gui_que_new(void);
void gui_que_del(void);
void gui_que_sel(void);
void gui_showque(uint i);

double gui_getvol(void);
void gui_seek(uint cmd);

void wconvert_init(void);
void gui_showconvert(void);
void gui_setconvpos(uint cmd);
int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx);

int gui_conf_rec(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
void wrec_init(void);
void gui_rec_show(void);
int gui_rec_addsetts(void *trk);

void wdev_init(void);
void gui_dev_show(void);

void winfo_init(void);
void gui_media_showinfo(void);

void wgoto_init(void);

void wuri_init(void);

void wfilter_init(void);

void wabout_init(void);
