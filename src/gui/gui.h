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
	ffui_ctl lpos;
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
	ffui_edit eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnsets;
	ffui_paned pnout;
} gui_wconvert;

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
	ffui_ctl labout;
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

	union {
	int ogg_quality;
	float ogg_quality_f;
	};
	int mpg_quality;
	int flac_complevel;

	int conv_pcm_rate;
	int conv_channels;
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

typedef struct ggui {
	fflock lktrk;
	gui_trk *curtrk;
	fflock lk;
	const fmed_queue *qu;
	const fmed_track *track;
	void *play_id;
	char *rec_dir;
	ffstr rec_format;
	uint load_err;

	uint go_pos;

	void *rec_trk;

	ffui_menu mfile
		, mplay
		, mrec
		, mconvert
		, mhelp
		, mtray;
	ffui_dialog dlg;

	gui_wmain wmain;
	gui_wconvert wconvert;
	gui_winfo winfo;
	gui_wgoto wgoto;
	gui_wabout wabout;
	gui_wlog wlog;
	gui_wuri wuri;

	ffthd th;
	cvt_sets_t conv_sets;

	uint wconv_init :1;
} ggui;

const fmed_core *core;
ggui *gg;

enum {
	DLG_FILT_INPUT,
	DLG_FILT_OUTPUT,
	DLG_FILT_PLAYLISTS,
};

#define GUI_USRCONF  "%APPDATA%/fmedia/fmedia.gui.conf"

enum ST {
	ST_PLAYING = 1,
	ST_PAUSE,
	ST_PAUSED,
};

struct gui_trk {
	uint state;
	uint lastpos;
	uint sample_rate;
	uint total_time_sec;
	uint gain;
	uint seekpos;

	fflock lkcmds;
	ffarr cmds; //struct cmd[]

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
	GOTO_SHOW,
	GOTO,
	GOPOS,
	SETGOPOS,

	VOL,
	VOLUP,
	VOLDOWN,

	REC,
	PLAYREC,
	MIXREC,
	SHOWRECS,

	SHOWCONVERT,
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

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n);


void* gui_getctl(void *udata, const ffstr *name);
void gui_addcmd(cmdfunc2 func, uint cmd);
void gui_media_add1(const char *fn);

void wmain_init(void);
void gui_runcmd(const struct cmd *cmd, void *udata);
void gui_corecmd_op(uint cmd, void *udata);
void gui_corecmd_add(const struct cmd *cmd, void *udata);
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid);
int gui_setmeta(gui_trk *g, fmed_que_entry *qent);
void gui_clear(void);
void gui_status(const char *s, size_t len);
void gui_media_added(fmed_que_entry *ent);
void gui_media_removed(uint i);
void gui_rec(uint cmd);
void gui_que_new(void);
void gui_que_del(void);
void gui_que_sel(void);

void gui_seek(uint cmd);

void wconvert_init(void);
void gui_showconvert(void);
int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx);

void winfo_init(void);
void gui_media_showinfo(void);

void wgoto_init(void);

void wuri_init(void);

void wabout_init(void);
