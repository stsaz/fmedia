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
	ffui_ctl stbar;
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

typedef struct gui_wabout {
	ffui_wnd wabout;
	ffui_ctl labout;
} gui_wabout;

typedef struct gui_wlog {
	ffui_wnd wlog;
	ffui_paned pnlog;
	ffui_edit tlog;
} gui_wlog;

typedef struct gui_trk gui_trk;

typedef struct ggui {
	fflock lktrk;
	gui_trk *curtrk;
	fflock lk;
	const fmed_queue *qu;
	const fmed_track *track;
	fftask cmdtask;
	void *play_id;
	char *rec_dir;
	ffstr rec_format;
	uint load_err;
	char *list_fn;

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
	gui_wabout wabout;
	gui_wlog wlog;

	ffthd th;

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

	//private:
	ONCLOSE,
	CVT_SETS_EDITDONE,
};

typedef void (*cmdfunc0)(void);
typedef void (*cmdfunc)(uint id);
typedef void (*cmdfunc2)(gui_trk *g, uint id);
typedef union {
	cmdfunc0 f0;
	cmdfunc f;
} cmdfunc_u;

enum CMDFLAGS {
	F1 = 0,
	F0 = 1,
	F2 = 2,
};

struct cmd {
	uint cmd;
	uint flags; // enum CMDFLAGS
	void *func; // cmdfunc*
};

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n);


void* gui_getctl(void *udata, const ffstr *name);
void gui_task_add(uint id);
void gui_addcmd(cmdfunc2 func, uint cmd);
void gui_media_add1(const char *fn);

void wmain_init(void);
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid);
void gui_clear(void);
void gui_status(const char *s, size_t len);
void gui_media_added(fmed_que_entry *ent);
void gui_media_removed(uint i);
void gui_que_new(void);
void gui_que_del(void);
void gui_que_sel(void);

void wconvert_init(void);
void gui_showconvert(void);

void winfo_init(void);
void gui_media_showinfo(void);

void wabout_init(void);
