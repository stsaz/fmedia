
#define ffmem_alloc_S  "memory alloc"
#define ffmem_realloc_S  "memory realloc"
#define fffile_open_S  "file open"
#define fffile_seek_S  "file seek"
#define fffile_map_S  "file map"
#define fffile_rm_S  "file remove"
#define fffile_close_S  "file close"
#define fffile_rename_S  "file rename"
#define fffile_info_S  "get file info"
#define fffile_settime_S  "set file time"
#define fffile_read_S  "read"
#define fffile_write_S  "write"
#define fffile_nblock_S  "set non-blocking mode"
#define ffdir_open_S  "directory open"
#define ffdir_make_S  "directory make"
#define ffdir_rm_S  "directory remove"
#define ffpipe_create_S  "pipe create"
#define ffpipe_open_S  "pipe open"
#define ffkqu_create_S  "kqueue create"
#define ffkqu_attach_S  "kqueue attach"
#define ffkqu_wait_S  "kqueue wait"
#define ffskt_create_S  "socket create"
#define ffskt_bind_S  "socket bind"
#define ffskt_setopt_S  "socket option"
#define ffskt_nblock_S  "set non-blocking mode"
#define ffskt_shut_S  "socket shutdown"
#define ffskt_connect_S  "socket connect"
#define ffskt_listen_S  "socket listen"
#define ffskt_send_S  "socket send"
#define ffskt_recv_S  "socket recv"
#define ffdl_open_S  "library open"
#define ffdl_addr_S  "get library symbol"
#define fftmr_create_S  "timer create"
#define fftmr_start_S  "timer start"
#define ffthd_create_S  "thread create"
#define ffps_fork_S  "process fork"
#define ffaddr_info_S  "address resolve"

#ifdef FF_WIN

/**
return 0 if b = TRUE or (b = FALSE and IO_PENDING)
return -1 if b = FALSE and !IO_PENDING */
#define fferr_ioret(b) (((b) || GetLastError() == ERROR_IO_PENDING) ? 0 : -1)

#ifdef FF_MINGW
#undef EINVAL
#undef EEXIST
#undef EOVERFLOW
#undef ENOSPC
#undef EBADF
#undef ENOMEM
#undef EACCES
#undef ENOTEMPTY
#undef ETIMEDOUT
#undef EAGAIN
#undef ECANCELED
#undef EINTR
#undef ENOENT
#undef ENOSYS
#endif

#define EINVAL  ERROR_INVALID_PARAMETER
#define EEXIST  ERROR_ALREADY_EXISTS
#define EOVERFLOW  ERROR_INVALID_DATA
#define ENOSPC  ERROR_DISK_FULL
#define EBADF  ERROR_INVALID_HANDLE
#define ENOMEM  ERROR_NOT_ENOUGH_MEMORY
#define EACCES  ERROR_ACCESS_DENIED
#define ENOTEMPTY  ERROR_DIR_NOT_EMPTY
#define ETIMEDOUT  WSAETIMEDOUT
#define EAGAIN  WSAEWOULDBLOCK
#define ECANCELED  ERROR_OPERATION_ABORTED
#define EINTR  WAIT_TIMEOUT
#define ENOENT  ERROR_FILE_NOT_FOUND
#define ENOSYS  ERROR_NOT_SUPPORTED
#endif

#define fferr_fdlim(code)  fferr_fdlimit(code)
#define fferr_nofile(code)  fferr_notexist(code)
#define fferr_strp  fferr_strptr
