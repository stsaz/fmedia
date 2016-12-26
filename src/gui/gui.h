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
} gui_wmain;

typedef struct gui_wconvert {
	ffui_wnd wconvert;
	ffui_menu mmconv;
	ffui_combx eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnsets;
	ffui_paned pnout;
} gui_wconvert;

typedef struct gui_wrec {
	ffui_wnd wrec;
	ffui_menu mmrec;
	ffui_edit eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnsets;
	ffui_paned pnout;
} gui_wrec;

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

typedef struct gui_trk gui_trk;

typedef struct cvt_sets_t {
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
	fflock lktrk;
	gui_trk *curtrk;
	fflock lk;
	const fmed_queue *qu;
	const fmed_track *track;
	uint load_err;
	int vol;

	uint go_pos;
	ffarr filenames; //char*[]
	void *rec_trk;

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
	gui_winfo winfo;
	gui_wgoto wgoto;
	gui_wabout wabout;
	gui_wlog wlog;
	gui_wuri wuri;

	ffthd th;
	cvt_sets_t conv_sets;
	rec_sets_t rec_sets;

	byte seek_step_delta;
	byte seek_leap_delta;
	byte minimize_to_tray;
	byte portable_conf;
	uint wconv_init :1
		, wrec_init :1
		;
} ggui;

const fmed_core *core;
ggui *gg;

enum {
	DLG_FILT_INPUT,
	DLG_FILT_OUTPUT,
	DLG_FILT_PLAYLISTS,
};

#define GUI_USRCONF  "%APPDATA%/fmedia/fmedia.gui.conf"
#define GUI_USRCONF_PORT  "./fmedia.gui.conf"

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
	fftask task;

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
	INFOEDIT,

	HIDE,
	SHOW,
	QUIT,
	ABOUT,

	CONF_EDIT,
	USRCONF_EDIT,
	FMEDGUI_EDIT,
	README_SHOW,
	CHANGES_SHOW,
	OPEN_HOMEPAGE,

	URL_ADD,
	URL_CLOSE,

	//private:
	ONCLOSE,
	CVT_SETS_EDITDONE,
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

const struct cmd cmd_play;

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n);


void* gui_getctl(void *udata, const ffstr *name);
void gui_media_add1(const char *fn);

void wmain_init(void);
void gui_runcmd(const struct cmd *cmd, void *udata);
void gui_corecmd_op(uint cmd, void *udata);
void gui_corecmd_add(const struct cmd *cmd, void *udata);
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid);
int gui_setmeta(gui_trk *g, fmed_que_entry *qent);
void gui_clear(void);
void gui_status(const char *s, size_t len);
void gui_media_added(fmed_que_entry *ent, uint flags);
void gui_media_removed(uint i);
void gui_rec(uint cmd);
char* gui_usrconf_filename(void);

enum {
	GUI_TAB_CONVERT = 1,
	GUI_TAB_NOSEL = 2,
};
int gui_newtab(uint flags);

void gui_que_new(void);
void gui_que_del(void);
void gui_que_sel(void);

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

void winfo_init(void);
void gui_media_showinfo(void);

void wgoto_init(void);

void wuri_init(void);

void wabout_init(void);
