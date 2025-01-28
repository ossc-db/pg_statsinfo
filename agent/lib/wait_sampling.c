/*
 * lib/wait_sampling.c
 *     Collect statistics of wait sample.
 *
 * Copyright (c) 2009-2025, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
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
wait_samplingSharedState *wait_sampling = NULL;
extern bool wait_sampling_queries;
extern int wait_sampling_max;
extern bool	wait_sampling_save;
extern HTAB			*wait_sampling_hash;

/* Module callbacks */
void		init_wait_sampling(void);
void		fini_wait_sampling(void);

/* Internal functions */
static void wait_sampling_shmem_shutdown(int code, Datum arg);
void wait_sampling_shmem_startup(void);
static void attatch_shmem(void);
static Size wait_sampling_memsize(void);
extern uint32 wait_sampling_hash_fn(const void *key, Size keysize);
extern int wait_sampling_match_fn(const void *key1, const void *key2, Size keysize);
extern void wait_sampling_entry_alloc(wait_samplingEntry *item, bool direct);
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
	RequestAddinShmemSpace(wait_sampling_memsize());
	RequestNamedLWLockTranche("sample_wait_sampling", 1);
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
 * wait_sampling_shmem_startup() - 
 *
 * Allocate or attach shared memory, and set up a process-exit hook function.
 */
void
wait_sampling_shmem_startup(void)
{
	attatch_shmem();

    /*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(wait_sampling_shmem_shutdown, 0);
}

/*
 * wait_sampling_shmem_shutdown() -
 * Dump statistics into file.
 */
static void
wait_sampling_shmem_shutdown(int code, Datum arg)
{
	FILE		*file;
	HASH_SEQ_STATUS	hash_seq;
	int32		num_entries;
	wait_samplingEntry	*entry;

	/* Don't try to dump during a crash. */
	if (code)
		return;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!wait_sampling || !wait_sampling_hash)
		return;

	/* Don't dump if told not to. */
	if (!wait_sampling_save)
		return;

	file = AllocateFile(STATSINFO_WS_DUMP_FILE ".tmp", PG_BINARY_W);
	if (file == NULL)
		goto error;

	if (fwrite(&STATSINFO_WS_FILE_HEADER, sizeof(uint32), 1, file) != 1)
		goto error;

	num_entries = hash_get_num_entries(wait_sampling_hash);
	if (fwrite(&num_entries, sizeof(int32), 1, file) != 1)
		goto error;

	/* Serializing to disk. */
	hash_seq_init(&hash_seq, wait_sampling_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		if (fwrite(entry, sizeof(wait_samplingEntry), 1, file) != 1)
		{
			/* note: we assume hash_seq_term won't change errno */
			hash_seq_term(&hash_seq);
			goto error;
		}
	}

	/* Dump global statistics */
	if (fwrite(&wait_sampling->stats, sizeof(wait_samplingGlobalStats), 1, file) != 1)
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
	wait_sampling = NULL;
	wait_sampling_hash = NULL;

	/*
	 * Create or attach to the shared memory state, including hash table
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	wait_sampling = ShmemInitStruct("sample_wait_sampling", sizeof(wait_samplingSharedState), &found);

	if (!found)
	{
		/* First time through ... */
		wait_sampling->lock = &(GetNamedLWLockTranche("sample_wait_sampling"))->lock;
		SpinLockInit(&wait_sampling->mutex);
		wait_sampling->stats.dealloc = 0;
		wait_sampling->stats.stats_reset = GetCurrentTimestamp();
	}

	info.keysize = sizeof(wait_samplingHashKey);
	info.entrysize = sizeof(wait_samplingEntry);
	info.hash = wait_sampling_hash_fn;
	info.match = wait_sampling_match_fn;

	wait_sampling_hash = ShmemInitHash("wait sampling hash",
							  wait_sampling_max, wait_sampling_max,
							  &info,
							  HASH_FUNCTION | HASH_ELEM | HASH_COMPARE);

	LWLockRelease(AddinShmemInitLock);

	/*
	 * Done if some other process already completed our initialization.
	 */
	if (found)
		return;

	if (!wait_sampling_save)
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
	 * It might be better to check wait_sampling_max and num(old stats number) before
	 * issue entry_alloc. Because if num >> wait_sampling_max (change param between
	 * PostgreSQL stop and start), it should cause high frequency dealloc()s.
	 * TODO: optimization to avoid the high-frequency dealloc()s.
	 */ 
	for (i = 0; i < num; i++)
	{
		wait_samplingEntry     temp;

		if (fread(&temp, sizeof(wait_samplingEntry), 1, file) != 1)
			goto error;

		/* enter this item to hash-table directly */
		wait_sampling_entry_alloc(&temp, true);
	}

	/* Read global statistics. */
	if (fread(&wait_sampling->stats, sizeof(wait_samplingGlobalStats), 1, file) != 1)
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
wait_sampling_memsize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(wait_samplingSharedState));
	size = add_size(size, hash_estimate_size(wait_sampling_max, sizeof(wait_samplingEntry)));
	size = add_size(size, hash_estimate_size(wait_sampling_max, sizeof(wait_samplingSubEntry)));

	return size;
}
