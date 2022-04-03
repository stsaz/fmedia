#define ffthd  ffthread
#define FFTHD_INV  FFTHREAD_NULL
#define FFTHDCALL  FFTHREAD_PROCCALL
#define ffthd_id  ffuint64
#define ffthd_create(proc, param, stack_size)  ffthread_create(proc, param, stack_size)
#define ffthd_join(th, timeout_ms, exit_code)  ffthread_join(th, timeout_ms, exit_code)
#define ffthd_detach(th)  ffthread_detach(th)
#define ffthd_sleep(ms)  ffthread_sleep(ms)
#define ffthd_curid()  ffthread_curid()
