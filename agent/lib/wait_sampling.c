/*
 * lib/wait_sampling.c
 *     Collect statistics of wait events.
 *
 * Copyright (c) 2009-2020, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "postgres.h"
#include "storage/proc.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "utils/builtins.h"
#include "pgstat.h"
#include "utils/memutils.h"

#include "../common.h"
#include "pgut/pgut-be.h"

#include "wait_sampling.h"
#include "optimizer/planner.h"
#include "access/twophase.h"
#include "utils/datetime.h"

#ifndef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/* Backend local variables */
pgwsSharedState *pgws = NULL;
extern bool profile_queries;
extern int pgws_max;
extern HTAB			*pgws_hash;

/* Module callbacks */
void		init_wait_sampling(void);
void		fini_wait_sampling(void);

/* Internal functions */
static void backend_shutdown_hook(int code, Datum arg);
void pgws_shmem_startup(void);
static void attatch_shmem(void);
static Size pgws_memsize(void);
extern uint32 pgws_hash_fn(const void *key, Size keysize);
extern int pgws_match_fn(const void *key1, const void *key2, Size keysize);

// static void errout(char* format, ...) {
// 	va_list list;
// 
// 	FILE *f = fopen("/tmp/errout", "a");
// 	if (f == NULL) return;
// 	
// 	va_start(list, format);
// 	vfprintf(f, format, list);
// 	va_end(list);
// 	fclose(f);
// }

/*
 * Module load callbacks
 */
void
init_wait_sampling(void)
{
	RequestAddinShmemSpace(pgws_memsize());
	RequestNamedLWLockTranche("sample_wait_events", 1);
}

/*
 * Module unload callback
 */
void
fini_wait_sampling(void)
{
	/* do nothing */
}

/*
 * pgws_shmem_startup() - 
 *
 * Allocate or attach shared memory, and set up a process-exit hook function
 * for the buffer.
 */
void
pgws_shmem_startup(void)
{
	attatch_shmem();

	/*
	 * Invalidate entry for this backend on cleanup.
	 */
	on_shmem_exit(backend_shutdown_hook, 0);
}

/*
 * backend_shutdown_hook() -
 *
 * Invalidate status entry for this backend.
 */
static void
backend_shutdown_hook(int code, Datum arg)
{
	/* do nothing */
}

static void
attatch_shmem(void)
{
	bool	found;
	HASHCTL		info;

	/*
	 * Create or attach to the shared memory state, including hash table
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	info.hash = pgws_hash_fn;
	info.match = pgws_match_fn;
	info.keysize = sizeof(pgwsHashKey);
	info.entrysize = sizeof(pgwsEntry);

	pgws_hash = ShmemInitHash("wait sampling hash",
							  pgws_max, pgws_max,
							  &info,
							  HASH_FUNCTION | HASH_ELEM | HASH_COMPARE);

	pgws = ShmemInitStruct("sample_wait_events", sizeof(pgwsSharedState), &found);

	if (!found)
	{
		/* First time through ... */
		pgws->lock = &(GetNamedLWLockTranche("sample_wait_events"))->lock;
		SpinLockInit(&pgws->mutex);
		pgws->stats.dealloc = 0;
		pgws->stats.stats_reset = GetCurrentTimestamp();
	}

	LWLockRelease(AddinShmemInitLock);
}

/*
 * Estimate shared memory space needed.
 */
static Size
pgws_memsize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(pgwsSharedState));
	size = add_size(size, hash_estimate_size(pgws_max, sizeof(pgwsEntry)));
	size = add_size(size, hash_estimate_size(pgws_max, sizeof(pgwsSubEntry)));

	return size;
}
