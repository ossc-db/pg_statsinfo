/*
 * lib/libstatsinfo.c
 *
 * Copyright (c) 2009-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "libstatsinfo.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <float.h>

#include "access/hash.h"
#include "access/heapam.h"
#include "catalog/pg_type.h"
#include "catalog/pg_control.h"
#include "catalog/pg_tablespace.h"
#include "funcapi.h"
#include "libpq/pqsignal.h"
#include "mb/pg_wchar.h"
#include "regex/regex.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "postmaster/syslogger.h"
#include "postmaster/fork_process.h"
#include "postmaster/postmaster.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/ps_status.h"
#include "catalog/pg_collation.h"
#include "utils/timestamp.h"
#include "utils/rel.h"
#include "access/htup_details.h"
#include "postmaster/bgworker.h"
#include "postmaster/bgworker_internals.h"
#include "common/ip.h"
#include "utils/varlena.h"
#include "access/table.h"
#include "pgut/pgut-be.h"
#include "pgut/pgut-spi.h"
#include "../common.h"
#include "wait_sampling.h"
#include "rusage.h"

#define INVALID_PID			(-1)
#define START_WAIT_MIN		(10)
#define START_WAIT_MAX		(300)
#define STOP_WAIT_MIN		(10)
#define STOP_WAIT_MAX		(300)

/* also adjust non-critial setting parameters? */
/* #define ADJUST_NON_CRITICAL_SETTINGS */

#ifndef WIN32
#define PROGRAM_NAME		"pg_statsinfod"
#else
#define PROGRAM_NAME		"pg_statsinfo.exe"
#endif

#define TAKE_HOOK(func, replace) \
	prev_##func##_hook = func##_hook; \
	func##_hook = replace;

#define RESTORE_HOOK(func) \
	func##_hook = prev_##func##_hook;

/*
 * known message formats
 */

#define MSG_SHUTDOWN \
	"database system is shut down"

#define MSG_SHUTDOWN_SMART \
	"received smart shutdown request"

#define MSG_SHUTDOWN_FAST \
	"received fast shutdown request"

#define MSG_SHUTDOWN_IMMEDIATE \
	"received immediate shutdown request"

#define MSG_SIGHUP \
	"received SIGHUP, reloading configuration files"

/* log_autovacuum_min_duration: vacuum */
#define MSG_AUTOVACUUM \
	"%s vacuum%s \"%s.%s.%s\": index scans: %d\n" \
	"pages: %u removed, %u remain, %u scanned (%.2f%% of total)\n" \
	"tuples: %lld removed, %lld remain, %lld are dead but not yet removable\n" \
	"%s" \
	"removable cutoff: %u, which was %d XIDs old when operation ended\n" \
	"%s" \
	"avg read rate: %.3f %s, avg write rate: %.3f %s\n" \
	"buffer usage: %lld hits, %lld misses, %lld dirtied\n" \
	"WAL usage: %lld records, %lld full page images, %llu bytes\n" \
	"system usage: %s"

/* log_autovacuum_min_duration: analyze */
#define MSG_AUTOANALYZE \
	"automatic analyze of table \"%s.%s.%s\"\n" \
	"%s" \
	"avg read rate: %.3f %s, avg write rate: %.3f %s\n" \
	"buffer usage: %lld hits, %lld misses, %lld dirtied\n" \
	"system usage: %s"

/* log_checkpoint: staring */
#define MSG_CHECKPOINT_STARTING \
	"%s starting: %s"

/* log_checkpoint: complete */
#define MSG_CHECKPOINT_COMPLETE \
	"checkpoint complete: wrote %d buffers (%.1f%%); " \
	"%d WAL file(s) added, %d removed, %d recycled; " \
	"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s; " \
	"sync files=%d, longest=%ld.%03d s, average=%ld.%03d s; " \
	"distance=%d kB, estimate=%d kB; lsn=%X/%X, redo lsn=%X/%X"

/* log_restartpoint: complete */
#define MSG_RESTARTPOINT_COMPLETE \
	"restartpoint complete: wrote %d buffers (%.1f%%); " \
	"%d WAL file(s) added, %d removed, %d recycled; " \
	"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s; " \
	"sync files=%d, longest=%ld.%03d s, average=%ld.%03d s; " \
	"distance=%d kB, estimate=%d kB; lsn=%X/%X, redo lsn=%X/%X"

PG_MODULE_MAGIC;

static const char *
default_log_maintenance_command(void)
{
	char	bin_path[MAXPGPATH];
	char	command[MAXPGPATH];

	/* $PGHOME/bin */
	strlcpy(bin_path, my_exec_path, MAXPGPATH);
	get_parent_directory(bin_path);

	snprintf(command, sizeof(command),
		"%s/%s %%l", bin_path, "archive_pglog.sh");
	return pstrdup(command);
}

/*---- GUC variables ----*/

#define DEFAULT_SAMPLING_INTERVAL			5		/* sec */
#define DEFAULT_WAIT_SAMPLING_INTERVAL			10		/* msec */
#define DEFAULT_SNAPSHOT_INTERVAL			600		/* sec */
#define DEFAULT_SYSLOG_LEVEL				DISABLE
#define DEFAULT_TEXTLOG_LEVEL				WARNING
#define DEFAULT_REPOLOG_LEVEL				WARNING
#define DEFAULT_REPOLOG_BUFFER				10000
#define DEFAULT_REPOLOG_INTERVAL			10
#define DEFAULT_MAINTENANCE_TIME			"00:02:00"
#define DEFAULT_REPOSITORY_KEEPDAY			7		/* day */
#define DEFAULT_REPOLOG_KEEPDAY				7		/* day */
#define DEFAULT_LOG_MAINTENANCE_COMMAND		default_log_maintenance_command()
#define DEFAULT_LONG_LOCK_THRESHOLD			30		/* sec */
#define DEFAULT_STAT_STATEMENTS_MAX			30
#define DEFAULT_CONTROLFILE_FSYNC_INTERVAL	60		/* sec */
#define DEFAULT_LONG_TRANSACTION_MAX		10
#define LONG_TRANSACTION_THRESHOLD			1.0		/* sec */
#define DEFAULT_ENABLE_MAINTENANCE			"on"	/* snapshot + log */
#define DEFAULT_WAIT_SAMPLING_QUERIES				true
#define DEFAULT_WAIT_SAMPLING_MAX					25000

static const struct config_enum_entry elevel_options[] =
{
	{ "debug"	, DEBUG2 },
	{ "log"		, LOG },
	{ "info"	, INFO },
	{ "notice"	, NOTICE },
	{ "warning"	, WARNING },
	{ "error"	, ERROR },
	{ "fatal"	, FATAL },
	{ "panic"	, PANIC },
	{ "alert"	, ALERT },
	{ "disable"	, DISABLE },
	{ NULL }
};

#ifdef WIN32
static const struct config_enum_entry server_message_level_options[] = {
	{"debug", DEBUG2, true},
	{"debug5", DEBUG5, false},
	{"debug4", DEBUG4, false},
	{"debug3", DEBUG3, false},
	{"debug2", DEBUG2, false},
	{"debug1", DEBUG1, false},
	{"info", INFO, false},
	{"notice", NOTICE, false},
	{"warning", WARNING, false},
	{"error", ERROR, false},
	{"log", LOG, false},
	{"fatal", FATAL, false},
	{"panic", PANIC, false},
	{NULL, 0, false}
};
#endif

static const char *const RELOAD_PARAM_NAMES[] =
{
	"log_directory",
	"log_error_verbosity",
	"syslog_facility",
	"syslog_ident",
	GUC_PREFIX ".excluded_dbnames",
	GUC_PREFIX ".excluded_schemas",
	GUC_PREFIX ".stat_statements_max",
	GUC_PREFIX ".stat_statements_exclude_users",
	GUC_PREFIX ".repository_server",
	GUC_PREFIX ".sampling_interval",
	GUC_PREFIX ".wait_sampling_interval",
	GUC_PREFIX ".snapshot_interval",
	GUC_PREFIX ".syslog_line_prefix",
	GUC_PREFIX ".syslog_min_messages",
	GUC_PREFIX ".textlog_min_messages",
	GUC_PREFIX ".textlog_filename",
	GUC_PREFIX ".textlog_line_prefix",
	GUC_PREFIX ".textlog_permission",
	GUC_PREFIX ".repolog_min_messages",
	GUC_PREFIX ".repolog_buffer",
	GUC_PREFIX ".repolog_interval",
	GUC_PREFIX ".adjust_log_level",
	GUC_PREFIX ".adjust_log_info",
	GUC_PREFIX ".adjust_log_notice",
	GUC_PREFIX ".adjust_log_warning",
	GUC_PREFIX ".adjust_log_error",
	GUC_PREFIX ".adjust_log_log",
	GUC_PREFIX ".adjust_log_fatal",
	GUC_PREFIX ".textlog_nologging_users",
	GUC_PREFIX ".repolog_nologging_users",
	GUC_PREFIX ".enable_maintenance",
	GUC_PREFIX ".maintenance_time",
	GUC_PREFIX ".repository_keepday",
	GUC_PREFIX ".repolog_keepday",
	GUC_PREFIX ".log_maintenance_command",
	GUC_PREFIX ".controlfile_fsync_interval",
	GUC_PREFIX ".enable_alert",
	GUC_PREFIX ".target_server",
	GUC_PREFIX ".wait_sampling_queries",
	GUC_PREFIX ".wait_sampling_save",
	GUC_PREFIX ".collect_column",
	GUC_PREFIX ".collect_index",
	GUC_PREFIX ".rusage_save",
	GUC_PREFIX ".rusage_track",
	GUC_PREFIX ".rusage_track_planning",
	GUC_PREFIX ".rusage_track_utility"
};

static char	   *excluded_dbnames = NULL;
static char	   *excluded_schemas = NULL;
static char	   *repository_server = NULL;
static int		sampling_interval = DEFAULT_SAMPLING_INTERVAL;
static int		wait_sampling_interval = DEFAULT_WAIT_SAMPLING_INTERVAL;
static int		snapshot_interval = DEFAULT_SNAPSHOT_INTERVAL;
static char	   *syslog_line_prefix = NULL;
static int		syslog_min_messages = DEFAULT_SYSLOG_LEVEL;
static char	   *textlog_filename = NULL;
static char	   *textlog_line_prefix = NULL;
static int		textlog_min_messages = DEFAULT_TEXTLOG_LEVEL;
static int		textlog_permission = 0600;
static int		repolog_min_messages = DEFAULT_REPOLOG_LEVEL;
static int		repolog_buffer = DEFAULT_REPOLOG_BUFFER;
static int		repolog_interval = DEFAULT_REPOLOG_INTERVAL;
static bool		adjust_log_level = false;
static char	   *adjust_log_info = NULL;
static char	   *adjust_log_notice = NULL;
static char	   *adjust_log_warning = NULL;
static char	   *adjust_log_error = NULL;
static char	   *adjust_log_log = NULL;
static char	   *adjust_log_fatal = NULL;
static char	   *textlog_nologging_users = NULL;
static char	   *repolog_nologging_users = NULL;
static char	   *enable_maintenance = NULL;
static char	   *maintenance_time = NULL;
static int		repository_keepday = DEFAULT_REPOSITORY_KEEPDAY;
static int		repolog_keepday = DEFAULT_REPOLOG_KEEPDAY;
static char	   *log_maintenance_command = NULL;
static int		long_lock_threshold = DEFAULT_LONG_LOCK_THRESHOLD;
static int		stat_statements_max = DEFAULT_STAT_STATEMENTS_MAX;
static char	   *stat_statements_exclude_users = NULL;
static int		long_transaction_max = DEFAULT_LONG_TRANSACTION_MAX;
static int		controlfile_fsync_interval = DEFAULT_CONTROLFILE_FSYNC_INTERVAL;
static bool		enable_alert = false;
static char	   *target_server = NULL;
bool			wait_sampling_queries = DEFAULT_WAIT_SAMPLING_QUERIES;
int				wait_sampling_max = DEFAULT_WAIT_SAMPLING_MAX;
bool			wait_sampling_save = true;
extern wait_samplingSharedState	*wait_sampling;
static bool		collect_column = true;
static bool		collect_index = true;

int		rusage_max = 0;
bool	rusage_save = true;
int		rusage_track = STATSINFO_RUSAGE_TRACK_TOP;
bool	rusage_track_planning = false;
bool	rusage_track_utility = false;
/*---- Function declarations ----*/

PG_FUNCTION_INFO_V1(statsinfo_sample);
PG_FUNCTION_INFO_V1(statsinfo_sample_wait_sampling);
PG_FUNCTION_INFO_V1(statsinfo_sample_wait_sampling_reset);
PG_FUNCTION_INFO_V1(statsinfo_sample_wait_sampling_info);
PG_FUNCTION_INFO_V1(statsinfo_activity);
PG_FUNCTION_INFO_V1(statsinfo_long_xact);
PG_FUNCTION_INFO_V1(statsinfo_snapshot);
PG_FUNCTION_INFO_V1(statsinfo_maintenance);
PG_FUNCTION_INFO_V1(statsinfo_wait_sampling_profile);
PG_FUNCTION_INFO_V1(statsinfo_tablespaces);
PG_FUNCTION_INFO_V1(statsinfo_start);
PG_FUNCTION_INFO_V1(statsinfo_stop);
PG_FUNCTION_INFO_V1(statsinfo_cpustats);
PG_FUNCTION_INFO_V1(statsinfo_cpustats_noarg);
PG_FUNCTION_INFO_V1(statsinfo_devicestats);
PG_FUNCTION_INFO_V1(statsinfo_loadavg);
PG_FUNCTION_INFO_V1(statsinfo_memory);
PG_FUNCTION_INFO_V1(statsinfo_profile);
PG_FUNCTION_INFO_V1(statsinfo_cpuinfo);
PG_FUNCTION_INFO_V1(statsinfo_meminfo);

extern Datum PGUT_EXPORT statsinfo_sample(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_sample_wait_sampling(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_sample_wait_sampling_reset(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_sample_wait_sampling_info(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_activity(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_long_xact(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_snapshot(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_maintenance(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_wait_sampling_profile(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_tablespaces(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_start(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_stop(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_cpustats(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_cpustats_noarg(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_devicestats(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_loadavg(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_memory(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_profile(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_cpuinfo(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_meminfo(PG_FUNCTION_ARGS);

extern PGUT_EXPORT void	_PG_init(void);
extern PGUT_EXPORT void	_PG_fini(void);
extern PGUT_EXPORT void	request_last_xact_activity(void);
extern PGUT_EXPORT void	init_last_xact_activity(void);
extern PGUT_EXPORT void	fini_last_xact_activity(void);
extern PGUT_EXPORT void	init_wait_sampling(void);
extern PGUT_EXPORT void	fini_wait_sampling(void);
extern PGUT_EXPORT void	wait_sampling_shmem_startup(void);

/*----  Internal declarations ----*/

/* activity statistics */
typedef struct Activity
{
	int		samples;

	/* from pg_stat_activity */
	int		idle;
	int		idle_in_xact;
	int		waiting;
	int		running;
	int		max_backends;
} Activity;

/* hashtable key for long transaction */
typedef struct LongXactHashKey
{
	int				pid;
	TimestampTz		start;
} LongXactHashKey;

/* hashtable entry for long transaction */
typedef struct LongXactEntry
{
	LongXactHashKey	key;		/* hash key of entry - MUST BE FIRST */
	int				pid;
	TimestampTz		start;
	double			duration;	/* in sec */
	char			client[NI_MAXHOST];
	char			query[1];	/* VARIABLE LENGTH ARRAY - MUST BE LAST */
	/* Note: the allocated length of query[] is actually pgstat_track_activity_query_size */
} LongXactEntry;

/* structures describing the devices and partitions */
typedef struct DiskStats
{
	unsigned int	dev_major;		/* major number of device */
	unsigned int	dev_minor;		/* minor number of device */
	char			dev_name[128];	/* device name */
	unsigned long	rd_ios;			/* # of reads completed */
	unsigned long	rd_merges;		/* # of reads merged */
	unsigned long	rd_sectors;		/* # of sectors read */
	unsigned int	rd_ticks;		/* # of milliseconds spent reading */
	unsigned long	wr_ios;			/* # of writes completed */
	unsigned long	wr_merges;		/* # of writes merged */
	unsigned long	wr_sectors;		/* # of sectors written */
	unsigned int	wr_ticks;		/* # of milliseconds spent writing  */
	unsigned int	ios_pgr;		/* # of I/Os currently in progress */
	unsigned int	tot_ticks;		/* # of milliseconds spent doing I/Os */
	unsigned int	rq_ticks;		/* weighted # of milliseconds spent doing I/Os */
} DiskStats;

/* hashtable key for diskstats */
typedef struct DiskStatsHashKey
{
	unsigned int	dev_major;
	unsigned int	dev_minor;
} DiskStatsHashKey;

/* hashtable entry for diskstats */
typedef struct DiskStatsEntry
{
	DiskStatsHashKey	key;			/* hash key of entry - MUST BE FIRST */
	time_t				timestamp;		/* timestamp */
	unsigned int		field_num;		/* number of fields in diskstats */
	DiskStats			stats;			/* I/O statistics */
	float8				drs_ps_max;		/* number of sectors read per second (max) */
	float8				dws_ps_max;		/* number of sectors write per second (max) */
	int16				overflow_drs;	/* overflow counter of rd_sectors */
	int16				overflow_drt;	/* overflow counter of rd_ticks */
	int16				overflow_dws;	/* overflow counter of wr_sectors */
	int16				overflow_dwt;	/* overflow counter of wr_ticks */
	int16				overflow_dit;	/* overflow counter of rq_ticks */
} DiskStatsEntry;

/* structures for pg_statsinfo launcher state */
typedef struct silSharedState
{
	LWLockId	lockid;
	pid_t		pid;
} silSharedState;

static void StartStatsinfoLauncher(void);
PGDLLEXPORT void StatsinfoLauncherMain(Datum main_arg);
static void StatsinfoLauncherMainLoop(void);
static void sil_sigusr1_handler(SIGNAL_ARGS);
static void sil_sigusr2_handler(SIGNAL_ARGS);
static void sil_sighup_handler(SIGNAL_ARGS);
static void sil_sigchld_handler(SIGNAL_ARGS);
static pid_t exec_background_process(char cmd[], int *outStdin);
static void sample_activity(void);
static void sample_diskstats(void);
static void parse_diskstats(HTAB *diskstats);
static void must_be_superuser(void);
static int get_devinfo(const char *path, Datum values[], bool nulls[]);
static char *get_archive_path(void);
static void adjust_log_destination(GucContext context, GucSource source);
static pid_t get_postmaster_pid(void);
static bool verify_log_filename(const char *filename);
static bool verify_timestr(const char *timestr);
static bool postmaster_is_alive(void);
static bool is_shared_preload(const char *library);
static pid_t get_statsinfo_pid(const char *pid_file);
static void inet_to_cstring(const SockAddr *addr, char host[NI_MAXHOST]);
static Datum get_cpustats(FunctionCallInfo fcinfo,
	int64 prev_cpu_user, int64 prev_cpu_system, int64 prev_cpu_idle, int64 prev_cpu_iowait);
static int exec_grep(const char *filename, const char *regex, List **records);
static int exec_split(const char *rawstring, const char *regex, List **fields);
static bool parse_int64(const char *value, int64 *result);
static bool parse_float8(const char *value, double *result);
static uint32 lx_hash_fn(const void *key, Size keysize);
static int lx_match_fn(const void *key1, const void *key2, Size keysize);
static LongXactEntry *lx_entry_alloc(LongXactHashKey *key, PgBackendStatus *be);
static void lx_entry_dealloc(void);
static int lx_entry_cmp(const void *lhs, const void *rhs);
static uint32 ds_hash_fn(const void *key, Size keysize);
static int ds_match_fn(const void *key1, const void *key2, Size keysize);
static void sample_waits(void);
static void probe_waits(void);
uint32 wait_sampling_hash_fn(const void *key, Size keysize);
int wait_sampling_match_fn(const void *key1, const void *key2, Size keysize);
static uint32 wait_sampling_sub_hash_fn(const void *key, Size keysize);
static int wait_sampling_sub_match_fn(const void *key1, const void *key2, Size keysize);
void wait_sampling_entry_alloc(wait_samplingEntry *key, bool direct);
static void wait_sampling_entry_dealloc(void);
static int wait_sampling_entry_cmp(const void *lhs, const void *rhs);

#if defined(WIN32)
static int str_to_elevel(const char *name, const char *str,
						 const struct config_enum_entry *options);
#endif

static bool check_textlog_filename(char **newval, void **extra, GucSource source);
static bool check_enable_maintenance(char **newval, void **extra, GucSource source);
static bool check_maintenance_time(char **newval, void **extra, GucSource source);
static void pg_statsinfo_emit_log_hook(ErrorData *edata);
static bool is_log_level_output(int elevel, int log_min_level);
static emit_log_hook_type	prev_emit_log_hook = NULL;
static void pg_statsinfo_shmem_request_hook(void);
static void pg_statsinfo_shmem_startup_hook(void);
static void silShmemInit(void);
static Size silShmemSize(void);
static void lookup_sil_state(void);
static shmem_request_hook_type	prev_shmem_request_hook = NULL;
static shmem_startup_hook_type	prev_shmem_startup_hook = NULL;

static Activity		 activity = { 0, 0, 0, 0, 0, 0 };
static HTAB			*long_xacts = NULL;
static HTAB			*diskstats = NULL;
HTAB				*wait_sampling_hash = NULL;


/* variables for pg_statsinfo launcher */
static volatile bool	 got_SIGCHLD = false;
static volatile bool	 got_SIGUSR1 = false;
static volatile bool	 got_SIGUSR2 = false;
static volatile bool	 got_SIGHUP = false;
static silSharedState	*sil_state = NULL;

/*
 * statsinfo_sample - sample statistics for server instance.
 */
Datum
statsinfo_sample(PG_FUNCTION_ARGS)
{
	must_be_superuser();

	sample_activity();
	sample_diskstats();

	PG_RETURN_VOID();
}

/*
 * statsinfo_sample_wait_sampling - sample statistics of wait sampling for server instance.
 */
Datum
statsinfo_sample_wait_sampling(PG_FUNCTION_ARGS)
{
	must_be_superuser();

	sample_waits();

	PG_RETURN_VOID();
}

/*
 * statsinfo_sample_wait_sampling_reset - reset sample statistics of wait sampling for server instance.
 */
Datum
statsinfo_sample_wait_sampling_reset(PG_FUNCTION_ARGS)
{
	HASH_SEQ_STATUS hash_seq;
	wait_samplingEntry  *entry;

	must_be_superuser();

	LWLockAcquire(wait_sampling->lock, LW_EXCLUSIVE);

	/* Remove all entries. */
	hash_seq_init(&hash_seq, wait_sampling_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		hash_search(wait_sampling_hash, &entry->key, HASH_REMOVE, NULL);
	}

	LWLockRelease(wait_sampling->lock);

	/*
	 * Reset global statistics for sample_wait_sampling since all entries are
	 * removed.
	 */
	{
		volatile wait_samplingSharedState *s = (volatile wait_samplingSharedState *) wait_sampling;
		TimestampTz stats_reset = GetCurrentTimestamp();

		SpinLockAcquire(&s->mutex);
		s->stats.dealloc = 0;
		s->stats.stats_reset = stats_reset;
		SpinLockRelease(&s->mutex);
	}

	PG_RETURN_VOID();
}

/* Number of output arguments (columns) for sample_wait_sampling_info */
#define SAMPLE_WAIT_SAMPLING_INFO_COLS	2

/*
 * Return statistics of sample_wait_sampling.
 */
Datum
statsinfo_sample_wait_sampling_info(PG_FUNCTION_ARGS)
{
	wait_samplingGlobalStats stats;
	TupleDesc	tupdesc;
	Datum		values[SAMPLE_WAIT_SAMPLING_INFO_COLS];
	bool		nulls[SAMPLE_WAIT_SAMPLING_INFO_COLS];

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, 0, sizeof(nulls));

	/* Read global statistics for sample_wait_sampling_info */
	{
		volatile wait_samplingSharedState *s = (volatile wait_samplingSharedState *) wait_sampling;

		SpinLockAcquire(&s->mutex);
		stats = s->stats;
		SpinLockRelease(&s->mutex);
	}

	values[0] = Int64GetDatum(stats.dealloc);
	values[1] = TimestampTzGetDatum(stats.stats_reset);

	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

static void
sample_activity(void)
{
	TimestampTz	now;
	int			backends = 0;
	int			idle = 0;
	int			idle_in_xact = 0;
	int			waiting = 0;
	int			running = 0;
	int			i;

	if (!long_xacts)
	{
		/* create hash table when first needed */
		HASHCTL		ctl;

		ctl.keysize = sizeof(LongXactHashKey);
		ctl.entrysize = offsetof(LongXactEntry, query) +
							pgstat_track_activity_query_size;
		ctl.hash = lx_hash_fn;
		ctl.match = lx_match_fn;
		long_xacts = hash_create("Long Transaction",
								 long_transaction_max, &ctl,
								 HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);
	}

	now = GetCurrentTimestamp();

	for (i = pgstat_fetch_stat_numbackends(); i > 0; i--)
	{
		PgBackendStatus    *be;
		long				secs;
		int					usecs;
		double				duration;
		PGPROC			   *proc;
		LongXactHashKey		key;
		LongXactEntry	   *entry;
		int					procpid;

		be = pgstat_get_beentry_by_backend_id(i);
		if (!be)
			continue;

		procpid = be->st_procpid;
		if (procpid == 0)
			continue;

		/* ignore if not client backend */
		if (be->st_backendType != B_BACKEND)
			continue;

		/*
		 * sample idle transactions
		 */
		if (procpid != MyProcPid)
		{
			uint32	classId;

			proc = BackendPidGetProc(procpid);
			if (proc == NULL)
				 continue;	/* This backend is dead */

			classId = proc->wait_event_info & 0xFF000000;
			if (classId == PG_WAIT_LWLOCK ||
				classId == PG_WAIT_LOCK)
				waiting++;
			else if (be->st_state == STATE_IDLE)
				idle++;
			else if (be->st_state == STATE_IDLEINTRANSACTION)
				idle_in_xact++;
			else if (be->st_state == STATE_RUNNING)
				running++;

			backends++;
		}

		/*
		 * sample long transactions, but exclude vacuuming processes.
		 */
		if (be->st_xact_start_timestamp == 0)
			continue;

		TimestampDifference(be->st_xact_start_timestamp, now, &secs, &usecs);
		duration = secs + usecs / 1000000.0;
		if (duration < LONG_TRANSACTION_THRESHOLD)
			continue;

		/* XXX: needs lock? */
		if ((proc = BackendPidGetProc(be->st_procpid)) == NULL ||
			(proc->statusFlags & PROC_IN_VACUUM))
			continue;

		/* set up key for hashtable search */
		key.pid = be->st_procpid;
		key.start = be->st_xact_start_timestamp;

		/* lookup the hash table entry */
		entry = (LongXactEntry *) hash_search(long_xacts, &key, HASH_FIND, NULL);

		/* create new entry, if not present */
		if (!entry)
			entry = lx_entry_alloc(&key, be);

		if (be->st_state == STATE_IDLEINTRANSACTION)
			strlcpy(entry->query,
				"<IDLE> in transaction", pgstat_track_activity_query_size);
		else
			strlcpy(entry->query,
				be->st_activity_raw, pgstat_track_activity_query_size);

		entry->duration = duration;
	}

	activity.idle += idle;
	activity.idle_in_xact += idle_in_xact;
	activity.waiting += waiting;
	activity.running += running;

	if (activity.max_backends < backends)
		activity.max_backends = backends;

	activity.samples++;

	lx_entry_dealloc();
}

static void
sample_diskstats(void)
{
	if (!diskstats)
	{
		/* create hash table when first needed */
		HASHCTL		ctl;

		ctl.keysize = sizeof(DiskStatsHashKey);
		ctl.entrysize = sizeof(DiskStatsEntry);
		ctl.hash = ds_hash_fn;
		ctl.match = ds_match_fn;
		diskstats = hash_create("diskstats", 30, &ctl,
								HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);
	}

	parse_diskstats(diskstats);
}

static void
check_io_peak(DiskStatsEntry *entry, unsigned long rd_sec,
			  unsigned long wr_sec, time_t duration)
{
	float8	calc;

	if (duration <= 0)
		return;

	if (rd_sec >= entry->stats.rd_sectors)
	{
		calc = (float8) (rd_sec - entry->stats.rd_sectors) / duration;
		if (calc > entry->drs_ps_max)
			entry->drs_ps_max = calc;
	}
	if (wr_sec >= entry->stats.wr_sectors)
	{
		calc = (float8) (wr_sec - entry->stats.wr_sectors) / duration;
		if (calc > entry->dws_ps_max)
			entry->dws_ps_max = calc;
	}
}

static void
check_io_overflow(DiskStatsEntry *entry, unsigned long rd_sec,
				  unsigned long wr_sec, unsigned int rd_ticks,
				  unsigned int wr_ticks, unsigned int rq_ticks)
{
	if (entry->stats.rd_sectors > rd_sec)
		entry->overflow_drs++;
	if (entry->stats.wr_sectors > wr_sec)
		entry->overflow_dws++;
	if (entry->stats.rd_ticks > rd_ticks)
		entry->overflow_drt++;
	if (entry->stats.wr_ticks > wr_ticks)
		entry->overflow_dwt++;
	if (entry->stats.rq_ticks > rq_ticks)
		entry->overflow_dit++;
}

#define FILE_DISKSTATS					"/proc/diskstats"
#define NUM_DISKSTATS_FIELDS			14
#define NUM_DISKSTATS_PARTITION_FIELDS	7

static void
parse_diskstats(HTAB *htab)
{
	FILE			*fp;
	char			 line[256];
	char			 dev_name[128];
	unsigned int	 dev_major, dev_minor;
	unsigned int	 ios_pgr, tot_ticks, rq_ticks, wr_ticks;
	unsigned long	 rd_ios, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, wr_ios;
	unsigned long	 wr_merges, rd_sec_or_wr_ios, wr_sec;
	DiskStatsHashKey key;
	DiskStatsEntry	*entry;
	bool			 found;
	int				 i;
	time_t			 now;

	if ((fp = fopen(FILE_DISKSTATS, "r")) == NULL)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_DISKSTATS)));

	now = time(NULL);

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		/* major minor name rio rmerge rsect ruse wio wmerge wsect wuse running use aveq */
		i = sscanf(line, "%u %u %s %lu %lu %lu %lu %lu %lu %lu %u %u %u %u",
				&dev_major, &dev_minor, dev_name,
				&rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
				&wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks);

		if (i != NUM_DISKSTATS_FIELDS &&
			i != NUM_DISKSTATS_PARTITION_FIELDS)
			/* unknown entry: ignore it */
			continue;

		/* lookup the hash table entry */
		key.dev_major = dev_major;
		key.dev_minor = dev_minor;
		entry = (DiskStatsEntry *) hash_search(htab, &key, HASH_ENTER, &found);

		if (found)
		{
			/* check I/O peak and overflow if there is previous sample */
			time_t duration;

			duration = now - entry->timestamp;
			if (i == NUM_DISKSTATS_FIELDS)
			{
				check_io_peak(entry, rd_sec_or_wr_ios, wr_sec, duration);
				check_io_overflow(entry, rd_sec_or_wr_ios, wr_sec,
					rd_ticks_or_wr_sec, wr_ticks, rq_ticks);
			}
			else
			{
				check_io_peak(entry, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, duration);
				check_io_overflow(entry, rd_merges_or_rd_sec,
					rd_ticks_or_wr_sec, 0, 0, 0);
			}
		}
		else
		{
			/* initialize new entry */
			memset(&entry->stats, 0, sizeof(entry->stats));
			entry->field_num = i;
			entry->stats.dev_major = dev_major;
			entry->stats.dev_minor = dev_minor;
			strlcpy(entry->stats.dev_name, dev_name, sizeof(entry->stats.dev_name));
			entry->drs_ps_max = 0;
			entry->dws_ps_max = 0;
			entry->overflow_drs = 0;
			entry->overflow_drt = 0;
			entry->overflow_dws = 0;
			entry->overflow_dwt = 0;
			entry->overflow_dit = 0;
		}

		/* set I/O statistics */
		if (i == NUM_DISKSTATS_FIELDS)
		{
			/* device or partition */
			entry->stats.rd_ios     = rd_ios;				/* Field 1 -- # of reads completed */
			entry->stats.rd_merges  = rd_merges_or_rd_sec;	/* Field 2 -- # of reads merged */
			entry->stats.rd_sectors = rd_sec_or_wr_ios;		/* Field 3 -- # of sectors read */
			entry->stats.rd_ticks   = (unsigned int) rd_ticks_or_wr_sec;	/* Field 4 -- # of milliseconds spent reading */
			entry->stats.wr_ios     = wr_ios;				/* Field 5 -- # of writes completed */
			entry->stats.wr_merges  = wr_merges;			/* Field 6 -- # of writes merged */
			entry->stats.wr_sectors = wr_sec;				/* Field 7 -- # of sectors written */
			entry->stats.wr_ticks   = wr_ticks;				/* Field 8 -- # of milliseconds spent writing  */
			entry->stats.ios_pgr    = ios_pgr;				/* Field 9 -- # of I/Os currently in progress */
			entry->stats.tot_ticks  = tot_ticks;			/* Field 10 -- # of milliseconds spent doing I/Os */
			entry->stats.rq_ticks   = rq_ticks;				/* Field 11 -- weighted # of milliseconds spent doing I/Os */
		}
		else
		{
			/* partition without extended statistics */
			entry->stats.rd_ios     = rd_ios;				/* Field 1 -- # of reads issued */
			entry->stats.rd_sectors = rd_merges_or_rd_sec;	/* Field 2 -- # of sectors read */
			entry->stats.wr_ios     = rd_sec_or_wr_ios;		/* Field 3 -- # of writes issued */
			entry->stats.wr_sectors = rd_ticks_or_wr_sec;	/* Field 4 -- # of sectors written */
		}
		entry->timestamp = now;
	}

	if (ferror(fp) && errno != EINTR)
	{
		fclose(fp);
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": ", FILE_DISKSTATS)));
	}

	fclose(fp);
}

#define NUM_ACTIVITY_COLS		5

/*
 * statsinfo_activity - accumulate sampled counters.
 */
Datum
statsinfo_activity(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	HeapTuple	tuple;
	Datum		values[NUM_ACTIVITY_COLS];
	bool		nulls[NUM_ACTIVITY_COLS];
	int			i;

	must_be_superuser();

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	Assert(tupdesc->natts == lengthof(values));

	if (activity.samples > 0)
	{

		memset(nulls, 0, sizeof(nulls));

		i = 0;
		values[i++] = Int32GetDatum(activity.idle);
		values[i++] = Int32GetDatum(activity.idle_in_xact);
		values[i++] = Int32GetDatum(activity.waiting);
		values[i++] = Int32GetDatum(activity.running);
		values[i++] = Int32GetDatum(activity.max_backends);

		Assert(i == lengthof(values));

		/* reset activity statistics */
		memset(&activity, 0, sizeof(Activity));
	}
	else
	{
		for (i = 0; i < lengthof(nulls); i++)
			nulls[i] = true;
	}

	tuple = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

#define NUM_LONG_TRANSACTION_COLS		5

/*
 * statsinfo_long_xact - get long transaction information
 */
Datum
statsinfo_long_xact(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate	   *tupstore;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	HASH_SEQ_STATUS		hash_seq;
	LongXactEntry	   *entry;
	Datum				values[NUM_LONG_TRANSACTION_COLS];
	bool				nulls[NUM_LONG_TRANSACTION_COLS];
	int					i;

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

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == lengthof(values));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	if (long_xacts)
	{
		hash_seq_init(&hash_seq, long_xacts);
		while ((entry = hash_seq_search(&hash_seq)) != NULL)
		{
			memset(values, 0, sizeof(values));
			memset(nulls, 0, sizeof(nulls));

			i = 0;
			if (entry->client[0])
				values[i++] = CStringGetTextDatum(entry->client);
			else
				nulls[i++] = true;
			if (entry->pid != 0)
			{
				char	*clipped_activity;

				values[i++] = Int32GetDatum(entry->pid);
				values[i++] = TimestampTzGetDatum(entry->start);
				values[i++] = Float8GetDatum(entry->duration);

				clipped_activity = pgstat_clip_activity(entry->query);
				values[i++] = CStringGetTextDatum(clipped_activity);
				pfree(clipped_activity);
			}
			else
			{
				nulls[i++] = true;
				nulls[i++] = true;
				nulls[i++] = true;
				nulls[i++] = true;
			}
			Assert(i == lengthof(values));
			tuplestore_putvalues(tupstore, tupdesc, values, nulls);

			/* remove entry from hashtable */
			hash_search(long_xacts, &entry->key, HASH_REMOVE, NULL);
		}
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

#define WAIT_SAMPLING_PROFILE_COLS		7

/*
 * statsinfo_wait_sampling_profile - get wait sampling information
 */
Datum
statsinfo_wait_sampling_profile(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate	   *tupstore;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	HASH_SEQ_STATUS		hash_seq;
	wait_samplingEntry		   *entry;
	Datum				values[WAIT_SAMPLING_PROFILE_COLS];
	bool				nulls[WAIT_SAMPLING_PROFILE_COLS];
	int					i;
	const char			*event_type,
						*event;

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

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == lengthof(values));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	if (wait_sampling_hash)
	{
		hash_seq_init(&hash_seq, wait_sampling_hash);
		while ((entry = hash_seq_search(&hash_seq)) != NULL)
		{
			memset(values, 0, sizeof(values));
			memset(nulls, 0, sizeof(nulls));

			event_type = pgstat_get_wait_event_type(entry->key.wait_event_info);
			event = pgstat_get_wait_event(entry->key.wait_event_info);

			i = 0;

			values[i++] = Int32GetDatum(entry->key.dbid);
			values[i++] = Int32GetDatum(entry->key.userid);

			if (wait_sampling_queries)
				values[i++] = Int64GetDatumFast((int64)entry->key.queryid);
			else
				values[i++] = (Datum) 0;

			if (entry->key.backend_type)
				values[i++] = CStringGetTextDatum(GetBackendTypeDesc(entry->key.backend_type));
			else
				nulls[i++] = true;

			if (event_type)
				values[i++] = PointerGetDatum(cstring_to_text(event_type));
			else
				nulls[i++] = true;

			if (event)
				values[i++] = PointerGetDatum(cstring_to_text(event));
			else
				nulls[i++] = true;

			values[i++] = Int64GetDatumFast((int64)entry->counters.count);

			Assert(i == lengthof(values));
			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * statsinfo_snapshot(comment) - take a manual snapshot asynchronously.
 */
Datum
statsinfo_snapshot(PG_FUNCTION_ARGS)
{
	char *comment;

	if (PG_NARGS() < 1 || PG_ARGISNULL(0))
		comment = NULL;
	else
		comment = text_to_cstring(PG_GETARG_TEXT_PP(0));

	ereport(LOG,
		(errmsg(LOGMSG_SNAPSHOT),
		(comment ? errdetail("%s", comment) : 0)));

	PG_RETURN_VOID();
}

/*
 * statsinfo_maintenance(repository_keep_period) - perform maintenance asynchronously.
 */
Datum
statsinfo_maintenance(PG_FUNCTION_ARGS)
{
	TimestampTz	repository_keep_period = PG_GETARG_TIMESTAMP(0);

	ereport(LOG,
		(errmsg(LOGMSG_MAINTENANCE),
		(errdetail("%d", (int) timestamptz_to_time_t(repository_keep_period)))));

	PG_RETURN_VOID();
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	static char		default_repository_server[64];

	snprintf(default_repository_server, lengthof(default_repository_server),
		"dbname=postgres port=%s", GetConfigOption("port", false));

	/*
	 * Define (or redefine) custom GUC variables.
	 */
	DefineCustomEnumVariable(GUC_PREFIX ".syslog_min_messages",
							 "Sets the message levels that are system-logged.",
							 NULL,
							 &syslog_min_messages,
							 DEFAULT_SYSLOG_LEVEL,
							 elevel_options,
							 PGC_SIGHUP,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable(GUC_PREFIX ".textlog_min_messages",
							 "Sets the message levels that are text-logged.",
							 NULL,
							 &textlog_min_messages,
							 DEFAULT_TEXTLOG_LEVEL,
							 elevel_options,
							 PGC_SIGHUP,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomEnumVariable(GUC_PREFIX ".repolog_min_messages",
							 "Sets the message levels that are repository-logged.",
							 NULL,
							 &repolog_min_messages,
							 DEFAULT_REPOLOG_LEVEL,
							 elevel_options,
							 PGC_SIGHUP,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_filename",
							   "Sets the latest file name for textlog.",
							   NULL,
							   &textlog_filename,
							   "pg_statsinfo.log",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   check_textlog_filename,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_line_prefix",
							   "Controls information prefixed to each textlog line.",
							   "If blank, no prefix is used.",
							   &textlog_line_prefix,
							   "%t %p ",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".syslog_line_prefix",
							   "Controls information prefixed to each syslog line.",
							   "If blank, no prefix is used.",
							   &syslog_line_prefix,
							   "%t %p ",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".textlog_permission",
							"Sets the file permission.",
							NULL,
							&textlog_permission,
							0600,
							0000,
							0666,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".excluded_dbnames",
							   "Selects which dbnames are excluded by pg_statsinfo.",
							   NULL,
							   &excluded_dbnames,
							   "template0, template1",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".excluded_schemas",
							   "Selects which schemas are excluded by pg_statsinfo.",
							   NULL,
							   &excluded_schemas,
							   "pg_catalog,pg_toast,information_schema",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".sampling_interval",
							"Sets the sampling interval.",
							NULL,
							&sampling_interval,
							DEFAULT_SAMPLING_INTERVAL,
							1,
							INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_S,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".wait_sampling_interval",
							"Sets the wait sampling interval.",
							NULL,
							&wait_sampling_interval,
							DEFAULT_WAIT_SAMPLING_INTERVAL,
							1,
							INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_MS,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".snapshot_interval",
							"Sets the snapshot interval.",
							NULL,
							&snapshot_interval,
							DEFAULT_SNAPSHOT_INTERVAL,
							1,
							INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_S,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".repository_server",
							   "Connection string for repository database.",
							   NULL,
							   &repository_server,
							   default_repository_server,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".adjust_log_level",
							 "Enable the log level adjustment.",
							 NULL,
							 &adjust_log_level,
							 false,
							 PGC_SIGHUP,
							 GUC_SUPERUSER_ONLY,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_info",
							   "Selects SQL-STATE that want to change log level to 'INFO'.",
							   NULL,
							   &adjust_log_info,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_notice",
							   "Selects SQL-STATE that want to change log level to 'NOTICE'.",
							   NULL,
							   &adjust_log_notice,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_warning",
							   "Selects SQL-STATE that want to change log level to 'WARNING'.",
							   NULL,
							   &adjust_log_warning,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_error",
							   "Selects SQL-STATE that want to change log level to 'ERROR'.",
							   NULL,
							   &adjust_log_error,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_log",
							   "Selects SQL-STATE that want to change log level to 'LOG'.",
							   NULL,
							   &adjust_log_log,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_fatal",
							   "Selects SQL-STATE that want to change log level to 'FATAL'.",
							   NULL,
							   &adjust_log_fatal,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_nologging_users",
							   "Sets dbusers that doesn't logging to textlog.",
							   NULL,
							   &textlog_nologging_users,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".repolog_nologging_users",
							   "Sets dbusers that doesn't store the log in repository.",
							   NULL,
							   &repolog_nologging_users,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".enable_maintenance",
							   "Sets the maintenance mode.",
							   NULL,
							   &enable_maintenance,
							   DEFAULT_ENABLE_MAINTENANCE,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   check_enable_maintenance,
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".maintenance_time",
							   "Sets the maintenance time.",
							   NULL,
							   &maintenance_time,
							   DEFAULT_MAINTENANCE_TIME,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
							   check_maintenance_time,
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".repository_keepday",
							"Sets the retention period of repository.",
							NULL,
							&repository_keepday,
							DEFAULT_REPOSITORY_KEEPDAY,
							1,
							3650,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".repolog_keepday",
							"Sets the retention period of log which is in repository.",
							NULL,
							&repolog_keepday,
							DEFAULT_REPOLOG_KEEPDAY,
							1,
							3650,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".log_maintenance_command",
							   "Sets the shell command that will be called to the log maintenance.",
							   NULL,
							   &log_maintenance_command,
							   DEFAULT_LOG_MAINTENANCE_COMMAND,
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".long_lock_threshold",
							"Sets the threshold of lock wait time.",
							NULL,
							&long_lock_threshold,
							DEFAULT_LONG_LOCK_THRESHOLD,
							0,
							INT_MAX,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".stat_statements_max",
							"Sets the max collection size from pg_stat_statements.",
							NULL,
							&stat_statements_max,
							DEFAULT_STAT_STATEMENTS_MAX,
							0,
							INT_MAX,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".stat_statements_exclude_users",
							   "Sets dbusers that doesn't collect statistics of statement from pg_stat_statements.",
							   NULL,
							   &stat_statements_exclude_users,
							   "",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".controlfile_fsync_interval",
							"Sets the fsync interval of the control file.",
							NULL,
							&controlfile_fsync_interval,
							DEFAULT_CONTROLFILE_FSYNC_INTERVAL,
							-1,
							INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_S,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".repolog_buffer",
							"Sets the number of log to buffer which use to store log into repository.",
							NULL,
							&repolog_buffer,
							DEFAULT_REPOLOG_BUFFER,
							1,
							INT_MAX,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".repolog_interval",
							"Sets the store interval to store log in repository.",
							NULL,
							&repolog_interval,
							DEFAULT_REPOLOG_INTERVAL,
							0,
							60,
							PGC_SIGHUP,
							GUC_UNIT_S,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".long_transaction_max",
							"Sets the max collection size of long transaction.",
							NULL,
							&long_transaction_max,
							DEFAULT_LONG_TRANSACTION_MAX,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".enable_alert",
							"Enable the alert function.",
							NULL,
							&enable_alert,
							false,
							PGC_SIGHUP,
							GUC_SUPERUSER_ONLY,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".target_server",
							"Connection string for monitored database.",
							NULL,
							&target_server,
							"",
							PGC_SIGHUP,
							GUC_SUPERUSER_ONLY,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".wait_sampling_queries",
							"Whether wait sampling should be collected per query or not.",
							NULL,
							&wait_sampling_queries,
							DEFAULT_WAIT_SAMPLING_QUERIES,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".wait_sampling_max",
							"Set maximum number of wait sampling records.",
							NULL,
							&wait_sampling_max,
							DEFAULT_WAIT_SAMPLING_MAX,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".wait_sampling_save",
							"Save statsinfo wait sampling statistics across server shutdowns.",
							NULL,
							&wait_sampling_save,
							true,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".collect_column",
							"Enable collect column information.",
							NULL,
							&collect_column,
							true,
							PGC_SIGHUP,
							GUC_SUPERUSER_ONLY,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".collect_index",
							"Enable collect index information.",
							NULL,
							&collect_index,
							true,
							PGC_SIGHUP,
							GUC_SUPERUSER_ONLY,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable(GUC_PREFIX ".rusage_max",
							"Sets the maximum number of statements for rusage info..",
							NULL,
							&rusage_max,
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
							&rusage_track,
							STATSINFO_RUSAGE_TRACK_TOP,
							rusage_track_options,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

    DefineCustomBoolVariable(GUC_PREFIX ".rusage_track_planning",
							"Enable tracking rusage info on planning phase.",
							NULL,
							&rusage_track_planning,
							false,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

    DefineCustomBoolVariable(GUC_PREFIX ".rusage_track_utility",
							"Enable tracking rusage info for Utility Statements.",
							NULL,
							&rusage_track_utility,
							false,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".rusage_save",
							"Save statsinfo rusage statistics across server shutdowns.",
							NULL,
							&rusage_save,
							true,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	MarkGUCPrefixReserved("pg_statsinfo");

	if (IsUnderPostmaster)
		return;

	/*
	 * Check supported parameters combinations.
	 */
	if (!verify_log_filename(Log_filename))
		ereport(FATAL,
			(errmsg(LOG_PREFIX "unsupported log_filename: %s",
					Log_filename),
			 errhint("must have %%Y, %%m, %%d, %%H, %%M, and %%S in this order")));

	/*
	 * Adjust must-set parameters.
	 */
	SetConfigOption("logging_collector", "on", PGC_POSTMASTER, PGC_S_OVERRIDE);
	adjust_log_destination(PGC_POSTMASTER, PGC_S_OVERRIDE);

#ifdef NOT_USED
	/* XXX: should set unmodifiable parameter? */
	SetConfigOption("lc_messages", GetConfigOption("lc_messages", false), PGC_POSTMASTER, PGC_S_OVERRIDE);
#endif

#ifdef ADJUST_NON_CRITICAL_SETTINGS
	if (!pgstat_track_activities)
		SetConfigOption("track_activities", "on",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
	if (!pgstat_track_counts)
		SetConfigOption("track_counts", "on",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
	if (!log_checkpoints)
		SetConfigOption("log_checkpoints", "on",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
	if (Log_autovacuum_min_duration < 0)
		SetConfigOption("log_autovacuum_min_duration", "0",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
	if (!pgstat_track_functions)
		SetConfigOption("track_functions", "all",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
#endif /* ADJUST_NON_CRITICAL_SETTINGS */

	/* Install emit_log_hook */
	TAKE_HOOK(emit_log, pg_statsinfo_emit_log_hook);

	/* Request additional shared resources */
	TAKE_HOOK(shmem_request, pg_statsinfo_shmem_request_hook);

	/* Install shmem_startup_hook */
	TAKE_HOOK(shmem_startup, pg_statsinfo_shmem_startup_hook);

	/* Install xact_last_activity */
	init_last_xact_activity();

	/*
	 * spawn pg_statsinfo launcher process if the first call
	 */
	if (!IsUnderPostmaster)
		StartStatsinfoLauncher();
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall xact_last_activity */
	fini_last_xact_activity();
	/* Uninstall wait_sampling */
	fini_wait_sampling();
	/* Uninstall emit_log_hook */
	RESTORE_HOOK(emit_log);
	/* Uninstall shmem_startup_hook */
	RESTORE_HOOK(shmem_startup);
	/* Uninstall shmem_request_hook */
	RESTORE_HOOK(shmem_request);
}

/*
 * statsinfo_start - start statsinfo background process.
 */
Datum
statsinfo_start(PG_FUNCTION_ARGS)
{
	int32	timeout;
	char	pid_file[MAXPGPATH];
	char	buffer[64];
	pid_t	pid;
	int		cnt;
	int		save_client_min_messages = client_min_messages;
	int		save_log_min_messages = log_min_messages;

	/*
	 * adjust elevel to LOG so that message be output to client console only,
	 * not write to server log.
	 */
	client_min_messages = LOG;
	log_min_messages = FATAL;

	must_be_superuser();

	if (PG_ARGISNULL(0))
		elog(ERROR, "argument must not be NULL");

	timeout = PG_GETARG_INT32(0);
	if (timeout < START_WAIT_MIN || timeout > START_WAIT_MAX)
		elog(ERROR, "%d is outside the valid range for parameter (%d .. %d)",
			timeout, START_WAIT_MIN, START_WAIT_MAX);

	/* pg_statsinfo library must been preload as shared library */
	if (!is_shared_preload("pg_statsinfo"))
		elog(ERROR, "pg_statsinfo is not preloaded as shared library");

	Assert(sil_state && sil_state->pid != INVALID_PID);

	sprintf(buffer, "%s.pid", GetConfigOption("port", false));
	join_path_components(pid_file, TEMP_DIR, buffer);

	if ((pid = get_statsinfo_pid(pid_file)) != 0)	/* pid file exists */
	{
		if (kill(pid, 0) == 0)	/* process is alive */
		{
			elog(WARNING, "pg_statsinfod (PID %d) might be running", pid);
			goto done;
		}

		/* remove PID file */
		if (unlink(pid_file) != 0)
		{
			int save_errno = errno;
			elog(ERROR, "could not remove file \"%s\": %s",
				pid_file, strerror(save_errno));
		}
	}

	/* lookup the pg_statsinfo launcher state */
	lookup_sil_state();

	/* send signal that instruct start the statsinfo background process */
	if (kill(sil_state->pid, SIGUSR2) != 0)
		elog(ERROR, "could not send start signal (PID %d): %m", sil_state->pid);

	elog(LOG, "waiting for pg_statsinfod to start");

	pid = get_statsinfo_pid(pid_file);
	for (cnt = 0; pid == 0 && cnt < timeout; cnt++)
	{
		pg_usleep(1000000);		/* 1 sec */
		pid = get_statsinfo_pid(pid_file);
	}

	if (pid == 0)	/* pid file still not exists */
		elog(WARNING, "timed out waiting for pg_statsinfod startup");
	else
		elog(LOG, "pg_statsinfod started");

done:
	client_min_messages = save_client_min_messages;
	log_min_messages = save_log_min_messages;
	PG_RETURN_VOID();
}

/*
 * statsinfo_stop - stop statsinfo background process.
 */
Datum
statsinfo_stop(PG_FUNCTION_ARGS)
{
	int32	timeout;
	char	pid_file[MAXPGPATH];
	char	buffer[64];
	pid_t	pid;
	int		cnt;
	int		save_client_min_messages = client_min_messages;
	int		save_log_min_messages = log_min_messages;

	/*
	 * adjust elevel to LOG so that message be output to client console only,
	 * not write to server log.
	 */
	client_min_messages = LOG;
	log_min_messages = FATAL;

	must_be_superuser();

	if (PG_ARGISNULL(0))
		elog(ERROR, "argument must not be NULL");

	timeout = PG_GETARG_INT32(0);
	if (timeout < STOP_WAIT_MIN || timeout > STOP_WAIT_MAX)
		elog(ERROR, "%d is outside the valid range for parameter (%d .. %d)",
			timeout, STOP_WAIT_MIN, STOP_WAIT_MAX);

	/* pg_statsinfo library must been preload as shared library */
	if (!is_shared_preload("pg_statsinfo"))
		elog(ERROR, "pg_statsinfo is not preloaded as shared library");

	Assert(sil_state && sil_state->pid != INVALID_PID);

	sprintf(buffer, "%s.pid", GetConfigOption("port", false));
	join_path_components(pid_file, TEMP_DIR, buffer);

	if ((pid = get_statsinfo_pid(pid_file)) == 0)	/* no pid file */
	{
		elog(WARNING, "PID file \"%s\" does not exist; is pg_statsinfod running?",
			pid_file);
		goto done;
	}

	if (kill(pid, 0) != 0)	/* process is not alive */
	{
		elog(WARNING, "pg_statsinfod is not running (PID %d)", pid);
		goto done;
	}

	/* lookup the pg_statsinfo launcher state */
	lookup_sil_state();

	/* send signal that instruct stop the statsinfo background process */
	if (kill(sil_state->pid, SIGUSR1) != 0)
		elog(ERROR, "could not send stop signal (PID %d): %m", sil_state->pid);

	elog(LOG, "waiting for pg_statsinfod to shut down");

	pid = get_statsinfo_pid(pid_file);
	for (cnt = 0; pid != 0 && cnt < timeout; cnt++)
	{
		pg_usleep(1000000);		/* 1 sec */
		pid = get_statsinfo_pid(pid_file);
	}

	if (pid != 0)	/* pid file still exists */
		elog(WARNING, "timed out waiting for pg_statsinfod shut down");
	else
		elog(LOG, "pg_statsinfod stopped");

done:
	client_min_messages = save_client_min_messages;
	log_min_messages = save_log_min_messages;
	PG_RETURN_VOID();
}

#define FILE_CPUSTAT			"/proc/stat"
#define NUM_CPUSTATS_COLS		9
#define NUM_STAT_FIELDS_MIN		6

/* not support a kernel that does not have the required fields at "/proc/stat" */
#if !LINUX_VERSION_AT_LEAST(2,5,41)
#error kernel version 2.5.41 or later is required
#endif

/*
 * statsinfo_cpustats - get cpu information
 */
Datum
statsinfo_cpustats(PG_FUNCTION_ARGS)
{
	HeapTupleHeader	cpustats = PG_GETARG_HEAPTUPLEHEADER(0);
	int64			prev_cpu_user;
	int64			prev_cpu_system;
	int64			prev_cpu_idle;
	int64			prev_cpu_iowait;
	bool			isnull;

	/* previous cpustats */
	prev_cpu_user = DatumGetInt64(
		GetAttributeByNum(cpustats, 1, &isnull)); /* cpu_user */
	prev_cpu_system = DatumGetInt64(
		GetAttributeByNum(cpustats, 2, &isnull)); /* cpu_system */
	prev_cpu_idle = DatumGetInt64(
		GetAttributeByNum(cpustats, 3, &isnull)); /* cpu_idle */
	prev_cpu_iowait = DatumGetInt64(
		GetAttributeByNum(cpustats, 4, &isnull)); /* cpu_iowait */

	PG_RETURN_DATUM(get_cpustats(fcinfo,
		prev_cpu_user, prev_cpu_system, prev_cpu_idle, prev_cpu_iowait));
}

/*
 * statsinfo_cpustats - get cpu information
 * (remains of the old interface)
 */
Datum
statsinfo_cpustats_noarg(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(get_cpustats(fcinfo, 0, 0, 0, 0));
}

static Datum
get_cpustats(FunctionCallInfo fcinfo,
			 int64 prev_cpu_user,
			 int64 prev_cpu_system,
			 int64 prev_cpu_idle,
			 int64 prev_cpu_iowait)
{
	TupleDesc		 tupdesc;
	int64			 cpu_user;
	int64			 cpu_system;
	int64			 cpu_idle;
	int64			 cpu_iowait;
	List			*records = NIL;
	List			*fields = NIL;
	HeapTuple		 tuple;
	Datum			 values[NUM_CPUSTATS_COLS];
	bool			 nulls[NUM_CPUSTATS_COLS];
	char			*record;

	must_be_superuser();

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	Assert(tupdesc->natts == lengthof(values));

	/* extract cpu information */
	if (exec_grep(FILE_CPUSTAT, "^cpu\\s+", &records) <= 0)
		ereport(ERROR,
			(errcode(ERRCODE_DATA_EXCEPTION),
			 errmsg("unexpected file format: \"%s\"", FILE_CPUSTAT)));

	record = (char *) list_nth(records, 0);
	if (exec_split(record, "\\s+", &fields) < NUM_STAT_FIELDS_MIN)
		ereport(ERROR,
			(errcode(ERRCODE_DATA_EXCEPTION),
			 errmsg("unexpected file format: \"%s\"", FILE_CPUSTAT),
			 errdetail("number of fields is not corresponding")));

	memset(nulls, 0, sizeof(nulls));
	memset(values, 0, sizeof(values));

	/* cpu_id */
	values[0] = CStringGetTextDatum((char *) list_nth(fields, 0));

	/* cpu_user */
	parse_int64(list_nth(fields, 1), &cpu_user);
	values[1] = Int64GetDatum(cpu_user);

	/* cpu_system */
	parse_int64(list_nth(fields, 3), &cpu_system);
	values[2] = Int64GetDatum(cpu_system);

	/* cpu_idle */
	parse_int64(list_nth(fields, 4), &cpu_idle);
	values[3] = Int64GetDatum(cpu_idle);

	/* cpu_iowait */
	parse_int64(list_nth(fields, 5), &cpu_iowait);
	values[4] = Int64GetDatum(cpu_iowait);

	/* set the overflow flag if value is smaller than previous value */
	if (cpu_user < prev_cpu_user)
		values[5] = Int16GetDatum(1); /* overflow_user */
	else
		values[5] = Int16GetDatum(0);
	if (cpu_system < prev_cpu_system)
		values[6] = Int16GetDatum(1); /* overflow_system */
	else
		values[6] = Int16GetDatum(0);
	if (cpu_idle < prev_cpu_idle)
		values[7] = Int16GetDatum(1); /* overflow_idle */
	else
		values[7] = Int16GetDatum(0);
	if (cpu_iowait < prev_cpu_iowait)
		values[8] = Int16GetDatum(1); /* overflow_iowait */
	else
		values[8] = Int16GetDatum(0);

	tuple = heap_form_tuple(tupdesc, values, nulls);

	return HeapTupleGetDatum(tuple);
}

#define NUM_DEVICESTATS_COLS			17
#define TYPE_DEVICE_TABLESPACES			TEXTOID
#define SQL_SELECT_TABLESPACES "\
SELECT \
	device, \
	pg_catalog.split_part(device, ':', 1), \
	pg_catalog.split_part(device, ':', 2), \
	statsinfo.array_agg(name) \
FROM \
	statsinfo.tablespaces \
WHERE \
	device IS NOT NULL \
GROUP BY \
	device"

/*
 * statsinfo_devicestats - get device information
 */
Datum
statsinfo_devicestats(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc		 tupdesc;
	Tuplestorestate	*tupstore;
	MemoryContext	 per_query_ctx;
	MemoryContext	 oldcontext;
	SPITupleTable	*tuptable;
	Datum			 values[NUM_DEVICESTATS_COLS];
	bool			 nulls[NUM_DEVICESTATS_COLS];
	int				 row;
	bool			 isnull;

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

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == lengthof(values));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	/* take a sample of diskstats */
	sample_diskstats();

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI connect failure");

	execute(SPI_OK_SELECT, SQL_SELECT_TABLESPACES);
	tuptable = SPI_tuptable;

	for (row = 0; row < SPI_processed; row++)
	{
		HeapTuple tup = tuptable->vals[row];
		TupleDesc desc = tuptable->tupdesc;
		char *device = SPI_getvalue(tup, desc, 1);
		char *dev_major = SPI_getvalue(tup, desc, 2);
		char *dev_minor = SPI_getvalue(tup, desc, 3);
		DiskStatsHashKey key;
		DiskStatsEntry *entry;

		memset(nulls, 0, sizeof(nulls));
		memset(values, 0, sizeof(values));
		values[0] = CStringGetTextDatum(dev_major);			/* device_major */
		values[1] = CStringGetTextDatum(dev_minor);			/* device_minor */
		values[16] = SPI_getbinval(tup, desc, 4, &isnull);	/* device_tblspaces */

		key.dev_major = atoi(dev_major);
		key.dev_minor = atoi(dev_minor);
		entry = hash_search(diskstats, &key, HASH_FIND, NULL);

		if (!entry)
		{
			ereport(DEBUG2,
				(errmsg("device information of \"%s\" used by tablespace \"%s\" does not exist in \"%s\"",
					device, SPI_getvalue(tup, desc, 4), FILE_DISKSTATS)));

			nulls[2] = true;	/* device_name */
			nulls[3] = true;	/* device_readsector */
			nulls[4] = true;	/* device_readtime */
			nulls[5] = true;	/* device_writesector */
			nulls[6] = true;	/* device_writetime */
			nulls[7] = true;	/* device_queue */
			nulls[8] = true;	/* device_iototaltime */
			nulls[9] = true;	/* device_rsps_max */
			nulls[10] = true;	/* device_wsps_max */
			nulls[11] = true;	/* overflow_drs */
			nulls[12] = true;	/* overflow_drt */
			nulls[13] = true;	/* overflow_dws */
			nulls[14] = true;	/* overflow_dwt */
			nulls[15] = true;	/* overflow_dit */

			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
			continue;
		}

		if (entry->field_num  == NUM_DISKSTATS_FIELDS)
		{
			values[2] = CStringGetTextDatum(entry->stats.dev_name);	/* device_name */
			values[3] = Int64GetDatum(entry->stats.rd_sectors);		/* device_readsector */
			values[4] = Int64GetDatum(entry->stats.rd_ticks);		/* device_readtime */
			values[5] = Int64GetDatum(entry->stats.wr_sectors);		/* device_writesector */
			values[6] = Int64GetDatum(entry->stats.wr_ticks);		/* device_writetime */
			values[7] = Int64GetDatum(entry->stats.ios_pgr);		/* device_queue */
			values[8] = Int64GetDatum(entry->stats.rq_ticks);		/* device_iototaltime */
		}
		else if (entry->field_num == NUM_DISKSTATS_PARTITION_FIELDS)
		{
			values[2] = CStringGetTextDatum(entry->stats.dev_name);	/* device_name */
			values[3] = Int64GetDatum(entry->stats.rd_sectors);		/* device_readsector */
			nulls[4] = true;										/* device_readtime */
			values[5] = Int64GetDatum(entry->stats.wr_sectors);		/* device_writesector */
			nulls[6] = true;										/* device_writetime */
			nulls[7] = true;										/* device_queue */
			nulls[8] = true;										/* device_iototaltime */
		}
		else
			ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("unexpected file format: \"%s\"", FILE_DISKSTATS),
				 errdetail("number of fields is not corresponding")));

		values[9] = Float8GetDatum(entry->drs_ps_max);		/* device_rsps_max */
		values[10] = Float8GetDatum(entry->dws_ps_max);		/* device_wsps_max */
		values[11] = Int16GetDatum(entry->overflow_drs);	/* overflow_drs */
		values[12] = Int16GetDatum(entry->overflow_drt);	/* overflow_drt */
		values[13] = Int16GetDatum(entry->overflow_dws);	/* overflow_dws */
		values[14] = Int16GetDatum(entry->overflow_dwt);	/* overflow_dwt */
		values[15] = Int16GetDatum(entry->overflow_dit);	/* overflow_dit */

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);

		/* reset counter */
		entry->drs_ps_max = 0;
		entry->dws_ps_max = 0;
		entry->overflow_drs = 0;
		entry->overflow_drt = 0;
		entry->overflow_dws = 0;
		entry->overflow_dwt = 0;
		entry->overflow_dit = 0;
	}
		
	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
	SPI_finish();

	return (Datum) 0;
}

#define FILE_LOADAVG			"/proc/loadavg"
#define NUM_LOADAVG_COLS		3
#define NUM_LOADAVG_FIELDS_MIN	3

/*
 * statsinfo_loadavg - get loadavg information
 */
Datum
statsinfo_loadavg(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	int			fd;
	char		buffer[256];
	int			nbytes;
	float4		loadavg1;
	float4		loadavg5;
	float4		loadavg15;
	HeapTuple	tuple;
	Datum		values[NUM_LOADAVG_COLS];
	bool		nulls[NUM_LOADAVG_COLS];

	must_be_superuser();

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	Assert(tupdesc->natts == lengthof(values));

	/* extract loadavg information */
	if ((fd = open(FILE_LOADAVG, O_RDONLY)) < 0)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_LOADAVG)));

	if ((nbytes = read(fd, buffer, sizeof(buffer) - 1)) < 0)
	{
		close(fd);
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": ", FILE_LOADAVG)));
	}

	close(fd);
	buffer[nbytes] = '\0';

	if (sscanf(buffer, "%f %f %f",
			&loadavg1, &loadavg5, &loadavg15) < NUM_LOADAVG_FIELDS_MIN)
		ereport(ERROR,
			(errcode(ERRCODE_DATA_EXCEPTION),
			 errmsg("unexpected file format: \"%s\"", FILE_LOADAVG),
			 errdetail("number of fields is not corresponding")));

	memset(nulls, 0, sizeof(nulls));
	memset(values, 0, sizeof(values));

	/* loadavg1 */
	values[0] = Float4GetDatum(loadavg1);

	/* loadavg5 */
	values[1] = Float4GetDatum(loadavg5);

	/* loadavg15 */
	values[2] = Float4GetDatum(loadavg15);

	tuple = heap_form_tuple(tupdesc, values, nulls);

	return HeapTupleGetDatum(tuple);
}

#define FILE_MEMINFO		"/proc/meminfo"
#define NUM_MEMORY_COLS		5

typedef struct meminfo_table
{
	const char	*name;	/* memory type name */
	int64		*slot;	/* slot in return struct */
} meminfo_table;

static int
compare_meminfo_table(const void *a, const void *b)
{
	return strcmp(((const meminfo_table *) a)->name, ((const meminfo_table *) b)->name);
}

/*
 * statsinfo_memory - get memory information
 */
Datum
statsinfo_memory(PG_FUNCTION_ARGS)
{
	TupleDesc		 tupdesc;
	HeapTuple		 tuple;
	Datum			 values[NUM_MEMORY_COLS];
	bool			 nulls[NUM_MEMORY_COLS];
	int				 fd;
	char			 buffer[2048];
	int				 nbytes;
	int64			 main_free = 0;
	int64			 buffers = 0;
	int64			 cached = 0;
	int64			 swap_free = 0;
	int64			 swap_total = 0;
	int64			 dirty = 0;
	char 			 namebuf[16];
	char			*head;
	char			*tail;
	int				 meminfo_table_count;
	meminfo_table	 findme = { namebuf, NULL };
	meminfo_table	*found;
	meminfo_table	 meminfo_tables[] =
	{
		{"Buffers",   &buffers},
		{"Cached",    &cached},
		{"Dirty",     &dirty},		/* 2.5.41+ */
		{"MemFree",   &main_free},
		{"SwapFree",  &swap_free},
		{"SwapTotal", &swap_total}
	};

	must_be_superuser();

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	Assert(tupdesc->natts == lengthof(values));

	/* extract memory information */
	if ((fd = open(FILE_MEMINFO, O_RDONLY)) < 0)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_MEMINFO)));

	if ((nbytes = read(fd, buffer, sizeof(buffer) - 1)) < 0)
	{
		close(fd);
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": ", FILE_MEMINFO)));
	}

	close(fd);
	buffer[nbytes] = '\0';

	meminfo_table_count = sizeof(meminfo_tables) / sizeof(meminfo_table);
	head = buffer;
	for (;;)
	{
		if ((tail = strchr(head, ':')) == NULL)
			break;
		*tail = '\0';
		if (strlen(head) >= sizeof(namebuf))
		{
			head = tail + 1;
			goto nextline;
		}
		strcpy(namebuf, head);
		found = bsearch(&findme, meminfo_tables, meminfo_table_count,
						sizeof(meminfo_table), compare_meminfo_table);
		head = tail + 1;
		if (!found)
			goto nextline;
		*(found->slot) = strtoul(head, &tail, 10);

nextline:
		if ((tail = strchr(head, '\n')) == NULL)
			break;
		head = tail + 1;
	}

	memset(nulls, 0, sizeof(nulls));
	memset(values, 0, sizeof(values));

	/* memfree */
	values[0] = Int64GetDatum(main_free);

	/* buffers */
	values[1] = Int64GetDatum(buffers);

	/* cached */
	values[2] = Int64GetDatum(cached);

	/* swap */
	values[3] = Int64GetDatum(swap_total - swap_free);

	/* dirty */
	values[4] = Int64GetDatum(dirty);

	tuple = heap_form_tuple(tupdesc, values, nulls);

	return HeapTupleGetDatum(tuple);
}

#define FILE_PROFILE		"/proc/systemtap/statsinfo_prof/profile"
#define NUM_PROFILE_COLS	3
#define NUM_PROFILE_FIELDS	3

/*
 * statsinfo_profile - get profile information
 */
Datum
statsinfo_profile(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc		 tupdesc;
	Tuplestorestate	*tupstore;
	MemoryContext	 per_query_ctx;
	MemoryContext	 oldcontext;

	struct stat		 st;
	FILE			*fp = NULL;
	char			 readbuf[1024];
	List			*fields = NIL;
	Datum			 values[NUM_PROFILE_COLS];
	bool			 nulls[NUM_PROFILE_COLS];
	int64			 ival = 0;
	double			 dval = 0;
	int				 i;

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

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == lengthof(values));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	/* profile result stat check */
	if (stat(FILE_PROFILE, &st) == -1)
		PG_RETURN_VOID();

	/* profile result open */
	if ((fp = fopen(FILE_PROFILE, "r")) == NULL)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_PROFILE)));

	/* profile result line data read */
	while (fgets(readbuf, sizeof(readbuf), fp) != NULL)
	{
		/* remove line separator */
		if (readbuf[strlen(readbuf) - 1] == '\n')
			readbuf[strlen(readbuf) - 1] = '\0';

		/* line data separate to ',' */
		if (exec_split(readbuf, ",", &fields) != NUM_PROFILE_FIELDS)
		{
			fclose(fp);
			ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("unexpected file format: \"%s\"", FILE_PROFILE),
				 errdetail("number of fields is not corresponding")));
		}

		memset(nulls, 0, sizeof(nulls));
		memset(values, 0, sizeof(values));

		i = 0;
		ival = 0;
		dval = 0;
		/* processing */
		values[i++] = CStringGetTextDatum((char *) list_nth(fields, 0));

		/* execute */
		parse_int64(list_nth(fields, 1), &ival);
		values[i++] = Int64GetDatum(ival);

		/* total_exec_time */
		parse_float8(list_nth(fields, 2), &dval);
		values[i++] = Float8GetDatum(dval);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);

		list_free(fields);
	}

	if (ferror(fp) && errno != EINTR)
	{
		fclose(fp);
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": ", FILE_PROFILE)));
	}

	fclose(fp);

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	PG_RETURN_VOID();
}

/*
 * Removes all leading and trailing white spaces.
 * Note: this function modify the argument string
 */
static char*
str_trim( char *str )
{
	char *start;
	char *end;

	if (!str)
		return( str );

	start = str;
	end = str + strlen( str ) - 1;

	while ( (start <= end) && isspace(start[0]) )
		start++;

	while ( (start <= end) && isspace(end[0]) )
		end--;
	end[1] = '\0';

	return( start );
}

#define FILE_CPUINFO		"/proc/cpuinfo"
#define NUM_CPUINFO_COLS	7
#define VALUE_LEN			255

/*
 * statsinfo_cpuinfo - get cpu information
 */
Datum
statsinfo_cpuinfo(PG_FUNCTION_ARGS)
{
	TupleDesc		 tupdesc;
	HeapTuple		 tuple;

	FILE			*fp = NULL;
	char			 readbuf[1024];
	Datum			 values[NUM_CPUINFO_COLS];
	bool			 nulls[NUM_CPUINFO_COLS];

	char			 vendor_id[VALUE_LEN + 1];
	char			 model_name[VALUE_LEN + 1];
	float4			 cpu_mhz;
	int				 processors;		/* logical processors count */
	int				 siblings;			/* threads(processors) per cpu */
	int				 cores_per_socket;	/* cores per socket */
	int				 sockets;			/* sockets count */

	char			*item;
	char			*val;

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == lengthof(values));

	/* cpuinfo open */
	if ((fp = fopen(FILE_CPUINFO, "r")) == NULL)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_CPUINFO)));

	memset(nulls, 0, sizeof(nulls));
	memset(values, 0, sizeof(values));

	vendor_id[0] = '\0';
	model_name[0] = '\0';
	cpu_mhz = 0;
	processors = 0;
	siblings = 0;
	cores_per_socket = 0;
	sockets = 0;

	/* cpuinfo line data read */
	while (fgets(readbuf, sizeof(readbuf), fp) != NULL)
	{
		/* remove line separator */
		if (readbuf[strlen(readbuf) - 1] == '\n')
			readbuf[strlen(readbuf) - 1] = '\0';

		/* split item name and value */
		if ( (val = strchr(readbuf, ':')) == NULL )
			continue;
		val[0] = '\0';
		val++;
		item = str_trim( readbuf );
		val = str_trim( val );

		if (strcmp(item, "vendor_id") == 0)
		{
			if (vendor_id[0] == '\0')
				strncpy(vendor_id, val, VALUE_LEN);
		}
		else if (strcmp(item, "model name") == 0)
		{
			if (model_name[0] == '\0')
				strncpy(model_name, val, VALUE_LEN);
		}
		else if (strcmp(item, "cpu MHz") == 0)
		{
			if (cpu_mhz == 0)
				cpu_mhz = atof(val);
		}
		else if (strcmp(item, "processor") == 0)
		{
			processors++;
		}
		else if (strcmp(item,"siblings") == 0)
		{
			if (siblings == 0)
				siblings = atoi(val);
		}
		else if (strcmp(item, "cpu cores") == 0)
		{
			if (cores_per_socket == 0)
				cores_per_socket = atoi(val);
		}
		else if (strcmp(item, "physical id") == 0)
		{
			if (sockets < atoi(val))
				sockets = atoi(val);
		}
	}
	sockets++;

	fclose(fp);

	values[0] = CStringGetTextDatum( vendor_id );
	values[1] = CStringGetTextDatum( model_name );
	values[2] = Float4GetDatum( cpu_mhz );
	values[3] = Int32GetDatum( processors );
	if (cores_per_socket > 0)
		values[4] = Int32GetDatum( siblings / cores_per_socket );
	else
		values[4] = Int32GetDatum( 0 );
	values[5] = Int32GetDatum( cores_per_socket );
	values[6] = Int32GetDatum( sockets );

	tuple = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

#define FILE_MEMINFO		"/proc/meminfo"
#define NUM_MEMINFO_COLS	1

/*
 * statsinfo_meminfo - get memory information
 */
Datum
statsinfo_meminfo(PG_FUNCTION_ARGS)
{
	FILE			*fp = NULL;
	char			 readbuf[1024];
	int64			 mem_total;

	char			*item;
	char			*val;
	char			*unit;

	/* meminfo open */
	if ((fp = fopen(FILE_MEMINFO, "r")) == NULL)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", FILE_MEMINFO)));

	mem_total = 0;

	/* meminfo line data read */
	while(fgets(readbuf, sizeof(readbuf), fp) != NULL)
	{
		/* remove line separator */
		if (readbuf[strlen(readbuf) - 1] == '\n')
			readbuf[strlen(readbuf) - 1] = '\0';

		/* split item name and value */
		if ( (val = strchr(readbuf, ':')) == NULL )
			continue;
		*val = '\0';
		val++;
		item = str_trim( readbuf );
		val = str_trim( val );

		/* split value and unit string */
		if ( (unit = strchr(val, ' ')) != NULL)
		{
			*unit = '\0';
			unit++;
		}

		if (strcmp(item, "MemTotal") == 0)
			mem_total = atoll(val) * 1024;
	}
	fclose(fp);

	PG_RETURN_INT64(mem_total);
}

static bool
checked_write(int fd, const void *buf, int size)
{
	if (write(fd, buf, size) != size)
	{
		errno = errno ? errno : ENOSPC;
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("pg_statsinfo launcher failed to pass startup parameters to pg_statsinfod: %m"),
				 errdetail("pg_statsinfod might fail to start due to environmental reasons")));

		return false;
	}

	return true;
}

static bool
send_end(int fd)
{
	uint32	zero = 0;

	return checked_write(fd, &zero, sizeof(zero));
}

static bool
send_str(int fd, const char *key, const char *value)
{
	uint32	size;

	/* key */
	size = strlen(key);
	if (!checked_write(fd, &size, sizeof(size)) ||
		!checked_write(fd, key, size))
		return false;
	/* value */
	size = strlen(value);
	if (!checked_write(fd, &size, sizeof(size)) ||
		!checked_write(fd, value, size))
		return false;

	return true;
}

static bool
send_i32(int fd, const char *key, int value)
{
	char	buf[32];

	snprintf(buf, lengthof(buf), "%d", value);
	return send_str(fd, key, buf);
}

static bool
send_u32(int fd, const char *key, int value)
{
	char	buf[32];

	snprintf(buf, lengthof(buf), "%u", value);
	return send_str(fd, key, buf);
}

static bool
send_u64(int fd, const char *key, uint64 value)
{
	char	buf[32];

	snprintf(buf, lengthof(buf), UINT64_FORMAT, value);
	return send_str(fd, key, buf);
}

static bool
send_reload_params(int fd)
{
	int	 i;

	for (i = 0; i < lengthof(RELOAD_PARAM_NAMES); i++)
	{
		if (!send_str(fd, RELOAD_PARAM_NAMES[i],
				GetConfigOption(RELOAD_PARAM_NAMES[i], false)))
			return false;
	}

	return true;
}

/*
 * StartStatsinfoLauncher - start pg_statsinfo launcher process.
 */
static void
StartStatsinfoLauncher(void)
{
	BackgroundWorker	worker;

	/*
	 * setup background worker
	 */
	memset(&worker, 0, sizeof(worker));
	snprintf(worker.bgw_name, BGW_MAXLEN, "pg_statsinfo launcher");
	snprintf(worker.bgw_type, BGW_MAXLEN, "pg_statsinfo launcher");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_statsinfo");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "StatsinfoLauncherMain");
	worker.bgw_main_arg = (Datum) NULL;
	worker.bgw_notify_pid = 0;

	memset(&worker.bgw_extra, 0, BGW_EXTRALEN);

	RegisterBackgroundWorker(&worker);

	return;
}

/*
 * StatsinfoLauncherMain - Main entry point for pg_statsinfo launcher process.
 */
void
StatsinfoLauncherMain(Datum main_arg)
{
	bool	found;

	/* Establish signal handlers before unblocking signals */
	pqsignal(SIGUSR1, sil_sigusr1_handler);
	pqsignal(SIGUSR2, sil_sigusr2_handler);
	pqsignal(SIGCHLD, sil_sigchld_handler);
	pqsignal(SIGHUP, sil_sighup_handler);

	/* setup the pg_statsinfo launcher state */
	LWLockAcquire(sil_state->lockid, LW_EXCLUSIVE);
	sil_state = ShmemInitStruct("pg_statsinfo",
								silShmemSize(),
								&found);
	Assert(found);
	sil_state->pid = MyProcPid;
	LWLockRelease(sil_state->lockid);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* main loop */
	StatsinfoLauncherMainLoop();
}

#define LAUNCH_RETRY_PERIOD		300	/* sec */
#define LAUNCH_RETRY_MAX		5

/*
 * StatsinfoLauncherMainLoop - Main loop for the pg_statsinfo launcher process.
 */
static void
StatsinfoLauncherMainLoop(void)
{
	int			StatsinfoPID;
	int			StatsinfoFD;
	int			launch_retry = 0;
	pg_time_t	launch_time;
	char		cmd[MAXPGPATH];
	bool		StartAgentNeeded = true;

	ereport(LOG,
		(errmsg("pg_statsinfo launcher started")));

	/* launch a pg_statsinfod process */
	StatsinfoPID = exec_background_process(cmd, &StatsinfoFD);
	launch_time = (pg_time_t) time(NULL);

	for (;;)
	{
		/* pg_statsinfo launcher quits either when the postmaster dies */
		if (!postmaster_is_alive())
			break;

		/* If we have lost the pg_statsinfod, try to start a new one */
		if (StartAgentNeeded && StatsinfoPID == 0)
		{
			time_t now = time(NULL);

			/*
			 * relaunch new pg_statsinfod process.
			 * if the pg_statsinfod was aborted continuously,
			 * then not relaunch pg_statsinfod process.
			 */
			if (now - launch_time <= LAUNCH_RETRY_PERIOD)
			{
				if (launch_retry >= LAUNCH_RETRY_MAX)
				{
					ereport(WARNING,
						(errmsg("pg_statsinfod is aborted continuously")));
					StartAgentNeeded = false;
					continue;
				}
			}
			else
				launch_retry = 0;

			ereport(LOG,
				(errmsg("relaunch a pg_statsinfod process")));

			StatsinfoPID = exec_background_process(cmd, &StatsinfoFD);
			launch_time = (pg_time_t) time(NULL);
			launch_retry++;
		}

		/* instruct to stop pg_statsinfod process */
		if (got_SIGUSR1)
		{
			got_SIGUSR1 = false;
			StartAgentNeeded = false;

			if (StatsinfoPID != 0)
			{
				/* send signal that instruct to shut down */
				if (kill(StatsinfoPID, SIGUSR1) != 0)
					ereport(WARNING,
						(errmsg("could not send stop signal (PID %d): %m",
							StatsinfoPID)));
			}
			else	/* not running */
				ereport(WARNING,
					(errmsg("pg_statsinfod is not running")));
		}

		/* instruct to start pg_statsinfod process */
		if (got_SIGUSR2)
		{
			got_SIGUSR2 = false;
			StartAgentNeeded = true;

			if (StatsinfoPID == 0)
			{
				StatsinfoPID = exec_background_process(cmd, &StatsinfoFD);
				launch_time = (pg_time_t) time(NULL);
				launch_retry = 0;
			}
			else	/* already running */
				ereport(WARNING,
					(errmsg("another pg_statsinfod might be running")));
		}

		/* reload configuration and send to pg_statsinfod */
		if (got_SIGHUP)
		{
			got_SIGHUP = false;

			/* reload configuration file */
			ProcessConfigFile(PGC_SIGHUP);

			/* send params to pg_statsinfod */
			if (StatsinfoPID != 0)
			{
				send_reload_params(StatsinfoFD);
				send_end(StatsinfoFD);
				kill(StatsinfoPID, SIGHUP);
			}
		}

		/* pg_statsinfod process died */
		if (got_SIGCHLD)
		{
			int status;

			got_SIGCHLD = false;

			if (StatsinfoPID != 0)
			{
				/* close pipe  */
				close(StatsinfoFD);

				waitpid(StatsinfoPID, &status, WNOHANG);
				StatsinfoPID = 0;

				if (WIFEXITED(status))
				{
					/* pg_statsinfod is normally end */
					if (WEXITSTATUS(status) == STATSINFO_EXIT_SUCCESS)
					{
						StartAgentNeeded = false;
						continue;
					}

					/* pg_statsinfod is aborted with fatal error */
					if (WEXITSTATUS(status) == STATSINFO_EXIT_FAILED)
					{
						ereport(WARNING,
							(errmsg("pg_statsinfod is aborted with fatal error")));
						StartAgentNeeded = false;
						continue;
					}
				}

				/* pg_statsinfod is abnormally end */
				ereport(WARNING, (errmsg("pg_statsinfod is aborted")));
			}
		}

		pg_usleep(100000L);		/* 100ms */
	}

	/* Normal exit from the pg_statsinfo launcher is here */
	ereport(LOG,
		(errmsg("pg_statsinfo launcher shutting down")));

	proc_exit(0);
}

#ifndef SizeofHeapTupleHeader
#define SizeofHeapTupleHeader offsetof(HeapTupleHeaderData, t_bits)
#endif

/*
 * exec_background_process - Start statsinfo background process.
 */
static pid_t
exec_background_process(char cmd[], int *outStdin)
{
	char			binpath[MAXPGPATH];
	char			share_path[MAXPGPATH];
	int				fd;
	pid_t			fpid;
	pid_t			postmaster_pid = get_postmaster_pid();
	pg_time_t		log_ts;
	pg_tz		   *log_tz;
	ControlFileData	ctrl;

	log_ts = (pg_time_t) time(NULL);
	log_tz = pg_tzset(GetConfigOption("log_timezone", false));

	/* $PGHOME/bin */
	strlcpy(binpath, my_exec_path, MAXPGPATH);
	get_parent_directory(binpath);

	/* $PGHOME/share */
	get_share_path(my_exec_path, share_path);

	/*
	 * Read control file. We cannot retrieve it from "Control File" shared memory
	 * because the shared memory might not be initialized yet.
	 */
	if (!readControlFile(&ctrl, DataDir))
		elog(ERROR,  LOG_PREFIX "could not read control file: %m");

	/* Make command line. Add postmaster pid only for ps display */
	snprintf(cmd, MAXPGPATH, "%s/%s %d", binpath, PROGRAM_NAME, postmaster_pid);

	/* Execute a background process. */
	fpid = forkexec(cmd, &fd);
	if (fpid == 0 || fd < 0)
	{
		elog(WARNING, LOG_PREFIX "could not execute background process");
		return fpid;
	}

	/* send GUC variables to background process. */
	if (!send_u64(fd, "instance_id", ctrl.system_identifier) ||
		!send_i32(fd, "postmaster_pid", postmaster_pid) ||
		!send_str(fd, "port", GetConfigOption("port", false)) ||
		!send_str(fd, "server_version_num", GetConfigOption("server_version_num", false)) ||
		!send_str(fd, "server_version_string", GetConfigOption("server_version", false)) ||
		!send_str(fd, "share_path", share_path) ||
		!send_i32(fd, "server_encoding", GetDatabaseEncoding()) ||
		!send_str(fd, "data_directory", DataDir) ||
		!send_str(fd, "log_timezone", pg_localtime(&log_ts, log_tz)->tm_zone) ||
		!send_u32(fd, "page_size", ctrl.blcksz) ||
		!send_u32(fd, "xlog_seg_size", ctrl.xlog_seg_size) ||
		!send_u32(fd, "page_header_size", MAXALIGN(SizeOfPageHeaderData)) ||
		!send_u32(fd, "htup_header_size", MAXALIGN(SizeofHeapTupleHeader)) ||
		!send_u32(fd, "item_id_size", sizeof(ItemIdData)) ||
		!send_i32(fd, "sil_pid", getpid()) ||
		!send_str(fd, ":debug", _("DEBUG")) ||
		!send_str(fd, ":info", _("INFO")) ||
		!send_str(fd, ":notice", _("NOTICE")) ||
		!send_str(fd, ":log", _("LOG")) ||
		!send_str(fd, ":warning", _("WARNING")) ||
		!send_str(fd, ":error", _("ERROR")) ||
		!send_str(fd, ":fatal", _("FATAL")) ||
		!send_str(fd, ":panic", _("PANIC")) ||
		!send_str(fd, ":shutdown", _(MSG_SHUTDOWN)) ||
		!send_str(fd, ":shutdown_smart", _(MSG_SHUTDOWN_SMART)) ||
		!send_str(fd, ":shutdown_fast", _(MSG_SHUTDOWN_FAST)) ||
		!send_str(fd, ":shutdown_immediate", _(MSG_SHUTDOWN_IMMEDIATE)) ||
		!send_str(fd, ":sighup", _(MSG_SIGHUP)) ||
		!send_str(fd, ":autovacuum", _(MSG_AUTOVACUUM)) ||
		!send_str(fd, ":autoanalyze", _(MSG_AUTOANALYZE)) ||
		!send_str(fd, ":checkpoint_starting", _(MSG_CHECKPOINT_STARTING)) ||
		!send_str(fd, ":checkpoint_complete", _(MSG_CHECKPOINT_COMPLETE)) ||
		!send_str(fd, ":restartpoint_complete", _(MSG_RESTARTPOINT_COMPLETE)) ||
		!send_reload_params(fd) ||
		!send_end(fd))
	{
		/* nothing to do */
	}

	*outStdin = fd;
	return fpid;
}

/* SIGUSR1: instructs to stop the pg_statsinfod process */
static void
sil_sigusr1_handler(SIGNAL_ARGS)
{
	got_SIGUSR1 = true;
}

/* SIGUSR2: instructs to start the pg_statsinfod process */
static void
sil_sigusr2_handler(SIGNAL_ARGS)
{
	got_SIGUSR2 = true;
}

/* SIGCHLD: pg_statsinfod process died */
static void
sil_sigchld_handler(SIGNAL_ARGS)
{
	got_SIGCHLD = true;
}

/* SIGHUP: reload configuration */
static void
sil_sighup_handler(SIGNAL_ARGS)
{
	got_SIGHUP = true;
}

/*
 * check for superuser, bark if not.
 */
static void
must_be_superuser(void)
{
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("only superuser may access statsinfo functions")));
}

/*
 * statsinfo_statfs - get filesystem information
 *	OUT : SETOF oid, name, location, device, total, avail
 */
Datum
statsinfo_tablespaces(PG_FUNCTION_ARGS)
{
#define TABLESPACES_COLS	7
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate	   *tupstore;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	TableScanDesc		scan;
	HeapTuple			tuple;
	Relation			relation;
	Datum				values[TABLESPACES_COLS];
	bool				nulls[TABLESPACES_COLS];
	int					i;
	ssize_t				len;
	char			   *path;
	char				pg_xlog[MAXPGPATH];
	char				location[MAXPGPATH];

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

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	Assert(tupdesc->natts == TABLESPACES_COLS);

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	relation = table_open(TableSpaceRelationId, AccessShareLock);
	scan = table_beginscan_catalog(relation, 0, NULL);

	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Form_pg_tablespace form = (Form_pg_tablespace) GETSTRUCT(tuple);
		Datum			datum;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		i = 0;

		/* oid */
		values[i++] = ObjectIdGetDatum(form->oid);

		/* name */
		values[i++] = CStringGetTextDatum(NameStr(form->spcname));

		/* location */
		if (form->oid == DEFAULTTABLESPACE_OID ||
			form->oid == GLOBALTABLESPACE_OID)
			datum = CStringGetTextDatum(DataDir);
		else
		{
			datum = DirectFunctionCall1(pg_tablespace_location,
										ObjectIdGetDatum(form->oid));
		}
		values[i++] = datum;

		/* device */
		i += get_devinfo(TextDatumGetCString(datum), values + i, nulls + i);

		/* spcoptions */
		values[i] = fastgetattr(tuple, Anum_pg_tablespace_spcoptions,
								  RelationGetDescr(relation), &nulls[i]);
		i++;
		Assert(i == TABLESPACES_COLS);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	heap_endscan(scan);

	table_close(relation, AccessShareLock);

	/* append pg_xlog if symlink */
	join_path_components(pg_xlog, DataDir, "pg_wal");

	if ((len = readlink(pg_xlog, location, lengthof(location))) > 0)
	{
		location[len] = '\0';
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		i = 0;

		nulls[i++] = true;
		values[i++] = CStringGetTextDatum("<WAL directory>");
		values[i++] = CStringGetTextDatum(location);
		i += get_devinfo(location, values + i, nulls + i);
		nulls[i++] = true;

		Assert(i == TABLESPACES_COLS);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* archive_command */
	if ((path = get_archive_path()) != NULL)
	{
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		i = 0;

		nulls[i++] = true;
		values[i++] = CStringGetTextDatum("<WAL archive directory>");
		values[i++] = CStringGetTextDatum(path);
		i += get_devinfo(path, values + i, nulls + i);
		nulls[i++] = true;

		Assert(i == TABLESPACES_COLS);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

static int
get_devinfo(const char *path, Datum values[], bool nulls[])
{
	int		i = 0;
	char	devname[32];
	int64	total;
	int64	avail;

#ifndef WIN32
	struct stat		st;

	if (stat(path, &st) == 0)
		snprintf(devname, lengthof(devname), "%d:%d", major(st.st_dev), minor(st.st_dev));
	else
		devname[0] = '\0';
#else
	snprintf(devname, lengthof(devname), "%c:\\", path[0]);
#endif

	if (devname[0])
		values[i++] = CStringGetTextDatum(devname);
	else
		nulls[i++] = true;

	if (get_diskspace(path, &total, &avail))
	{
		values[i++] = Int64GetDatum(avail);
		values[i++] = Int64GetDatum(total);
	}
	else
	{
		nulls[i++] = true;
		nulls[i++] = true;
	}

	return i;
}

static char *
get_archive_path(void)
{
	const char *archive_command = GetConfigOption("archive_command", false);

	if (archive_command && archive_command[0])
	{
		char *command = pstrdup(archive_command);
		char *begin;
		char *end;
		char *fname;

		/* example: 'cp "%p" /path/to/arclog/"%f"' */
		for (begin = command; *begin;)
		{
			begin = begin + strspn(begin, " \n\r\t\v");
			end = begin + strcspn(begin, " \n\r\t\v");
			*end = '\0';

			if ((fname = strstr(begin, "%f")) != NULL)
			{
				while (strchr(" \n\r\t\v\"'", *begin))
					begin++;
				fname--;
				while (fname > begin && strchr(" \n\r\t\v\"'/", fname[-1]))
					fname--;
				*fname = '\0';

				if (is_absolute_path(begin))
					return begin;
				break;
			}

			begin = end + 1;
		}

		pfree(command);
	}

	return NULL;
}

/*
 * Remove 'stderr' and add 'csvlog' to log_destination.
 */
static void
adjust_log_destination(GucContext context, GucSource source)
{
	char		   *rawstring;
	List		   *elemlist;
	StringInfoData	buf;

	/* always need csvlog */
	initStringInfo(&buf);
	appendStringInfoString(&buf, "csvlog");

	/* Need a modifiable copy of string */
	rawstring = pstrdup(GetConfigOption("log_destination", false));

	/* Parse string into list of identifiers */
	if (SplitIdentifierString(rawstring, ',', &elemlist))
	{
		ListCell	   *l;

		foreach(l, elemlist)
		{
			char	   *tok = (char *) lfirst(l);

			if (pg_strcasecmp(tok, "stderr") == 0 ||
				pg_strcasecmp(tok, "csvlog") == 0)
				continue;

			appendStringInfoChar(&buf, ',');
			appendStringInfoString(&buf, tok);
		}

		list_free(elemlist);
	}

	pfree(rawstring);

	SetConfigOption("log_destination", buf.data, context, source);
	pfree(buf.data);
}

static pid_t
get_postmaster_pid(void)
{
#ifndef WIN32
	return PostmasterPid;
#else
	/*
	 * PostmasterPid is not available on Windows because the variable is not
	 * dllexport'ed. Instead, use getpid if I am the postmaster, or getppid
	 * if I am a backend.
	 */
	if (!IsUnderPostmaster)
		return getpid();	/* I am postmaster */
	else
		return getppid();	/* my parent must be postmaster */
#endif
}

/*
 * check filename contains %Y, %m, %d, %H, %M, and %S in this order.
 */
static bool
verify_log_filename(const char *filename)
{
	const char	items[] = { 'Y', 'm', 'd', 'H', 'M', 'S' };
	size_t		i = 0;

	while (i < lengthof(items))
	{
		const char *percent = strchr(filename, '%');

		if (percent == NULL)
			return false;

		if (percent[1] == '%')
		{
			filename = percent + 2;
		}
		else if (percent[1] == items[i])
		{
			filename = percent + 2;
			i++;
		}
		else
			return false;	/* fail */
	}

	return true;	/* ok */
}

/* forbid empty filename and reserved characters */
static bool
check_textlog_filename(char **newval, void **extra, GucSource source)
{
	if (!*newval[0])
	{
		GUC_check_errdetail(GUC_PREFIX ".textlog_filename must not be emtpy");
		return false;
	}

	if (strpbrk(*newval, "/\\?*:|\"<>"))
	{
		GUC_check_errdetail(GUC_PREFIX ".textlog_filename must not contain reserved characters: %s",
			*newval);
		return false;
	}
	return true;
}

/* forbid unrecognized keyword for maintenance mode */
static bool
check_enable_maintenance(char **newval, void **extra, GucSource source)
{
	char		*rawstring;
	List		*elemlist;
	ListCell	*cell;
	bool		 bool_val;

	if (parse_bool(*newval, &bool_val))
		return true;

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		GUC_check_errdetail(GUC_PREFIX ".enable_maintenance list syntax is invalid");
		goto error;
	}

	foreach(cell, elemlist)
	{
		char *tok = (char *) lfirst(cell);

		if (pg_strcasecmp(tok, "snapshot") != 0 &&
			pg_strcasecmp(tok, "log") != 0 &&
			pg_strcasecmp(tok, "repolog") != 0)
		{
			GUC_check_errdetail(GUC_PREFIX ".enable_maintenance unrecognized keyword: \"%s\"", tok);
			goto error;
		}
	}

	pfree(rawstring);
	list_free(elemlist);
	return true;

error:
	pfree(rawstring);
	list_free(elemlist);
	return false;
}

/* forbid empty and invalid time format */
static bool
check_maintenance_time(char **newval, void **extra, GucSource source)
{
	if (!*newval[0])
	{
		GUC_check_errdetail(GUC_PREFIX ".maintenance_time must not be emtpy, use default (\"%s\")",
			DEFAULT_MAINTENANCE_TIME);
		return false;
	}

	if (!verify_timestr(*newval))
	{
		GUC_check_errdetail(GUC_PREFIX ".maintenance_time invalid syntax for time: %s, use default (\"%s\")",
			*newval, DEFAULT_MAINTENANCE_TIME);
		GUC_check_errhint("format should be [hh:mm:ss]");
		return false;
	}
	return true;
}

/* verify time format string (HH:MM:SS) */
static bool
verify_timestr(const char *timestr)
{
	if (strlen(timestr) != 8)
		return false;

	/* validate field of the hour */
	if (!isdigit(timestr[0]) || !isdigit(timestr[1]) || timestr[0] > '2'
		|| (timestr[0] == '2' && timestr[1] > '3'))
		return false;

	/* validate the delimiter */
	if (timestr[2] != ':')
		return false;

	/* validate field of the minute */
	if (!isdigit(timestr[3]) || !isdigit(timestr[4]) || timestr[3] > '5')
		return false;

	/* validate the delimiter */
	if (timestr[5] != ':')
		return false;

	/* validate field of the second */
	if (!isdigit(timestr[6]) || !isdigit(timestr[7]) || timestr[6] > '5')
		return false;

	return true;
}

#if defined(WIN32)
static int
str_to_elevel(const char *name,
			  const char *str,
			  const struct config_enum_entry *options)
{
	const struct config_enum_entry *e;

	for (e = options; e && e->name; e++)
	{
		if (pg_strcasecmp(str, e->name) == 0)
			return e->val;
	}

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("invalid value for parameter \"%s\": \"%s\"", name, str)));
	return 0;
}

static const char *
elevel_to_str(int elevel)
{
	const struct config_enum_entry *e;

	for (e = elevel_options; e && e->name; e++)
	{
		if (e->val == elevel)
			return e->name;
	}

	elog(ERROR, "could not find enum option %d for %s",
		 elevel, GUC_PREFIX ".log_min_messages");
	return NULL;				/* silence compiler */
}
#endif

static int
exec_grep(const char *filename, const char *regex, List **records)
{
	List		*rec = NIL;
	FILE		*fp = NULL;
	char		 readbuf[1024];
	regex_t		 reg_t;
	regmatch_t	 matches[1];
	char		 errstr[256];
	pg_wchar	*pattern = NULL;
	int			 pattern_len;
	int			 ret;

	if ((fp = fopen(filename, "r")) == NULL)
	{
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": ", filename)));
		goto error;	/* fail */
	}

	/* Convert pattern string to wide characters */
	pattern = (pg_wchar *) palloc((strlen(regex) + 1) * sizeof(pg_wchar));
	pattern_len = pg_mb2wchar_with_len(regex, pattern, strlen(regex));

	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED, DEFAULT_COLLATION_OID);
	if (ret)
	{
		pg_regerror(ret, &reg_t, errstr, sizeof(errstr));
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
			 errmsg("invalid regular expression: %s", errstr)));
		goto error;	/* fail */
	}

	while (fgets(readbuf, sizeof(readbuf), fp) != NULL)
	{
		char		*record;
		pg_wchar	*data;
		int			 data_len;

		data = (pg_wchar *) palloc((strlen(readbuf) + 1) * sizeof(pg_wchar));
		data_len = pg_mb2wchar_with_len(readbuf, data, strlen(readbuf));

		ret = pg_regexec(&reg_t, data, data_len, 0, NULL, 1, matches, 0);
		if (ret)
		{
			if (ret != REG_NOMATCH)
			{
				/* REG_NOMATCH is not an error, everything else is */
				pg_regerror(ret, &reg_t, errstr, sizeof(errstr));
				ereport(ERROR,
					(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
					 errmsg("regular expression match failed: %s", errstr)));
				pfree(data);
				goto error;	/* fail */
			}
			/* no match */
			pfree(data);
			continue;
		}

		/* remove line separator */
		if (readbuf[strlen(readbuf) - 1] == '\n')
			readbuf[strlen(readbuf) - 1] = '\0';
		record = pstrdup(readbuf);

		rec = lappend(rec, record);
		pfree(data);
	}

	if (ferror(fp) && errno != EINTR)
	{
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": ", filename)));
		goto error;	/* fail */
	}
	pg_regfree(&reg_t);
	pfree(pattern);
	fclose(fp);

	*records = rec;
	return list_length(rec);

error:
	if (fp != NULL)
		fclose(fp);
	if (rec != NIL)
		list_free(rec);
	if (pattern)
		pfree(pattern);
	pg_regfree(&reg_t);
	return -1;
}

static int
exec_split(const char *rawstring, const char *regex, List **fields)
{
	List		*fld = NIL;
	regex_t		 reg_t;
	regmatch_t	 matches[1];
	char		 errstr[256];
	const char	*nextp;
	int			 ret;
	pg_wchar	*pattern;
	int			 pattern_len;
	int			 i;

	if (strlen(rawstring) == 0)
		return 0;

	/* Convert pattern string to wide characters */
	pattern = (pg_wchar *) palloc((strlen(regex) + 1) * sizeof(pg_wchar));
	pattern_len = pg_mb2wchar_with_len(regex, pattern, strlen(regex));

	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED, DEFAULT_COLLATION_OID);
	if (ret)
	{
		pg_regerror(ret, &reg_t, errstr, sizeof(errstr));
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
			 errmsg("invalid regular expression: %s", errstr)));
		goto error;	/* fail */
	}

	nextp = rawstring;
	for (i = 0;; i++)
	{
		char		*field;
		pg_wchar	*data;
		int			 data_len;

		data = (pg_wchar *) palloc((strlen(nextp) + 1) * sizeof(pg_wchar));
		data_len = pg_mb2wchar_with_len(nextp, data, strlen(nextp));

		ret = pg_regexec(&reg_t, data, data_len, 0, NULL, 1, matches, REG_NOTBOL|REG_NOTEOL);
		if (ret)
		{
			if (ret != REG_NOMATCH)
			{
				/* REG_NOMATCH is not an error, everything else is */
				pg_regerror(ret, &reg_t, errstr, sizeof(errstr));
				ereport(ERROR,
					(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
					 errmsg("regular expression match failed: %s", errstr)));
				pfree(data);
				goto error;	/* fail */
			}
			/* no match */
			pfree(data);
			break;
		}

		field = palloc(matches[0].rm_so + 1);
		strlcpy(field, nextp, matches[0].rm_so + 1);
		fld = lappend(fld, field);

		nextp = nextp + matches[0].rm_eo;
		pfree(data);
	}
	/* last field */
	fld = lappend(fld, pstrdup(nextp));

	pg_regfree(&reg_t);
	pfree(pattern);

	*fields = fld;
	return list_length(fld);

error:
	if (fld != NIL)
		list_free(fld);
	if (pattern)
		pfree(pattern);
	pg_regfree(&reg_t);
	return -1;
}

/*
 * Parse string as int64
 * valid range: -9223372036854775808 ~ 9223372036854775807
 */
static bool
parse_int64(const char *value, int64 *result)
{
	int64	val;
	char   *endptr;

	if (strcmp(value, "INFINITE") == 0)
	{
		*result = LLONG_MAX;
		return true;
	}

	errno = 0;
#ifdef WIN32
	val = _strtoi64(value, &endptr, 0);
#elif defined(HAVE_LONG_INT_64)
	val = strtol(value, &endptr, 0);
#elif defined(HAVE_LONG_LONG_INT_64)
	val = strtoll(value, &endptr, 0);
#else
	val = strtol(value, &endptr, 0);
#endif
	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	*result = val;

	return true;
}

/*
 * Parse string as double
 * valid range: -1.7E-308 ~ 1.7E308
 */
static bool
parse_float8(const char *value, double *result)
{
	double	val;
	char   *endptr;

	if (strcmp(value, "INFINITE") == 0)
	{
		*result = DBL_MAX;
		return true;
	}

	errno = 0;
	val = strtod(value, &endptr);

	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	*result = val;

	return true;
}

/*
 * postmaster_is_alive - check whether postmaster process is still alive
 */
static bool
postmaster_is_alive(void)
{
#ifndef WIN32
	pid_t	ppid = getppid();

	/* If the postmaster is still our parent, it must be alive. */
	if (ppid == PostmasterPid)
		return true;

	/* If the init process is our parent, postmaster must be dead. */
	if (ppid == 1)
		return false;

	/*
	 * If we get here, our parent process is neither the postmaster nor init.
	 * This can occur on BSD and MacOS systems if a debugger has been attached.
	 * We fall through to the less-reliable kill() method.
	 */

	/*
	 * Use kill() to see if the postmaster is still alive. This can sometimes
	 * give a false positive result, since the postmaster's PID may get
	 * recycled, but it is good enough for existing uses by indirect children
	 * and in debugging environments.
	 */
	return (kill(PostmasterPid, 0) == 0);
#else							/* WIN32 */
	return (WaitForSingleObject(PostmasterHandle, 0) == WAIT_TIMEOUT);
#endif   /* WIN32 */
}

/*
 * is_shared_preload - check the library is preloaded as shared library
 */
static bool
is_shared_preload(const char *library)
{
	char		*rawstring;
	List		*elemlist;
	ListCell	*cell;
	bool		 find = false;

	if (shared_preload_libraries_string == NULL ||
		shared_preload_libraries_string[0] == '\0')
		return false;

	/* need a modifiable copy of string */
	rawstring = pstrdup(shared_preload_libraries_string);

	/* parse string into list of identifiers */
	SplitIdentifierString(rawstring, ',', &elemlist);

	foreach (cell, elemlist)
	{
		if (strcmp((char *) lfirst(cell), library) == 0)
		{
			find = true;
			break;
		}
	}

	pfree(rawstring);
	list_free(elemlist);
	return find;
}

static pid_t
get_statsinfo_pid(const char *pid_file)
{
	FILE	*fp;
	pid_t	 pid;

	if ((fp = fopen(pid_file, "r")) == NULL)
	{
		int save_errno;
		/* No pid file, not an error */
		if (errno == ENOENT)
			return 0;
		save_errno = errno;
		elog(ERROR,
			"could not open PID file \"%s\": %s", pid_file, strerror(save_errno));
	}

	if (fscanf(fp, "%d\n", &pid) != 1)
		elog(ERROR, "invalid data in PID file \"%s\"", pid_file);
	fclose(fp);
	return pid;
}

static void
inet_to_cstring(const SockAddr *addr, char host[NI_MAXHOST])
{
	host[0] = '\0';

	if (addr->addr.ss_family == AF_INET
#ifdef HAVE_IPV6
		|| addr->addr.ss_family == AF_INET6
#endif
		)
	{
		char		port[NI_MAXSERV];
		int			ret;

		port[0] = '\0';
		ret = pg_getnameinfo_all(&addr->addr,
								 addr->salen,
								 host, NI_MAXHOST,
								 port, sizeof(port),
								 NI_NUMERICHOST | NI_NUMERICSERV);
		if (ret == 0)
			clean_ipv6_addr(addr->addr.ss_family, host);
		else
			host[0] = '\0';
	}
}

/*
 * lx_hash_fn - calculate hash value for a key
 */
static uint32
lx_hash_fn(const void *key, Size keysize)
{
	const LongXactHashKey	*k = (const LongXactHashKey *) key;

	return hash_uint32((uint32) k->pid) ^
		   hash_uint32((uint32) k->start);
}

/*
 * lx_match_fn - compare two keys, zero means match
 */
static int
lx_match_fn(const void *key1, const void *key2, Size keysize)
{
	const LongXactHashKey	*k1 = (const LongXactHashKey *) key1;
	const LongXactHashKey	*k2 = (const LongXactHashKey *) key2;

	if (k1->pid == k2->pid &&
		k1->start == k2->start)
		return 0;
	else
		return 1;
}

/*
 * lx_entry_alloc - allocate a new long transaction entry
 */
static LongXactEntry *
lx_entry_alloc(LongXactHashKey *key, PgBackendStatus *be)
{
	LongXactEntry	*entry;
	bool			 found;

	/* create an entry with desired hash code */
	entry = (LongXactEntry *) hash_search(long_xacts, key, HASH_ENTER, &found);

	if (!found)
	{
		/* new entry, initialize it */
		entry->pid = be->st_procpid;
		entry->start = be->st_xact_start_timestamp;
		inet_to_cstring(&be->st_clientaddr, entry->client);
	}

	return entry;
}

/*
 * lx_entry_dealloc - deallocate entries
 */
static void
lx_entry_dealloc(void)
{
	HASH_SEQ_STATUS	  hash_seq;
	LongXactEntry	**entries;
	LongXactEntry	 *entry;
	int				  entry_num;
	int				  excess;
	int				  i;

	entry_num = hash_get_num_entries(long_xacts);

	if (entry_num <= long_transaction_max)
		return;	/* not need to be deallocated */

	entries = palloc(entry_num * sizeof(LongXactEntry *));

	i = 0;
	hash_seq_init(&hash_seq, long_xacts);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
		entries[i++] = entry;

	qsort(entries, i, sizeof(LongXactEntry *), lx_entry_cmp);

	/* discards extra entries in order of duration */
	excess = entry_num - long_transaction_max;
	for (i = 0; i < excess; i++)
		hash_search(long_xacts, &entries[i]->key, HASH_REMOVE, NULL);

	pfree(entries);
}

/*
 * lx_entry_cmp - qsort comparator for sorting into duration order
 */
static int
lx_entry_cmp(const void *lhs, const void *rhs)
{
	double		l_duration = (*(LongXactEntry *const *) lhs)->duration;
	double		r_duration = (*(LongXactEntry *const *) rhs)->duration;

	if (l_duration < r_duration)
		return -1;
	else if (l_duration > r_duration)
		return +1;
	else
		return 0;
}

/*
 * ds_hash_fn - calculate hash value for a key
 */
static uint32
ds_hash_fn(const void *key, Size keysize)
{
	const DiskStatsHashKey	*k = (const DiskStatsHashKey *) key;

	return hash_uint32((uint32) k->dev_major) ^
		   hash_uint32((uint32) k->dev_minor);
}

/*
 * ds_match_fn - compare two keys, zero means match
 */
static int
ds_match_fn(const void *key1, const void *key2, Size keysize)
{
	const DiskStatsHashKey	*k1 = (const DiskStatsHashKey *) key1;
	const DiskStatsHashKey	*k2 = (const DiskStatsHashKey *) key2;

	if (k1->dev_major == k2->dev_major &&
		k1->dev_minor == k2->dev_minor)
		return 0;
	else
		return 1;
}

#define EDATA_MSGID(e) ((e)->message_id)

/*
 * pg_statsinfo_emit_log_hook - filtering by message level
 */
static void
pg_statsinfo_emit_log_hook(ErrorData *edata)
{
	static char	*m = "sending cancel to blocking autovacuum PID";
	static int	 recurse_level = 0;

	if (recurse_level > 0)
		return;

	if (prev_emit_log_hook)
		(*prev_emit_log_hook) (edata);

	/* Dedicated message for the autovacuum cancel request */
	recurse_level++;
	if ((edata->elevel == DEBUG1 || edata->elevel == LOG) &&
		strncmp(EDATA_MSGID(edata), m, strlen(m)) == 0)
	{
		int old_log_min_error_statement = log_min_error_statement;
		log_min_error_statement = LOG;
		ereport(LOG,
			(errmsg(LOGMSG_AUTOVACUUM_CANCEL_REQUEST),
			 errhint("%s", edata->message)));
		log_min_error_statement = old_log_min_error_statement;
	}

	/*
	 * Logging to csvlog higher than textlog_min_messages and
	 * syslog_min_messages and repolog_min_messages.
	 */
	if (!is_log_level_output(edata->elevel, textlog_min_messages) &&
		!is_log_level_output(edata->elevel, syslog_min_messages) &&
		!is_log_level_output(edata->elevel, repolog_min_messages))
		edata->output_to_server = false;
	recurse_level--;
}

/*
 * is_log_level_output - is elevel logically >= log_min_level?
 *
 * We use this for tests that should consider LOG to sort out-of-order,
 * between ERROR and FATAL.  Generally this is the right thing for testing
 * whether a message should go to the postmaster log, whereas a simple >=
 * test is correct for testing whether the message should go to the client.
 */
static bool
is_log_level_output(int elevel, int log_min_level)
{
	if (elevel == LOG || elevel == COMMERROR)
	{
		if (log_min_level == LOG || log_min_level <= ERROR)
			return true;
	}
	else if (log_min_level == LOG)
	{
		/* elevel != LOG */
		if (elevel >= FATAL)
			return true;
	}
	/* Neither is LOG */
	else if (elevel >= log_min_level)
		return true;

	return false;
}

/*
 * pg_statsinfo_shmem_request_hook - allocate or attach to shared memory
 */
static void
pg_statsinfo_shmem_request_hook(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	/* Install wait_sampling */
	init_wait_sampling();

	/* Install last_xact_activity */
	request_last_xact_activity();

	RequestAddinShmemSpace(silShmemSize());
	RequestNamedLWLockTranche("pg_statsinfo", 1);

	return;
}

/*
 * pg_statsinfo_shmem_startup_hook - allocate or attach to shared memory
 */
static void
pg_statsinfo_shmem_startup_hook(void)
{
	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	/* create or attach to the shared memory state */
	silShmemInit();

	wait_sampling_shmem_startup();

	return;
}

/*
 * silShmemInit -
 *     allocate and initialize pg_statsinfo launcher-related shared memory
 */
static void
silShmemInit(void)
{
	bool	found;

	/* create or attach to the shared memory state */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	sil_state = ShmemInitStruct("pg_statsinfo",
								silShmemSize(),
								&found);
	if (!found)
	{
		/* First time through ... */
		sil_state->lockid = &(GetNamedLWLockTranche("pg_statsinfo"))->lock;
		sil_state->pid = INVALID_PID;
	}
	else
		Assert(found);
	LWLockRelease(AddinShmemInitLock);
}

/*
 * silShmemSize - report shared memory space needed by silShmemInit
 */
static Size
silShmemSize(void)
{
	return MAXALIGN(sizeof(silSharedState));
}

/*
 * lookup_sil_state - lookup the pg_statsinfo launcher state from shared memory
 */
static void
lookup_sil_state(void)
{
	bool	found;

	/* lookup the pg_statsinfo launcher state with shared lock */
	LWLockAcquire(sil_state->lockid, LW_SHARED);
	sil_state = ShmemInitStruct("pg_statsinfo",
								silShmemSize(),
								&found);
	LWLockRelease(sil_state->lockid);
}

static void
sample_waits(void){
	probe_waits();
}

uint32
wait_sampling_hash_fn(const void *key, Size keysize)
{
	const wait_samplingHashKey	*k = (const wait_samplingHashKey *) key;

	return hash_uint32((uint32) k->userid) ^
			hash_uint32((uint32) k->dbid) ^
			hash_uint32((uint32) k->queryid) ^
			hash_uint32((uint32) k->backend_type) ^
			hash_uint32((uint32) k->wait_event_info);
}

int
wait_sampling_match_fn(const void *key1, const void *key2, Size keysize)
{
	const wait_samplingHashKey	*k1 = (const wait_samplingHashKey *) key1;
	const wait_samplingHashKey	*k2 = (const wait_samplingHashKey *) key2;

	if (k1->userid == k2->userid &&
		k1->dbid == k2->dbid &&
		k1->queryid == k2->queryid &&
		k1->backend_type == k2->backend_type &&
		k1->wait_event_info == k2->wait_event_info)
		return 0;
	else
		return 1;
}

static void
wait_sampling_entry_dealloc(void)
{
	HASH_SEQ_STATUS hash_seq;
	wait_samplingEntry **entries;
	wait_samplingEntry  *entry;
	int		     nvictims;
	int		     i;
	int		     j;
	HTAB		*hash_tmp;
	HASHCTL		ctl_tmp;
	wait_samplingSubEntry  *subentry;
	wait_samplingSubEntry  item;
	bool		  found;
	double		  usage_tie;
	uint64		  queryid_tie;
	Oid			  userid_tie;
	Oid			  dbid_tie;

	/*
	 * Sort entries by usage and deallocate USAGE_DEALLOC_PERCENT of them.
	 * While we're scanning the table, apply the decay factor to the usage
	 * values.
	 */
	entries = palloc(hash_get_num_entries(wait_sampling_hash) * sizeof(wait_samplingEntry *));

	ctl_tmp.hash = wait_sampling_sub_hash_fn;
	ctl_tmp.match = wait_sampling_sub_match_fn;
	ctl_tmp.keysize = sizeof(wait_samplingSubHashKey);
	ctl_tmp.entrysize = sizeof(wait_samplingSubEntry);
	/*
	 * hash_tmp is contraction of wait_sampling_hash
	 * to reduce the dimension
	 * applied to fields (backend_type, wait_event_info)
	 * Choosing victims in terms of each (userid, dbid, queryid)
	 * is a election that should work something like limit .. with tie
	 * because of consistency with pg_stat_statements.
	 */
	hash_tmp = hash_create("temporary table to find victims of wait_sampling_hash",
							wait_sampling_max,
							&ctl_tmp,
							HASH_FUNCTION | HASH_ELEM);

	i = 0;
	hash_seq_init(&hash_seq, wait_sampling_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		entries[i++] = entry;
		entry->counters.usage *= STATSINFO_USAGE_DECREASE_FACTOR; /* usage is decayed by time passes */

		item.key.userid = entry->key.userid;
		item.key.dbid = entry->key.dbid;
		item.key.queryid = entry->key.queryid;
		subentry = (wait_samplingSubEntry *) hash_search(hash_tmp, &item, HASH_ENTER, &found);
		if (!found)
			subentry->usage = entry->counters.usage;
		else if (subentry->usage < entry->counters.usage)
			subentry->usage = entry->counters.usage;
	}

	Assert(hash_get_num_entries(wait_sampling_hash) == i);

	for (j = 0; j < i; j++)
	{
		item.key.userid = entries[j]->key.userid;
		item.key.dbid = entries[j]->key.dbid;
		item.key.queryid = entries[j]->key.queryid;
		subentry = (wait_samplingSubEntry *) hash_search(hash_tmp, &item, HASH_FIND, NULL);
		if (!subentry)
			elog(WARNING, "There is a missing item in hash_tmp");
		else if (entries[j]->counters.usage < subentry->usage)
			entries[j]->counters.usage = subentry->usage;
	}

	/* order by usage, queryid, userid, dbid */
	qsort(entries, i, sizeof(wait_samplingEntry *), wait_sampling_entry_cmp);

	nvictims = Max(10, i * STATSINFO_USAGE_DEALLOC_PERCENT / 100);
	nvictims = Min(nvictims, i);

	for (j = 0; j < nvictims; j++)
	{
		hash_search(wait_sampling_hash, &entries[j]->key, HASH_REMOVE, NULL);
	}

	usage_tie = entries[nvictims - 1]->counters.usage;
	queryid_tie = entries[nvictims - 1]->key.queryid;
	userid_tie = entries[nvictims - 1]->key.userid;
	dbid_tie = entries[nvictims - 1]->key.dbid;

	for (j = nvictims; j < i; j++)
	{
		if (usage_tie == entries[j]->counters.usage &&
			queryid_tie == entries[j]->key.queryid &&
			userid_tie == entries[j]->key.userid &&
			dbid_tie == entries[j]->key.dbid)
			hash_search(wait_sampling_hash, &entries[j]->key, HASH_REMOVE, NULL);
		else
			break;
	}

	/* Increment the number of times entries are deallocated */
	{
		volatile wait_samplingSharedState *s = (volatile wait_samplingSharedState *) wait_sampling;

		SpinLockAcquire(&s->mutex);
		s->stats.dealloc += 1;
		SpinLockRelease(&s->mutex);
	}

	hash_destroy(hash_tmp);
	pfree(entries);
}

static uint32
wait_sampling_sub_hash_fn(const void *key, Size keysize)
{
	const wait_samplingSubHashKey	*k = (const wait_samplingSubHashKey *) key;

	return hash_uint32((uint32) k->userid) ^
		   hash_uint32((uint32) k->dbid) ^
		   hash_uint32((uint32) k->queryid);
}

static int
wait_sampling_sub_match_fn(const void *key1, const void *key2, Size keysize)
{
	const wait_samplingSubHashKey	*k1 = (const wait_samplingSubHashKey *) key1;
	const wait_samplingSubHashKey	*k2 = (const wait_samplingSubHashKey *) key2;

	if (k1->userid == k2->userid &&
		k1->dbid == k2->dbid &&
		k1->queryid == k2->queryid)
		return 0;
	else
		return 1;
}

static int
wait_sampling_entry_cmp(const void *lhs, const void *rhs)
{
	double		l_usage = (*(wait_samplingEntry *const *) lhs)->counters.usage;
	double		r_usage = (*(wait_samplingEntry *const *) rhs)->counters.usage;
	uint64		l_queryid = (*(wait_samplingEntry *const *) lhs)->key.queryid;
	uint64		r_queryid = (*(wait_samplingEntry *const *) rhs)->key.queryid;
	Oid			l_userid = (*(wait_samplingEntry *const *) lhs)->key.userid;
	Oid			r_userid = (*(wait_samplingEntry *const *) rhs)->key.userid;
	Oid			l_dbid = (*(wait_samplingEntry *const *) lhs)->key.dbid;
	Oid			r_dbid = (*(wait_samplingEntry *const *) rhs)->key.dbid;

	if (l_usage < r_usage)
		return -1;
	else if (l_usage > r_usage)
		return +1;

	if (l_queryid < r_queryid)
		return -1;
	else if (l_queryid > r_queryid)
		return +1;

	if (l_userid < r_userid)
		return -1;
	else if (l_userid > r_userid)
		return +1;

	if (l_dbid < r_dbid)
		return -1;
	else if (l_dbid > r_dbid)
		return +1;
	
	return 0;
}

/*
 * wait_sampling_entry_alloc - Enter the item into the hash table. 
 * if direct == true, it store the specific item to hash table as-is.
 * It use at reading saved stats file.
 */
void
wait_sampling_entry_alloc(wait_samplingEntry *item, bool direct)
{
	wait_samplingEntry	*entry;
	bool		found;

	/* Make space if needed */
	while (hash_get_num_entries(wait_sampling_hash) >= wait_sampling_max)
		wait_sampling_entry_dealloc();

	entry = (wait_samplingEntry *) hash_search(wait_sampling_hash, &item->key, HASH_ENTER, &found);
	if (found)
	{
		SpinLockAcquire(&entry->mutex);
 		entry->counters.count++;
		entry->counters.usage += STATSINFO_USAGE_INCREASE; /* usage is raised by touching */
		SpinLockRelease(&entry->mutex);
 	}
	else if (direct)
	{
		memset(&entry->counters, 0, sizeof(wait_samplingCounters));
		entry->counters = item->counters;
		SpinLockInit(&entry->mutex);
	}
	else
	{
		memset(&entry->counters, 0, sizeof(wait_samplingCounters));
		entry->counters.count = 1;
		entry->counters.usage = STATSINFO_USAGE_INIT;
		SpinLockInit(&entry->mutex);
	}
}

static void
probe_waits(void)
{
	int			i;

	for (i = pgstat_fetch_stat_numbackends(); i > 0; i--)
	{
		PgBackendStatus	*be;
		wait_samplingEntry		item;
		PGPROC			*proc;
		int				procpid;

		be = pgstat_get_beentry_by_backend_id(i);
		if (!be)
			continue;

		procpid = be->st_procpid;

		if (procpid == 0)
			continue;

		proc = BackendPidGetProc(procpid);

		if (proc == NULL)
			 continue;	/* This backend is dead */

		if (proc->wait_event_info == 0)
			continue;

		item.key.userid = be->st_userid;
		item.key.dbid = be->st_databaseid;

		if (wait_sampling_queries)
			item.key.queryid = be->st_query_id;
		else
			item.key.queryid = 0;

		item.key.backend_type = be->st_backendType;

		item.key.wait_event_info = proc->wait_event_info;

		/* store this item */
		wait_sampling_entry_alloc(&item, false);
	}
}
