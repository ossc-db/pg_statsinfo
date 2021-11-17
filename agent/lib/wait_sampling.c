/*
 * lib/wait_sampling.c
 *     Collect statistics of wait events.
 *
 * Copyright (c) 2009-2020, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "postgres.h"

#include <unistd.h>


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
extern bool	profile_save;
extern HTAB			*pgws_hash;

/* Module callbacks */
void		init_wait_sampling(void);
void		fini_wait_sampling(void);

/* Internal functions */
static void pgws_shmem_shutdown(int code, Datum arg);
void pgws_shmem_startup(void);
static void attatch_shmem(void);
static Size pgws_memsize(void);
extern uint32 pgws_hash_fn(const void *key, Size keysize);
extern int pgws_match_fn(const void *key1, const void *key2, Size keysize);
extern void pgws_entry_alloc(pgwsEntry *item, bool direct);
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
 * Allocate or attach shared memory, and set up a process-exit hook function.
 */
void
pgws_shmem_startup(void)
{
	attatch_shmem();

    /*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(pgws_shmem_shutdown, 0);
}

/*
 * pgws_shmem_shutdown() -
 * Dump statistics into file.
 */
static void
pgws_shmem_shutdown(int code, Datum arg)
{
	FILE		*file;
	HASH_SEQ_STATUS	hash_seq;
	int32		num_entries;
	pgwsEntry	*entry;

	/* Don't try to dump during a crash. */
	if (code)
		return;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!pgws || !pgws_hash)
		return;

	/* Don't dump if told not to. */
	if (!profile_save)
		return;

	file = AllocateFile(STATSINFO_WS_DUMP_FILE ".tmp", PG_BINARY_W);
	if (file == NULL)
		goto error;

	if (fwrite(&STATSINFO_WS_FILE_HEADER, sizeof(uint32), 1, file) != 1)
		goto error;

	num_entries = hash_get_num_entries(pgws_hash);
	if (fwrite(&num_entries, sizeof(int32), 1, file) != 1)
		goto error;

	/* Serializing to disk. */
	hash_seq_init(&hash_seq, pgws_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		if (fwrite(entry, sizeof(pgwsEntry), 1, file) != 1)
		{
			/* note: we assume hash_seq_term won't change errno */
			hash_seq_term(&hash_seq);
			goto error;
		}
	}

	/* Dump global statistics */
	if (fwrite(&pgws->stats, sizeof(pgwsGlobalStats), 1, file) != 1)
		goto error;

	if (FreeFile(file))
	{
		file = NULL;
		goto error;
	}

	/* Rename. If failed, a LOG message would be recorded. */
	(void) durable_rename(STATSINFO_WS_DUMP_FILE ".tmp", STATSINFO_WS_DUMP_FILE, LOG);

	return;

error:
	ereport(LOG,
		(errcode_for_file_access(),
			errmsg("could not write pg_statsinfo wait sampling file \"%s\": %m",
				STATSINFO_WS_DUMP_FILE ".tmp")));

	if (file)
		FreeFile(file);
	unlink(STATSINFO_WS_DUMP_FILE ".tmp");

}

static void
attatch_shmem(void)
{
	FILE	*file = NULL;
	bool	found;
	HASHCTL		info;
	uint32		header;
	int32		num;
	int			i;

	/* reset in case this is a restart within the postmaster */
	pgws = NULL;
	pgws_hash = NULL;

	/*
	 * Create or attach to the shared memory state, including hash table
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	pgws = ShmemInitStruct("sample_wait_events", sizeof(pgwsSharedState), &found);

	if (!found)
	{
		/* First time through ... */
		pgws->lock = &(GetNamedLWLockTranche("sample_wait_events"))->lock;
		SpinLockInit(&pgws->mutex);
		pgws->stats.dealloc = 0;
		pgws->stats.stats_reset = GetCurrentTimestamp();
	}

	info.keysize = sizeof(pgwsHashKey);
	info.entrysize = sizeof(pgwsEntry);
	info.hash = pgws_hash_fn;
	info.match = pgws_match_fn;

	pgws_hash = ShmemInitHash("wait sampling hash",
							  pgws_max, pgws_max,
							  &info,
							  HASH_FUNCTION | HASH_ELEM | HASH_COMPARE);

	LWLockRelease(AddinShmemInitLock);

	/*
	 * Done if some other process already completed our initialization.
	 */
	if (found)
		return;

	if (!profile_save)
		return;

	file = AllocateFile(STATSINFO_WS_DUMP_FILE, PG_BINARY_R);	
	if (file == NULL)
	{
		if (errno != ENOENT)
			goto error;
		return;
	}

	if (fread(&header, sizeof(uint32), 1, file) != 1)
		goto error;

	if (header != STATSINFO_WS_FILE_HEADER)
		goto error;

	if (fread(&num, sizeof(int32), 1, file) != 1)
		goto error;


	/*
	 * NOTE: read and store the old stats to hash-table.
	 * It might be better to check profile_max and num(old stats number) before
	 * issue entry_alloc. Because if num >> ptofile_max (change param between
	 * PostgreSQL stop and start), it should cause high frequency dealloc()s.
	 * TODO: optimization to avoid the high-frequency dealloc()s.
	 */ 
	for (i = 0; i < num; i++)
	{
		pgwsEntry     temp;

		if (fread(&temp, sizeof(pgwsEntry), 1, file) != 1)
			goto error;

		/* enter this item to hash-table directly */
		pgws_entry_alloc(&temp, true);
	}

	/* Read global statistics. */
	if (fread(&pgws->stats, sizeof(pgwsGlobalStats), 1, file) != 1)
		goto error;

	FreeFile(file);

	unlink(STATSINFO_WS_DUMP_FILE);

	return;

error:
	ereport(LOG,
		(errcode_for_file_access(),
			errmsg("could not read pg_statsinfo wait sampling stat file \"%s\": %m",
				STATSINFO_WS_DUMP_FILE)));

	if (file)
		FreeFile(file);
	/* delete bogus file, don't care of errors in this case */
	unlink(STATSINFO_WS_DUMP_FILE);

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
