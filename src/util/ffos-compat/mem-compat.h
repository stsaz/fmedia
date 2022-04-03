/** Allocate N objects of type T. */
#define ffmem_allocT(N, T)  ((T*)ffmem_alloc((N) * sizeof(T)))

/** Allocate N objects of type T.  Zero the buffer. */
#define ffmem_callocT(N, T)  ((T*)ffmem_calloc(N, sizeof(T)))

/** Allocate an object of type T. */
#define ffmem_tcalloc1(T)  ((T*)ffmem_calloc(1, sizeof(T)))

/** Zero the object. */
#define ffmem_tzero(p)  memset(p, 0, sizeof(*(p)))


/** Safely reallocate memory buffer.
Return NULL on error and free the original buffer. */
static FFINL void* ffmem_saferealloc(void *ptr, size_t newsize)
{
	void *p = ffmem_realloc(ptr, newsize);
	if (p == NULL) {
		ffmem_free(ptr);
		return NULL;
	}
	return p;
}

#define ffmem_safefree(p)  ffmem_free(p)

#define ffmem_safefree0(p)  FF_SAFECLOSE(p, NULL, ffmem_free)

#define ffmem_free0(p) \
do { \
	ffmem_free(p); \
	p = NULL; \
} while (0)
