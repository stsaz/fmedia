/** Shared data for network submodules.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>


#undef dbglog
#undef warnlog
#undef errlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, FILT_NAME, __VA_ARGS__)
#define warnlog(trk, ...)  fmed_warnlog(core, trk, FILT_NAME, __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, FILT_NAME, __VA_ARGS__)


typedef struct net_conf {
	uint bufsize;
	uint nbufs;
	uint buf_lowat;
	uint conn_tmout;
	uint tmout;
	byte user_agent;
	byte max_redirect;
	byte max_reconnect;
	byte meta;
	struct {
		char *host;
		uint port;
	} proxy;
} net_conf;

typedef struct netmod {
	const fmed_queue *qu;
	const fmed_track *track;
	net_conf conf;
} netmod;

extern netmod *net;
extern const fmed_core *core;
extern const char *const http_ua[];

extern const fmed_net_http http_iface;
extern const fmed_filter nethls;
