
#ifndef ACTION_NAMES
	#define A(id)  id
#else
	#define A(id)  #id
#endif

	A(A_NONE),

	A(PLAY),
	A(A_PLAY_PAUSE),
	A(A_PLAY_STOP),
	A(A_PLAY_STOP_AFTER),
	A(A_PLAY_NEXT),
	A(A_PLAY_PREV),
	A(A_PLAY_REPEAT),

	A(A_PLAY_SEEK),
	A(A_PLAY_FFWD),
	A(A_PLAY_RWND),
	A(A_PLAY_LEAP_FWD),
	A(A_PLAY_LEAP_BACK),
	A(A_PLAY_GOTO),
	A(A_PLAY_GOPOS),
	A(A_PLAY_SETGOPOS),

	A(A_PLAY_VOL),
	A(A_PLAY_VOLUP),
	A(A_PLAY_VOLDOWN),
	A(A_PLAY_VOLRESET),
	A(A_SHOW_PROPS),

	A(SEEKING),
	A(GOTO_SHOW),

// file operations:
	A(A_FILE_SHOWINFO),
	A(A_FILE_SHOWPCM),
	A(A_FILE_COPYFILE),
	A(A_FILE_COPYFN),
	A(A_FILE_SHOWDIR),
	A(A_FILE_DELFILE),

// playlist operations:
	A(A_LIST_NEW),
	A(A_LIST_CLOSE),
	A(A_LIST_SEL),
	A(A_LIST_SAVELIST),
	A(A_LIST_REMOVE),
	A(A_LIST_RANDOM),
	A(A_LIST_SORTRANDOM),
	A(A_LIST_RMDEAD),
	A(A_LIST_CLEAR),
	A(A_LIST_READMETA),
	A(A_LIST_SELALL),
	A(A_LIST_SELINVERT),
	A(A_LIST_SEL_AFTER_CUR),
	A(A_LIST_SORT),

	A(REC),
	A(REC_SETS),
	A(PLAYREC),
	A(MIXREC),
	A(SHOWRECS),
	A(DEVLIST_SHOWREC),
	A(DEVLIST_SHOW),
	A(DEVLIST_SELOK),

	A(SHOWCONVERT),
	A(SETCONVPOS_SEEK),
	A(SETCONVPOS_UNTIL),
	A(OUTBROWSE),
	A(CONVERT),
	A(CVT_SETS_EDIT),

	A(OPEN),
	A(ADD),
	A(ADDURL),
	A(TO_NXTLIST),
	A(INFOEDIT),
	A(FILTER_SHOW),
	A(FILTER_APPLY),
	A(FILTER_RESET),

	A(FAV_ADD),
	A(FAV_SHOW),

	A(SETTHEME), // 0xffffff00: mask for menu item index

	A(HIDE),
	A(SHOW),
	A(QUIT),
	A(ABOUT),
	A(CHECKUPDATE),

	A(CONF_EDIT),
	A(USRCONF_EDIT),
	A(FMEDGUI_EDIT),
	A(THEMES_EDIT),
	A(README_SHOW),
	A(CHANGES_SHOW),
	A(OPEN_HOMEPAGE),

	A(URL_ADD),
	A(URL_CLOSE),

//private:
	A(ONCLOSE),
	A(LIST_DISPINFO),
	A(CVT_SETS_EDITDONE),
	A(CVT_ACTIVATE),

#undef A
