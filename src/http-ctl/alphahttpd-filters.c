/** fmedia: HTTP filters for alphahttpd
2023, Simon Zolin */

#include <http/receive.h>
#include <http/request.h>
#include <http/virtspace.h>
#include <http/index.h>
#include <http/file.h>
#include <http/error.h>
#include <http/transfer.h>
#include <http/response.h>
#include <http/send.h>

const struct alphahttpd_filter* ah_filters[] = {
	&alphahttpd_filter_receive,
	&alphahttpd_filter_request,
	&alphahttpd_filter_virtspace,
	&alphahttpd_filter_index,
	&alphahttpd_filter_file,
	&alphahttpd_filter_error,
	&alphahttpd_filter_transfer,
	&alphahttpd_filter_response,
	&alphahttpd_filter_send,
	NULL
};
