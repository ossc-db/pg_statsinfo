/*
 * lib/last_xact_activity.c
 *	 Track statement execution in current/last transaction.
 *
 * Copyright (c) 2009-2020, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "postgres.h"
#include "access/heapam.h"
#include "storage/proc.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "pgstat.h"
#include "utils/memutils.h"
#include "access/htup_details.h"
#include "utils/varlena.h"

#include "../common.h"
#include "pgut/pgut-be.h"
#include "wait_sampling.h"

/* For rusage */
#include <unistd.h>

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifndef HAVE_GETRUSAGE
#include "rusagestub.h"
#endif

#include "access/hash.h"
#include "access/parallel.h"
#include "executor/executor.h"
#include "optimizer/planner.h"
#include "postmaster/autovacuum.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "utils/builtins.h"
#include "utils/guc.h"

#define STATSINFO_RUSAGE_DUMP_FILE	PGSTAT_STAT_PERMANENT_DIRECTORY "/pg_statsinfo_rusage.stat"
/* Magic number identifying the stats file format */
static const uint32 STATSINFO_RUSAGE_FILE_HEADER = 0x20210930;

#define STATSINFO_RUSAGE_COLS	28
#define RUSAGE_BLOCK_SIZE	512

#define TIMEVAL_DIFF(start, end) ((double) end.tv_sec + (double) end.tv_usec / 1000000.0) \
		- ((double) start.tv_sec + (double) start.tv_usec / 1000000.0)

/* 
 * For hash table dealloc factors. (same as pg_stat_statements)
 * TODO: should share for wait-events-hash-table 
 */
#define STATSINFO_USAGE_INCREASE		(1.0)
#define STATSINFO_USAGE_DECREASE_FACTOR	(0.99) 
#define STATSINFO_USAGE_DEALLOC_PERCENT	5
#define STATSINFO_USAGE_INIT		(1.0)


/* store kinds. (same as pg_stat_statements) */
typedef enum ruStoreKind
{
	STATSINFO_RUSAGE_PLAN = 0,
	STATSINFO_RUSAGE_EXEC,
	STATSINFO_RUSAGE_NUMKIND
} ruStoreKind;

/* getrusage() counters. */
typedef struct ruCounters
{
	double				  usage;		  /* usage factor */
	float8				  utime;		  /* CPU user time */
	float8				  stime;		  /* CPU system time */
#ifdef HAVE_GETRUSAGE
	/* These fields are only used for platform with HAVE_GETRUSAGE defined */
	int64				   minflts;		/* page reclaims (soft page faults) */
	int64				   majflts;		/* page faults (hard page faults) */
	//int64				   nswaps;		 /* swaps (NOT supported on Linux)  */
	int64				   reads;		  /* Physical block reads */
	int64				   writes;		 /* Physical block writes */
	//int64				   msgsnds;		/* IPC messages sent (NOT supported on Linux)*/
	//int64				   msgrcvs;		/* IPC messages received (NOT supported on Linux)*/
	//int64				   nsignals;	   /* signals received (NOT supported on Linux)*/
	int64				   nvcsws;		 /* voluntary context witches */
	int64				   nivcsws;		/* unvoluntary context witches */
#endif
} ruCounters;

#define STATSINFO_RUSAGE_MAX_NESTED_LEVEL 64
static int ru_max = 0;   /* max entries. TODO: Sould use same setting of pg_stat_statements.max */
static struct   rusage exec_rusage_start[STATSINFO_RUSAGE_MAX_NESTED_LEVEL];
static struct   rusage plan_rusage_start[STATSINFO_RUSAGE_MAX_NESTED_LEVEL];

/* Hashtable key that defines the identity of a hashtable entry. (same as pg_stat_statements) */
typedef struct ruHashKey
{
	Oid	userid;		/* user OID */
	Oid	dbid;		/* database OID */
	uint64	queryid;	/* query identifier */
	bool	top;		/* query executed at top level*/
} ruHashKey;

/* rusage entry per database (same as pg_stat_statements) */
typedef struct ruEntry
{
	ruHashKey	key;					/* hash key of entry - MUST BE FIRST */
	ruCounters	counters[STATSINFO_RUSAGE_NUMKIND];	/* statistics for this query */
	slock_t		mutex;					/* protects the counters only */
} ruEntry;


/* Global statistics for rusage */
typedef struct ruGlobalStats
{
	int64		dealloc;		/* # of times entries were deallocated */
	TimestampTz	stats_reset;	/* timestamp with all stats reset */
} ruGlobalStats;

/* Global shared state (same as pg_stat_statements) */
typedef struct ruSharedState
{
	LWLock	*lock;				/* protects hashtable search/modification */
	LWLock	*queryids_lock;		/* protects queryids array */
	slock_t		mutex;			/* protects ruGlobalStats fields: */
	ruGlobalStats stats;		/* global statistics for rusage */
	uint64	queryids[FLEXIBLE_ARRAY_MEMBER];	/* queryid info for  parallel leaders */
} ruSharedState;

/*---- Local variables ----*/

/* Current nesting depth of ExecutorRun+ProcessUtility calls */
static int	exec_nested_level = 0;

/* Current nesting depth of planner calls */
static int	plan_nested_level = 0;


/* Links to shared memory state */
static ruSharedState *ru_ss = NULL;
static HTAB *ru_hash = NULL;

/*---- GUC variables ----*/

typedef enum
{
	STATSINFO_RUSAGE_TRACK_NONE,	/* track no statements */
	STATSINFO_RUSAGE_TRACK_TOP,	/* only top level statements */
	STATSINFO_RUSAGE_TRACK_ALL	/* all statements, including nested ones */
}	STATSINFO_RUSAGE_TrackLevel;

static const struct config_enum_entry ru_track_options[] =
{
	{"none", STATSINFO_RUSAGE_TRACK_NONE, false},
	{"top", STATSINFO_RUSAGE_TRACK_TOP, false},
	{"all", STATSINFO_RUSAGE_TRACK_ALL, false},
	{NULL, 0, false}
};

static int	ru_track;		/* tracking level */
static bool	ru_track_planning;	/* whether to track planning duration */
static bool	ru_track_utility;	/* whether to track utility duration */

#define ru_enabled(level) \
	((ru_track == STATSINFO_RUSAGE_TRACK_ALL && (level) < STATSINFO_RUSAGE_MAX_NESTED_LEVEL) || \
	(ru_track == STATSINFO_RUSAGE_TRACK_TOP && (level) == 0))

#define is_top(level) (level) == 0


/* For decision retrieving rusage of utility statements (same as pg_stat_statements) */
#define PGSS_HANDLED_UTILITY(n)		(!IsA(n, ExecuteStmt) && \
							!IsA(n, PrepareStmt) && \
							!IsA(n, DeallocateStmt))

#ifndef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/*
 * A struct to store the queries per backend.
 */
typedef struct statEntry
{
	Oid userid;			/* Session user id						*/
	int pid;			/* Backend PID. 0 means inactive entry	*/
	TransactionId xid;	/* Current transaction id				*/
	bool inxact;		/* If this backend is in transaction	*/
	int change_count;	/* For consistency check				*/
	char *queries;		/* Pointer to query buffer				*/
	char *current;		/* Append point for query string.		*/
	char *tail;			/* Terminal point for query storing.	*/
} statEntry;

typedef struct statBuffer
{
	int max_id;				/* Maximum entry id for this buffer This is maximum
							 *  backend id for the shared buffer, and simply
							 *  number of entries for the snapshot.
							 */
	statEntry entries[1];	/* Arrays of the entries 			*/
} statBuffer;

/* Hook storage */
static shmem_startup_hook_type	prev_shmem_startup_hook = NULL;
static planner_hook_type	prev_planner_hook = NULL;
static ExecutorStart_hook_type	prev_ExecutorStart_hook = NULL;
static ExecutorRun_hook_type	prev_ExecutorRun_hook = NULL;
static ExecutorFinish_hook_type	prev_ExecutorFinish_hook = NULL;
static ExecutorEnd_hook_type	prev_ExecutorEnd_hook = NULL;
static ProcessUtility_hook_type	prev_ProcessUtility_hook = NULL;

/* Backend local variables */
static MemoryContext pglxaContext = NULL;
static statBuffer *stat_buffer_snapshot = NULL;
static int buffer_size_per_backend = 1000;
static statBuffer *stat_buffer = NULL;
static char *query_buffer = NULL;
static int query_length_limit = 100;
static bool record_xact_commands = false;
static bool free_localdata_on_execend = false;
static bool immediate_exit_xact = false;

/* Module callbacks */
void		init_last_xact_activity(void);
void		fini_last_xact_activity(void);
Datum		statsinfo_last_xact_activity(PG_FUNCTION_ARGS);
void		last_xact_activity_clear_snapshot(void);

PG_FUNCTION_INFO_V1(statsinfo_last_xact_activity);

/* Internal functions */
static void clear_snapshot(void);
static void shmem_startup(void);
static void backend_shutdown_hook(int code, Datum arg);
static void myExecutorStart(QueryDesc *queryDesc, int eflags);
static void myExecutorEnd(QueryDesc *queryDesc);
static void attatch_shmem(void);
static void append_query(statEntry *entry, const char *query_string);
static void init_entry(int beid, Oid userid);
static char* get_query_entry(int beid);
static statEntry *get_stat_entry(int beid);
static void make_status_snapshot(void);
static statEntry *get_snapshot_entry(int beid);
static Size buffer_size(int nbackends);

static void myProcessUtility0(Node *parsetree, const char *queryString);
static void myProcessUtility(PlannedStmt *pstmt, const char *queryString,
			   bool readOnlyTree,
			   ProcessUtilityContext context, ParamListInfo params,
			   QueryEnvironment *queryEnv,
			   DestReceiver *dest, QueryCompletion *qc);


/* For rusage */
static void ru_shmem_shutdown(int code, Datum arg);
static Size ru_memsize(void);
static Size ru_queryids_array_size(void);

static void	 ru_entry_store(uint64 queryId, ruStoreKind kind, int level, ruCounters counters);
static ruEntry *ru_entry_alloc(ruHashKey *key);
static void	 ru_entry_dealloc(void);
static int	  ru_entry_cmp(const void *lhs, const void *rhs);
static void	 ru_entry_reset(void);
static uint32   ru_hash_fn(const void *key, Size keysize);
static int	  ru_match_fn(const void *key1, const void *key2, Size keysize);
static void	 ru_compute_counters(ruCounters *counters,
					  struct rusage *rusage_start,
					  struct rusage *rusage_end,
					  QueryDesc *queryDesc);
static void	ru_check_stat_statements(void);

static PlannedStmt * myPlanner(Query *parse,
			 const char *query_string,
			 int cursorOptions,
			 ParamListInfo boundParams);

static void myExecutorRun(QueryDesc *queryDesc,
				 ScanDirection direction,
				 uint64 count
				 ,bool execute_once);
static void myExecutorFinish(QueryDesc *queryDesc);

static void statsinfo_rusage_internal(FunctionCallInfo fcinfo);

Datum statsinfo_rusage(PG_FUNCTION_ARGS);
Datum statsinfo_rusage_reset(PG_FUNCTION_ARGS);
Datum statsinfo_rusage_info(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(statsinfo_rusage);
PG_FUNCTION_INFO_V1(statsinfo_rusage_reset);
PG_FUNCTION_INFO_V1(statsinfo_rusage_info);


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

#define TAKE_HOOK2(func, replace) \
	prev_##func##_hook = func##_hook; \
	func##_hook = replace;

#define TAKE_HOOK1(func) \
	TAKE_HOOK2(func, func);

#define RESTORE_HOOK(func) \
	func##_hook = prev_##func##_hook;

/*
 * Module load callbacks
 */
void
init_last_xact_activity(void)
{
	/* Custom GUC variables */
	DefineCustomIntVariable(GUC_PREFIX ".buffer_size",
							"Sets the query buffer size per backend.",
							NULL,
							&buffer_size_per_backend,
							buffer_size_per_backend,	/* default value */
							100,						/* minimum size  */
							INT_MAX,					/* maximum size  */
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".query_length_limit",
							"Sets the limit of the length of each query to store.",
							NULL,
							&query_length_limit,
							query_length_limit,		/* default value */
							10,						/* minimum limit */
							INT_MAX,				/* maximum limit */
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".record_xact_commands",
							 "Enables to store transaction commands.",
							 NULL,
							 &record_xact_commands,
							 record_xact_commands,	/* default value */
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
	
	DefineCustomIntVariable(GUC_PREFIX ".rusage_max",
							"Sets the maximum number of statements for rusage info..",
							NULL,
							&ru_max,
							5000,
							100,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomEnumVariable(GUC_PREFIX ".rusage_track",
							 "Sets the tracking level for rusage info.",
							 NULL,
							 &ru_track,
							 STATSINFO_RUSAGE_TRACK_TOP,
							 ru_track_options,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
	DefineCustomBoolVariable(GUC_PREFIX ".rusage_track_planning",
							 "Enable tracking rusage info on planning phase.",
							 NULL,
							 &ru_track_planning,
							 false,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
	DefineCustomBoolVariable(GUC_PREFIX ".rusage_track_utility",
							 "Enable tracking rusage info for Utility Statements.",
							 NULL,
							 &ru_track_utility,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);


	RequestAddinShmemSpace(buffer_size(MaxBackends));


	ru_check_stat_statements();
	RequestAddinShmemSpace(ru_memsize());
	RequestNamedLWLockTranche("pg_statsinfo_rusage", 2);

	TAKE_HOOK1(shmem_startup);
	TAKE_HOOK2(planner, myPlanner);
	TAKE_HOOK2(ExecutorStart, myExecutorStart);
	TAKE_HOOK2(ExecutorRun, myExecutorRun);
	TAKE_HOOK2(ExecutorFinish, myExecutorFinish);
	TAKE_HOOK2(ExecutorEnd, myExecutorEnd);
	TAKE_HOOK2(ProcessUtility, myProcessUtility);
}

/*
 * Module unload callback
 */
void
fini_last_xact_activity(void)
{
	/* Uninstall hooks. */
	RESTORE_HOOK(shmem_startup);
	RESTORE_HOOK(planner);
	RESTORE_HOOK(ExecutorStart);
	RESTORE_HOOK(ExecutorRun);
	RESTORE_HOOK(ExecutorFinish);
	RESTORE_HOOK(ExecutorEnd);
	RESTORE_HOOK(ProcessUtility);
}

/*
 * shmem_startup() - 
 *
 * Allocate or attach shared memory, and set up a process-exit hook function
 * for the buffer.
 * Additionary, load saved stats files.
 */
static void
shmem_startup(void)
{
	bool		found;
	HASHCTL		info;
	FILE		*file;
	int		i;
	uint32		header;
	int32		num;
	ruEntry		*buffer = NULL;

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	attatch_shmem();

	ru_ss = NULL;

	/* Create or attach to the shared memory state */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	/* global access lock */
	ru_ss = ShmemInitStruct("pg_statsinfo_rusage",
					(sizeof(ruSharedState) + ru_queryids_array_size()),
					&found);

	if (!found)
	{
		/* First time */
		LWLockPadded *locks = GetNamedLWLockTranche("pg_statsinfo_rusage");
		ru_ss->lock = &(locks[0]).lock;
		ru_ss->queryids_lock = &(locks[1]).lock;
		ru_ss->stats.dealloc = 0;
		ru_ss->stats.stats_reset = GetCurrentTimestamp();
	}

	ru_check_stat_statements();

	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(ruHashKey);
	info.entrysize = sizeof(ruEntry);
	info.hash = ru_hash_fn;
	info.match = ru_match_fn;

	/* allocate stats shared memory hash */
	ru_hash = ShmemInitHash("pg_statsinfo_rusage hash",
							  ru_max, ru_max,
							  &info,
							  HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);

	LWLockRelease(AddinShmemInitLock);

	/*
	 * Invalidate entry for this backend on cleanup.
	 */
	on_shmem_exit(backend_shutdown_hook, 0);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(ru_shmem_shutdown, (Datum) 0);

	/*
	 * Done if some other process already completed our initialization.
	 */
	if (found)
		return;

	/* Load stat file, don't care about locking */
	file = AllocateFile(STATSINFO_RUSAGE_DUMP_FILE, PG_BINARY_R);
	if (file == NULL)
	{
		if (errno == ENOENT)
			return;		 /* ignore not-found error */
		goto error;
	}

	/* check is header is valid */
	if (fread(&header, sizeof(uint32), 1, file) != 1 ||
		header != STATSINFO_RUSAGE_FILE_HEADER)
		goto error;

	/* get number of entries */
	if (fread(&num, sizeof(int32), 1, file) != 1)
		goto error;

	for (i = 0; i < num ; i++)
	{
		ruEntry		temp;
		ruEntry		*entry;

		if (fread(&temp, sizeof(ruEntry), 1, file) != 1)
			goto error;

		entry = ru_entry_alloc(&temp.key);

		/* copy in the actual stats */
		entry->counters[0] = temp.counters[0];
		entry->counters[1] = temp.counters[1];
		/* don't initialize spinlock, already done */
	}

	FreeFile(file);

	/*
	 * Remove the file so it's not included in backups/replication slaves,
	 * etc. A new file will be written on next shutdown.
	 */
	unlink(STATSINFO_RUSAGE_DUMP_FILE);

	return;

error:
	ereport(LOG,
			(errcode_for_file_access(),
			 errmsg("could not read pg_statsinfo rusage stat file \"%s\": %m",
					STATSINFO_RUSAGE_DUMP_FILE)));
	if (buffer)
		pfree(buffer);
	if (file)
		FreeFile(file);
	/* delete bogus file, don't care of errors in this case */
	unlink(STATSINFO_RUSAGE_DUMP_FILE);

}

/*
 * backend_shutdown_hook() -
 *
 * Invalidate status entry for this backend.
 */
static void
backend_shutdown_hook(int code, Datum arg)
{
	statEntry *entry = get_stat_entry(MyBackendId);
	if (entry)
		entry->pid = 0;
}

/*
 * ru_shmem_shutdown() -
 *
 * Save current stats into the file.
 */
static void
ru_shmem_shutdown(int code, Datum arg)
{
	FILE		*file;
	HASH_SEQ_STATUS	hash_seq;
	int32		num_entries;
	ruEntry		*entry;

	/* Don't try to dump during a crash. */
	if (code)
		return;

	if (!ru_ss)
		return;

	file = AllocateFile(STATSINFO_RUSAGE_DUMP_FILE ".tmp", PG_BINARY_W);
	if (file == NULL)
		goto error;

	if (fwrite(&STATSINFO_RUSAGE_FILE_HEADER, sizeof(uint32), 1, file) != 1)
		goto error;

	num_entries = hash_get_num_entries(ru_hash);

	if (fwrite(&num_entries, sizeof(int32), 1, file) != 1)
		goto error;

	hash_seq_init(&hash_seq, ru_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		if (fwrite(entry, sizeof(ruEntry), 1, file) != 1)
		{
			/* note: we assume hash_seq_term won't change errno */
			hash_seq_term(&hash_seq);
			goto error;
		}
	}

	if (FreeFile(file))
	{
		file = NULL;
		goto error;
	}

	/*
	 * Rename file inplace
	 */
	if (rename(STATSINFO_RUSAGE_DUMP_FILE ".tmp", STATSINFO_RUSAGE_DUMP_FILE) != 0)
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not rename pg_statsinfo rusage file \"%s\": %m",
						STATSINFO_RUSAGE_DUMP_FILE ".tmp")));

	return;

error:
	ereport(LOG,
			(errcode_for_file_access(),
			 errmsg("could not read pg_statsinfo rusage file \"%s\": %m",
					STATSINFO_RUSAGE_DUMP_FILE)));

	if (file)
		FreeFile(file);
	unlink(STATSINFO_RUSAGE_DUMP_FILE);
}

/*
 * is_shared_preload_prior - does prior appear before target in shared-preload-libraries ?
 */
static bool
is_shared_preload_prior(const char *prior, const char *target)
{
	char			*rawstring;
	List			*elemlist;
	ListCell		*cell;
	bool			 find = false;

	if (shared_preload_libraries_string == NULL ||
			shared_preload_libraries_string[0] == '\0')
			return false;

	/* need a modifiable copy of string */
	rawstring = pstrdup(shared_preload_libraries_string);

	/* parse string into list of identifiers */
	SplitIdentifierString(rawstring, ',', &elemlist);

	foreach (cell, elemlist)
	{
		/* reach the target, break */
		if (strcmp((char *) lfirst(cell), target) == 0)
		{
			break;
		}
		/* find prior*/
		else if (strcmp((char *) lfirst(cell), prior) == 0)
		{
			find = true;
		}

	}

	pfree(rawstring);
	list_free(elemlist);
	return find;
}

static void
ru_check_stat_statements(void)
{
	const char *pgss_max;
	const char *name = "pg_stat_statements.max";
	const char *pgss = "pg_stat_statements";
	const char *pgst = "pg_statsinfo";

	/*
	 * Check if pg_stat_statements is loaded before statsinfo.
	 */
	if (!is_shared_preload_prior(pgss, pgst))
	{
		/*
		 * pg_stat_statements might not be loaded before statsinfo.
		 * In this case, queryId might be set 0 at Utility-hook in pg_stat_statments,
		 * so our Utility-hook could not handle those statements.
		 * For this reason, set ru_track_utility to false.
		 */
		if (ru_track_utility)
		{
			ru_track_utility = false;
			ereport(WARNING,
					errmsg("pg_statsinfo.ru_track_utility is set to false."),
					errhint("pg_statsinfo must be loaded after pg_stat_statements when enable ru_track_utility ."));
		}
	}
	else
	{
		pgss_max = GetConfigOptionByName(name, NULL, true);
		/* if ru_max is smaller than pgss_max, use pgss_max. */
		if (ru_max < atoi(pgss_max))
		{
			ru_max = atoi(pgss_max);
			ereport(LOG,
					errmsg("pg_statsinfo.rusage.max is changed from %d to %d.",
						ru_max, atoi(pgss_max)));
		}
	}
}

static void
ru_compute_counters(ruCounters *counters,
					  struct rusage *rusage_start,
					  struct rusage *rusage_end,
					  QueryDesc *queryDesc)
{
	/* Compute CPU time delta */
	counters->utime = TIMEVAL_DIFF(rusage_start->ru_utime, rusage_end->ru_utime);
	counters->stime = TIMEVAL_DIFF(rusage_start->ru_stime, rusage_end->ru_stime);

	if (queryDesc && queryDesc->totaltime)
	{
		/* Make sure stats accumulation is done */
		InstrEndLoop(queryDesc->totaltime);

		/* TODO: check Hz
		if (queryDesc->totaltime->total < (3. / my_linux_hz))
		{
			counters->stime = 0;
			counters->utime = queryDesc->totaltime->total;
		}
		*/
	}

#ifdef HAVE_GETRUSAGE
	/* Compute the rest of the counters */
	counters->minflts = rusage_end->ru_minflt - rusage_start->ru_minflt;
	counters->majflts = rusage_end->ru_majflt - rusage_start->ru_majflt;
	//counters->nswaps = rusage_end->ru_nswap - rusage_start->ru_nswap;
	counters->reads = rusage_end->ru_inblock - rusage_start->ru_inblock;
	counters->writes = rusage_end->ru_oublock - rusage_start->ru_oublock;
	//counters->msgsnds = rusage_end->ru_msgsnd - rusage_start->ru_msgsnd;
	//counters->msgrcvs = rusage_end->ru_msgrcv - rusage_start->ru_msgrcv;
	//counters->nsignals = rusage_end->ru_nsignals - rusage_start->ru_nsignals;
	counters->nvcsws = rusage_end->ru_nvcsw - rusage_start->ru_nvcsw;
	counters->nivcsws = rusage_end->ru_nivcsw - rusage_start->ru_nivcsw;
#endif
}

static void
ru_set_queryid(uint64 queryid)
{
	Assert(!IsParallelWorker());

	LWLockAcquire(ru_ss->queryids_lock, LW_EXCLUSIVE);
	ru_ss->queryids[MyBackendId] = queryid;
	LWLockRelease(ru_ss->queryids_lock);
}


static void
ru_entry_store(uint64 queryId, ruStoreKind kind,
				 int level, ruCounters counters)
{
	volatile ruEntry *e;

	ruHashKey key;
	ruEntry  *entry;

	/* Safety check... */
	if (!ru_ss || !ru_hash)
		return;

	/* Set up key for hashtable search */
	key.userid = GetUserId();
	key.dbid = MyDatabaseId;
	key.queryid = queryId;
	key.top = is_top(level);

	/* Lookup the hash table entry with shared lock. */
	LWLockAcquire(ru_ss->lock, LW_SHARED);

	entry = (ruEntry *) hash_search(ru_hash, &key, HASH_FIND, NULL);

	/* Create new entry, if not present */
	if (!entry)
	{
		/* Need exclusive lock to make a new hashtable entry - promote */
		LWLockRelease(ru_ss->lock);
		LWLockAcquire(ru_ss->lock, LW_EXCLUSIVE);

		/* OK to create a new hashtable entry */
		entry = ru_entry_alloc(&key);
	}

	/*
	 * Grab the spinlock while updating the counters (see comment about
	 * locking rules at the head of the file)
	 */
	e = (volatile ruEntry *) entry;

	SpinLockAcquire(&e->mutex);

	e->counters[0].usage += STATSINFO_USAGE_INCREASE;

	e->counters[kind].utime += counters.utime;
	e->counters[kind].stime += counters.stime;
#ifdef HAVE_GETRUSAGE
	e->counters[kind].minflts += counters.minflts;
	e->counters[kind].majflts += counters.majflts;
	//e->counters[kind].nswaps += counters.nswaps;
	e->counters[kind].reads += counters.reads;
	e->counters[kind].writes += counters.writes;
	//e->counters[kind].msgsnds += counters.msgsnds;
	//e->counters[kind].msgrcvs += counters.msgrcvs;
	//e->counters[kind].nsignals += counters.nsignals;
	e->counters[kind].nvcsws += counters.nvcsws;
	e->counters[kind].nivcsws += counters.nivcsws;
#endif
	SpinLockRelease(&e->mutex);

	LWLockRelease(ru_ss->lock);
}

static ruEntry
*ru_entry_alloc(ruHashKey *key)
{
	ruEntry  *entry;
	bool		found;

	/* Make space if needed */
	while (hash_get_num_entries(ru_hash) >= ru_max)
		ru_entry_dealloc();

	/* Find or create an entry with desired hash code */
	entry = (ruEntry *) hash_search(ru_hash, key, HASH_ENTER, &found);

	if (!found)
	{
		/* New entry, initialize it */
		/* reset the statistics */
		memset(&entry->counters, 0, sizeof(ruCounters) * STATSINFO_RUSAGE_NUMKIND);
		/* set the appropriate initial usage count */
		entry->counters[0].usage = STATSINFO_USAGE_INIT ;
		/* re-initialize the mutex each time ... we assume no one using it */
		SpinLockInit(&entry->mutex);
	}

	return entry;
}

static void
ru_entry_dealloc(void)
{
	HASH_SEQ_STATUS hash_seq;
	ruEntry **entries;
	ruEntry  *entry;
	int			 nvictims;
	int			 i;

	/*
	 * Sort entries by usage and deallocate USAGE_DEALLOC_PERCENT of them.
	 * While we're scanning the table, apply the decay factor to the usage
	 * values.
	 */
	entries = palloc(hash_get_num_entries(ru_hash) * sizeof(ruEntry *));

	i = 0;
	hash_seq_init(&hash_seq, ru_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		entries[i++] = entry;
		entry->counters[0].usage *= STATSINFO_USAGE_DECREASE_FACTOR;
	}

	qsort(entries, i, sizeof(ruEntry *), ru_entry_cmp);

	nvictims = Max(10, i * STATSINFO_USAGE_DEALLOC_PERCENT / 100);
	nvictims = Min(nvictims, i);

	for (i = 0; i < nvictims; i++)
	{
		hash_search(ru_hash, &entries[i]->key, HASH_REMOVE, NULL);
	}

	pfree(entries);

	/* Increment the number of times entries are deallocated */
	{
		volatile ruSharedState *s = (volatile ruSharedState *) ru_ss;

		SpinLockAcquire(&s->mutex);
		s->stats.dealloc += 1;
		SpinLockRelease(&s->mutex);
	}
}

static int
ru_entry_cmp(const void *lhs, const void *rhs)
{
	double	  l_usage = (*(ruEntry *const *) lhs)->counters[0].usage;
	double	  r_usage = (*(ruEntry *const *) rhs)->counters[0].usage;

	if (l_usage < r_usage)
		return -1;
	else if (l_usage > r_usage)
		return +1;
	else
		return 0;
}

static void
ru_entry_reset(void)
{
	HASH_SEQ_STATUS hash_seq;
	ruEntry  *entry;

	LWLockAcquire(ru_ss->lock, LW_EXCLUSIVE);

	hash_seq_init(&hash_seq, ru_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		hash_search(ru_hash, &entry->key, HASH_REMOVE, NULL);
	}

 	/* Reset global statistics for rusage since all entries are removed. */
    {
        volatile ruSharedState *s = (volatile ruSharedState *) ru_ss;
        TimestampTz stats_reset = GetCurrentTimestamp();

        SpinLockAcquire(&s->mutex);
        s->stats.dealloc = 0;
        s->stats.stats_reset = stats_reset;
        SpinLockRelease(&s->mutex);
    }

	LWLockRelease(ru_ss->lock);
}

static PlannedStmt *
myPlanner(Query *parse,
			 const char *query_string,
			 int cursorOptions,
			 ParamListInfo boundParams)
{
	PlannedStmt *result;

	/*
	 * We can't process the query if no queryid has been computed.
	 *
	 * Note that planner_hook can be called from the planner itself, so we
	 * have a specific nesting level for the planner.  However, utility
	 * commands containing optimizable statements can also call the planner,
	 * same for regular DML (for instance for underlying foreign key queries).
	 * So testing the planner nesting level only is not enough to detect real
	 * top level planner call.
	 */
	if (ru_enabled(plan_nested_level + exec_nested_level)
		&& ru_track_planning
		&& parse->queryId != UINT64CONST(0))
	{
		struct rusage *rusage_start = &plan_rusage_start[plan_nested_level];
		struct rusage rusage_end;
		ruCounters counters;

		/* capture kernel usage stats in rusage_start */
		getrusage(RUSAGE_SELF, rusage_start);

		plan_nested_level++;
		PG_TRY();
		{
			if (prev_planner_hook)
				result = prev_planner_hook(parse, query_string, cursorOptions,
										   boundParams);
			else
				result = standard_planner(parse, query_string, cursorOptions,
										  boundParams);
			plan_nested_level--;
		}
		PG_CATCH();
		{
			plan_nested_level--;
			PG_RE_THROW();
		}
		PG_END_TRY();

		/* capture kernel usage stats in rusage_end */
		getrusage(RUSAGE_SELF, &rusage_end);

		ru_compute_counters(&counters, rusage_start, &rusage_end, NULL);

		/* store rusage info */
		ru_entry_store(parse->queryId, STATSINFO_RUSAGE_PLAN, plan_nested_level + exec_nested_level, counters);
	}
	else
	{
		if (prev_planner_hook)
			result = prev_planner_hook(parse, query_string, cursorOptions,
									   boundParams);
		else
			result = standard_planner(parse, query_string, cursorOptions,
									  boundParams);
	}

	return result;
}


/*
 * myExecutorStart() - 
 *
 * Collect activity of SQL execution.
 */
static void
myExecutorStart(QueryDesc *queryDesc, int eflags)
{
	statEntry *entry;

	if (ru_enabled(exec_nested_level) && (queryDesc->plannedstmt->queryId != UINT64CONST(0)))
	{
		struct rusage *rusage_start = &exec_rusage_start[exec_nested_level];

		/* capture kernel usage stats in rusage_start */
		getrusage(RUSAGE_SELF, rusage_start);

		/* Save the queryid so parallel worker can retrieve it */
		if (!IsParallelWorker())
		{
			ru_set_queryid(queryDesc->plannedstmt->queryId);
		}
	}

	if (prev_ExecutorStart_hook)
		prev_ExecutorStart_hook(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

	entry = get_stat_entry(MyBackendId);

	entry->change_count++;

	/*
	 * Single query executed when not in transaction.
	 */
	if (!entry->inxact)
	{
		init_entry(MyBackendId, GetSessionUserId());
		/*
		 * Remember to free activity snapshot on ExecutorEnd when we're out of
		 * transaction here.
		 */
		free_localdata_on_execend = true;
	}
	else
		free_localdata_on_execend = false;

	/*
	 * Do not change data when pid is inconsistent when transaction is active.
	 */
	if (!(entry->inxact && entry->pid != MyProc->pid))
	{
		entry->xid = MyProc->xid;
		append_query(entry, queryDesc->sourceText);
	}
	entry->change_count++;
	Assert((entry->change_count & 1) == 0);

	return;
}


static void
myExecutorRun(QueryDesc *queryDesc,
				 ScanDirection direction,
				 uint64 count
				 ,bool execute_once)
{
	exec_nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorRun_hook)
			prev_ExecutorRun_hook(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
		exec_nested_level--;
	}
	PG_CATCH();
	{
		exec_nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
myExecutorFinish(QueryDesc *queryDesc)
{
	exec_nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorFinish_hook)
			prev_ExecutorFinish_hook(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
		exec_nested_level--;
	}
	PG_CATCH();
	{
		exec_nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * myExecutorEnd() -
 * 
 * Hook function for finish of SQL execution.
 */
static void
myExecutorEnd(QueryDesc * queryDesc)
{
	uint64 queryId;
	struct rusage rusage_end;
	ruCounters counters;

	if (ru_enabled(exec_nested_level) && queryDesc->plannedstmt->queryId != UINT64CONST(0))
	{
		struct rusage *rusage_start = &exec_rusage_start[exec_nested_level];

		/* capture kernel usage stats in rusage_end */
		getrusage(RUSAGE_SELF, &rusage_end);

		if (IsParallelWorker())
		{
			LWLockAcquire(ru_ss->queryids_lock, LW_SHARED);
			queryId = ru_ss->queryids[ParallelLeaderBackendId];
			LWLockRelease(ru_ss->queryids_lock);
		}
		else
			queryId = queryDesc->plannedstmt->queryId;

		ru_compute_counters(&counters, rusage_start, &rusage_end, queryDesc);

		/* store rusage info */
		ru_entry_store(queryId, STATSINFO_RUSAGE_EXEC, exec_nested_level, counters);
	}

	if (prev_ExecutorEnd_hook)
		prev_ExecutorEnd_hook(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);

	if (free_localdata_on_execend)
		clear_snapshot();
}


static uint32
ru_hash_fn(const void *key, Size keysize)
{
	const ruHashKey *k = (const ruHashKey *) key;

	return hash_uint32((uint32) k->userid) ^
		hash_uint32((uint32) k->dbid) ^
		hash_uint32((uint32) k->queryid) ^
		hash_uint32((uint32) k->top);
}

/*
 * Compare two keys - zero means match
 */
static int
ru_match_fn(const void *key1, const void *key2, Size keysize)
{
	const ruHashKey *k1 = (const ruHashKey *) key1;
	const ruHashKey *k2 = (const ruHashKey *) key2;

	if (k1->userid == k2->userid &&
		k1->dbid == k2->dbid &&
		k1->queryid == k2->queryid &&
		k1->top == k2->top)
		return 0;
	else
		return 1;
}


/*
 * Erase in-transaction flag if needed.
 */
static void
exit_transaction_if_needed()
{
	if (immediate_exit_xact)
	{
		statEntry *entry = get_stat_entry(MyBackendId);
		
		entry->inxact = false;
		immediate_exit_xact = false;
	}
}

static void
myProcessUtility0(Node *parsetree, const char *queryString)
{
	statEntry *entry;
	TransactionStmt *stmt;

	entry = get_stat_entry(MyBackendId);

	/*
	 * Initialize stat entry if I find that the PID of this backend has changed
	 * unexpectedly.
	 */
	if (MyProc->pid != 0 && entry->pid != MyProc->pid)
		init_entry(MyBackendId, GetSessionUserId());

	switch (nodeTag(parsetree))
	{
		case T_TransactionStmt:
			/*
			 * Process transaction statements.
			 */
			stmt = (TransactionStmt *)parsetree;
			switch (stmt->kind)
			{
				case TRANS_STMT_BEGIN:
					entry->change_count++;
					init_entry(MyBackendId, GetSessionUserId());
					entry->inxact = true;
					break;
				case TRANS_STMT_COMMIT:
				case TRANS_STMT_ROLLBACK:
				case TRANS_STMT_PREPARE:
				case TRANS_STMT_COMMIT_PREPARED:
				case TRANS_STMT_ROLLBACK_PREPARED:
					clear_snapshot();
					entry->change_count++;
					entry->inxact = false;
					break;
				default:
					return;
			}
			if (record_xact_commands)
				append_query(entry, queryString);
			break;

		case T_LockStmt:
		case T_IndexStmt:
		case T_VacuumStmt:
		case T_AlterTableStmt:
		case T_DropStmt:  /* Drop TABLE */
		case T_TruncateStmt:
		case T_ReindexStmt:
		case T_ClusterStmt:
			/*
			 * These statements are simplly recorded.
			 */
			entry->change_count++;

			/*
			 * Single query executed when not in transaction.
			 */
			if (!entry->inxact)
			{
				immediate_exit_xact = true;
				init_entry(MyBackendId, GetSessionUserId());
				entry->inxact = true;
			}

			append_query(entry, queryString);

			break;

		default:
			return;
	}

	entry->change_count++;
	Assert((entry->change_count & 1) == 0);
}

/*
 * myProcessUtility() -
 *
 * Processing transaction state change.
 */
static void
myProcessUtility(PlannedStmt *pstmt, const char *queryString,
				 bool readOnlyTree,
				 ProcessUtilityContext context, ParamListInfo params,
				 QueryEnvironment *queryEnv,
				 DestReceiver *dest, QueryCompletion *qc)
{
	Node		*parsetree = pstmt->utilityStmt;
	uint64		saved_queryId = pstmt->queryId;
	/*
	 * Do my process before other hook runs.
	 */
	myProcessUtility0(pstmt->utilityStmt, queryString);

	/* Determine whether to get rusage next. */

	/* set queryID to 0 as same as pg_stat_statements */
	//if (ru_enabled(exec_nested_level) && ru_track_utility)
	//	pstmt->queryId = UINT64CONST(0);

	if (ru_track_utility && ru_enabled(exec_nested_level) &&
			PGSS_HANDLED_UTILITY(parsetree))
	{
		struct rusage *rusage_start = &exec_rusage_start[exec_nested_level];
		struct rusage rusage_end;
		ruCounters counters;

		/* capture kernel usage stats in rusage_start */
		getrusage(RUSAGE_SELF, rusage_start);
		exec_nested_level++;

		PG_TRY();
		{
			if (prev_ProcessUtility_hook)
				prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree, context, params,
									 queryEnv, dest, qc);
				else
				standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
									queryEnv, dest, qc);
		}
		PG_FINALLY();
		{
			exec_nested_level--;
			exit_transaction_if_needed();
		}
		PG_END_TRY();

		/* capture kernel usage stats in rusage_end */
		getrusage(RUSAGE_SELF, &rusage_end);

		ru_compute_counters(&counters, rusage_start, &rusage_end, NULL);

		/* store rusage info */
		ru_entry_store(saved_queryId, STATSINFO_RUSAGE_EXEC, exec_nested_level, counters);
	}
	else
	{
		PG_TRY();
		{
			if (prev_ProcessUtility_hook)
				prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree, context, params,
									 queryEnv, dest, qc);
				else
				standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
									queryEnv, dest, qc);
		}
		PG_FINALLY();
		{
			exit_transaction_if_needed();
		}
		PG_END_TRY();
	}
}

/*
 * Reset rusage statistics.
 */
Datum
statsinfo_rusage_reset(PG_FUNCTION_ARGS)
{
	if (!ru_ss)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_statsinfo must be loaded via shared_preload_libraries")));

	ru_entry_reset();
	PG_RETURN_VOID();
}

Datum
statsinfo_rusage(PG_FUNCTION_ARGS)
{
		statsinfo_rusage_internal(fcinfo);

		return (Datum) 0;
}

static void
statsinfo_rusage_internal(FunctionCallInfo fcinfo)
{
	ReturnSetInfo   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext   per_query_ctx;
	MemoryContext   oldcontext;
	TupleDesc		   tupdesc;
	Tuplestorestate *tupstore;
	HASH_SEQ_STATUS hash_seq;
	ruEntry		   *entry;


	if (!ru_ss)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_statsinfo must be loaded via shared_preload_libraries")));
	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
							"allowed in this context")));

	/* Switch into long-lived context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	LWLockAcquire(ru_ss->lock, LW_SHARED);

	hash_seq_init(&hash_seq, ru_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		Datum		   values[STATSINFO_RUSAGE_COLS];
		bool			nulls[STATSINFO_RUSAGE_COLS];
		ruCounters	tmp;
		int				 i = 0;
		int				 kind, min_kind = 0;
#ifdef HAVE_GETRUSAGE
		int64		   reads, writes;
#endif

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[i++] = Int64GetDatum(entry->key.queryid);
		values[i++] = BoolGetDatum(entry->key.top);
		values[i++] = ObjectIdGetDatum(entry->key.userid);
		values[i++] = ObjectIdGetDatum(entry->key.dbid);

		for (kind = min_kind; kind < STATSINFO_RUSAGE_NUMKIND; kind++)
		{
			/* copy counters to a local variable to keep locking time short */
			{
				volatile ruEntry *e = (volatile ruEntry *) entry;

				SpinLockAcquire(&e->mutex);
				tmp = e->counters[kind];
				SpinLockRelease(&e->mutex);
			}

#ifdef HAVE_GETRUSAGE
			reads = tmp.reads * RUSAGE_BLOCK_SIZE;
			writes = tmp.writes * RUSAGE_BLOCK_SIZE;
			values[i++] = Int64GetDatumFast(reads);
			values[i++] = Int64GetDatumFast(writes);
#else
			nulls[i++] = true; /* reads */
			nulls[i++] = true; /* writes */
#endif
			values[i++] = Float8GetDatumFast(tmp.utime);
			values[i++] = Float8GetDatumFast(tmp.stime);
#ifdef HAVE_GETRUSAGE
			values[i++] = Int64GetDatumFast(tmp.minflts);
			values[i++] = Int64GetDatumFast(tmp.majflts);
			//values[i++] = Int64GetDatumFast(tmp.nswaps);
			//values[i++] = Int64GetDatumFast(tmp.msgsnds);
			//values[i++] = Int64GetDatumFast(tmp.msgrcvs);
			//values[i++] = Int64GetDatumFast(tmp.nsignals);
			values[i++] = Int64GetDatumFast(tmp.nvcsws);
			values[i++] = Int64GetDatumFast(tmp.nivcsws);
#else
			nulls[i++] = true; /* minflts */
			nulls[i++] = true; /* majflts */
			//nulls[i++] = true; /* nswaps */
			//nulls[i++] = true; /* msgsnds */
			//nulls[i++] = true; /* msgrcvs */
			//nulls[i++] = true; /* nsignals */
			nulls[i++] = true; /* nvcsws */
			nulls[i++] = true; /* nivcsws */
#endif
		
		}

		   tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	LWLockRelease(ru_ss->lock);

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
}

#define RUSAGE_STATS_INFO_COLS 2

/* Return statistics of rusage. */
Datum
statsinfo_rusage_info(PG_FUNCTION_ARGS)
{
	ruGlobalStats	stats;
	TupleDesc		tupdesc;
	Datum			values[RUSAGE_STATS_INFO_COLS];
	bool			nulls[RUSAGE_STATS_INFO_COLS];

	if (!ru_ss || !ru_hash)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pg_statsinfo must be loaded via shared_preload_libraries")));

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, 0, sizeof(nulls));

	/* Read global statistics for rusage of pg_statsinfo */
	{
		volatile ruSharedState *s = (volatile ruSharedState *) ru_ss;

		SpinLockAcquire(&s->mutex);
		stats = s->stats;
		SpinLockRelease(&s->mutex);
	}

	values[0] = Int64GetDatum(stats.dealloc);
	values[1] = TimestampTzGetDatum(stats.stats_reset);

	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

#define LAST_XACT_ACTIVITY_COLS		4

/*
 * statsinfo_last_xact_activity() -
 *
 * Retrieve queries of last transaction.
 */
Datum
statsinfo_last_xact_activity(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc	tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();

		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		tupdesc = CreateTemplateTupleDesc(LAST_XACT_ACTIVITY_COLS);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "xid",
						   XIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "in_xact",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "queries",
						   TEXTOID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);
		funcctx->user_fctx = palloc0(sizeof(int));

		/* Return zero rows if module has not been loaded properly. */
		if (!stat_buffer)
		{
			MemoryContextSwitchTo(oldcontext);
			funcctx = SRF_PERCALL_SETUP();
			SRF_RETURN_DONE(funcctx);
		}

		if (PG_NARGS() == 0 || PG_ARGISNULL(0))
		{
			make_status_snapshot();
			funcctx->max_calls = stat_buffer_snapshot->max_id;
		}
		else
		{
			/*
			 * Get one backend - locate by pid
			 * Returns zero rows when not found the pid.
			 */

			int pid = PG_GETARG_INT32(0);
			int *user_fctx = (int*)(funcctx->user_fctx);
			int i;

			make_status_snapshot();

			for (i = 1 ; i <= stat_buffer_snapshot->max_id; i++)
			{
				statEntry *entry = get_snapshot_entry(i);
				if (entry && entry->pid == pid)
				{
					*user_fctx = i;
					break;
				}
			}

			if (*user_fctx == 0)
				/* If not found, return zero rows */
				funcctx->max_calls = 0;
			else
				funcctx->max_calls = 1;
		}
		
		MemoryContextSwitchTo(oldcontext);
	}
				
	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		/* for each row */
		Datum		values[LAST_XACT_ACTIVITY_COLS];
		bool		nulls[LAST_XACT_ACTIVITY_COLS];
		HeapTuple	tuple;
		statEntry  *entry;
		int *user_fctx = (int*)funcctx->user_fctx;

		MemSet(values, 0, sizeof(values));
		MemSet(nulls, 0, sizeof(nulls));

		/*
		 * *user_fctx > 0 when calling last_xact_activity with parameter
		 */
		if (*user_fctx > 0)
			entry = get_snapshot_entry(*user_fctx);
		else
			entry = get_snapshot_entry(funcctx->call_cntr + 1);
		

		values[0] = Int32GetDatum(entry->pid);
		if (entry->xid != 0)
			values[1] = TransactionIdGetDatum(entry->xid);
		else
			nulls[1] = true;
		values[2] = BoolGetDatum(entry->inxact);
		values[3] = CStringGetTextDatum(entry->queries);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		SRF_RETURN_DONE(funcctx);
	}
}

void
last_xact_activity_clear_snapshot(void)
{
	clear_snapshot();
}


/*
 * Local functions
 */

static void
clear_snapshot(void)
{
	if (pglxaContext)
	{
		MemoryContextDelete(pglxaContext);
		pglxaContext = NULL;
	}
			
	stat_buffer_snapshot = NULL;
}

/*
 * Create snap shot of last_xact_activity for stable return.
 */
static void
make_status_snapshot(void)
{
	volatile statEntry *entry;
	statEntry *local_entry;
	statBuffer *tmp_stat_buffer;
	char * local_queries;
	int nentries = 0;
	int beid;

	if (stat_buffer_snapshot) return;

	if (!stat_buffer) return;

	if (!pglxaContext)
		pglxaContext =
			AllocSetContextCreate(TopMemoryContext,
								  "Last activity snapshot",
								  ALLOCSET_SMALL_SIZES);
	tmp_stat_buffer =
		(statBuffer*)MemoryContextAllocZero(pglxaContext,
											buffer_size(stat_buffer->max_id));
	local_queries =	(char*)(&tmp_stat_buffer->entries[stat_buffer->max_id]);

	entry = stat_buffer->entries;
	local_entry = tmp_stat_buffer->entries;

	for (beid = 1 ; beid <= stat_buffer->max_id ; beid++)
	{
		while (true)
		{
			int saved_change_count = entry->change_count;
		
			if (entry->pid > 0)
			{
				memcpy(local_entry, (char*)entry, sizeof(statEntry));
				if (superuser() || entry->userid == GetSessionUserId())
				{
					/*
					 * strcpy here is safe because the tail of buffer is always
					 * '\0'
					 */
					strcpy(local_queries, entry->queries);
				}
				else
				{
					strcpy(local_queries, "<command string not enabled>");
				}
				local_entry->queries = local_queries;
			}

			if (saved_change_count == entry->change_count &&
				(saved_change_count & 1) == 0)
				break;

			/* Make sure we can break out of loop if stuck. */
			CHECK_FOR_INTERRUPTS();
		}

		entry++;

		/* Only valid entries get included in the local array */
		if (local_entry->pid > 0)
		{
			local_entry++;
			local_queries += buffer_size_per_backend;
			nentries++;
		}
	}

	/*
	 * max_id of snapshot buffer is the number of valid entries.
	 */
	tmp_stat_buffer->max_id = nentries;
	stat_buffer_snapshot = tmp_stat_buffer;
}

/*
 * get_snapshot_entry() -
 *
 * get entry of snapshot. pos is 1-based position.
 */
static statEntry *
get_snapshot_entry(int pos)
{
	if (pos < 1 || pos > stat_buffer_snapshot->max_id) return NULL;

	return &stat_buffer_snapshot->entries[pos - 1];
}

/*
 * Append string to queries buffer.
 */
static void
append_query(statEntry *entry, const char *query_string)
{
	int query_length;
	int limited_length;
	bool add_ellipsis = false;

	limited_length = entry->tail - entry->current;

	if (limited_length > query_length_limit)
		limited_length = query_length_limit;

	query_length = strlen(query_string);
	
	if (query_length > limited_length)
	{
		limited_length -= 4;
		query_length = pg_mbcliplen(query_string, query_length, limited_length);
		if (query_length == 0) return;
		add_ellipsis = true;
	}
	else 
	{
		int tail;
		tail = pg_mbcliplen(query_string, query_length, query_length - 1);
		if (tail == query_length - 1 && query_string[tail] == ';')
			query_length--;
	}
		
	memcpy(entry->current, query_string, query_length);
	entry->current += query_length;
	if (add_ellipsis) {
		*(entry->current++) = '.';
		*(entry->current++) = '.';
		*(entry->current++) = '.';
	}
	*(entry->current++) = ';';
	*entry->current = '\0';
}

static Size
buffer_size(int nbackends)
{
	/* Calculate the size of statBuffer */
	Size struct_size = (Size)&(((statBuffer*)0)->entries[nbackends]);

	/* Calculate the size of query buffers*/
	Size query_buffer_size = mul_size(buffer_size_per_backend, nbackends);

	return add_size(struct_size, query_buffer_size);
}

static char*
get_query_entry(int beid)
{
	if (beid < 1 || beid > stat_buffer->max_id) return NULL;
	return query_buffer + buffer_size_per_backend * (beid - 1);
}

static statEntry *
get_stat_entry(int beid) {
	if (beid < 1 || beid > stat_buffer->max_id) return NULL;
	return &stat_buffer->entries[beid - 1];
}

static void
init_entry(int beid, Oid userid)
{
	statEntry *entry;
	entry = get_stat_entry(beid);
	if (MyProc)
	{
		entry->pid = MyProc->pid;
		entry->xid = MyProc->xid;
	}
	entry->userid = userid;
	entry->inxact = false;
	entry->queries = get_query_entry(beid);
	entry->current = entry->queries;
	entry->tail = entry->current + buffer_size_per_backend - 1;
	*(entry->current) = '\0';
	*(entry->tail) = '\0';		/* Stopper on snapshot */
}

static void
attatch_shmem(void)
{
	bool	found;
	int		bufsize;
	int		max_backends = MaxBackends;

	bufsize = buffer_size(max_backends);

	/*
	 * stat_buffer is used to determine that this module is enabled or not
	 * afterwards, assuming ShmemInitStruct returns NULL when failed to acquire
	 * shared memory.
	 */
	stat_buffer = (statBuffer*)ShmemInitStruct("last_xact_activity",
											  bufsize,
											  &found);

	if (!found)
	{
		int beid;

		MemSet(stat_buffer, 0, bufsize);
		query_buffer = (char*)(&stat_buffer->entries[max_backends]);
		stat_buffer->max_id = max_backends;
		for (beid = 1 ; beid <= max_backends ; beid++)
			init_entry(beid, 0);
	}
}

static Size 
ru_memsize(void)
{
	Size	size;

	size = MAXALIGN(sizeof(ruSharedState));
	size = add_size(size, hash_estimate_size(ru_max, sizeof(ruEntry)));
	size = add_size(size, MAXALIGN(ru_queryids_array_size()));

	return size;
}

static Size
ru_queryids_array_size(void)
{
	return (sizeof(uint64) * (MaxConnections + autovacuum_max_workers + 1
							+ max_worker_processes + 1));
}
