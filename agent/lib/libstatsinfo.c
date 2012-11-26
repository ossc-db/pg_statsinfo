/*
 * lib/libstatsinfo.c
 *
 * Copyright (c) 2010-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "libstatsinfo.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "access/heapam.h"
#include "catalog/pg_type.h"
#include "catalog/pg_control.h"
#include "catalog/pg_tablespace.h"
#include "funcapi.h"
#include "libpq/ip.h"
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
#include "utils/tqual.h"
#include "utils/lsyscache.h"
#include "utils/ps_status.h"

#if PG_VERSION_NUM >= 90100
#include "catalog/pg_collation.h"
#endif

#if PG_VERSION_NUM >= 90200
#include "utils/timestamp.h"
#include "utils/rel.h"
#endif

#include "pgut/pgut-be.h"
#include "pgut/pgut-spi.h"
#include "../common.h"

#ifndef WIN32
#include "linux/version.h"
#endif

/* also adjust non-critial setting parameters? */
/* #define ADJUST_NON_CRITICAL_SETTINGS */

#ifndef WIN32
#define PROGRAM_NAME		"pg_statsinfod"
#else
#define PROGRAM_NAME		"pg_statsinfo.exe"
#endif

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
#if PG_VERSION_NUM >= 90200
#define MSG_AUTOVACUUM \
	"automatic vacuum of table \"%s.%s.%s\": index scans: %d\n" \
	"pages: %d removed, %d remain\n" \
	"tuples: %.0f removed, %.0f remain\n" \
	"buffer usage: %d hits, %d misses, %d dirtied\n" \
	"avg read rate: %.3f MiB/s, avg write rate: %.3f MiB/s\n" \
	"system usage: %s"
#else
#define MSG_AUTOVACUUM \
	"automatic vacuum of table \"%s.%s.%s\": index scans: %d\n" \
	"pages: %d removed, %d remain\n" \
	"tuples: %.0f removed, %.0f remain\n" \
	"system usage: %s"
#endif

/* log_autovacuum_min_duration: analyze */
#define MSG_AUTOANALYZE \
	"automatic analyze of table \"%s.%s.%s\" system usage: %s"

/* log_checkpoints: staring */
#define MSG_CHECKPOINT_STARTING \
	"checkpoint starting:%s%s%s%s%s%s%s"

/* log_checkpoints: complete */
#if PG_VERSION_NUM >= 90100
#define MSG_CHECKPOINT_COMPLETE \
	"checkpoint complete: wrote %d buffers (%.1f%%); " \
	"%d transaction log file(s) added, %d removed, %d recycled; " \
	"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s; " \
	"sync files=%d, longest=%ld.%03d s, average=%ld.%03d s"
#else
#define MSG_CHECKPOINT_COMPLETE \
	"checkpoint complete: wrote %d buffers (%.1f%%); " \
	"%d transaction log file(s) added, %d removed, %d recycled; " \
	"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s"
#endif

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

#define DEFAULT_SAMPLING_INTERVAL		5		/* sec */
#define DEFAULT_SNAPSHOT_INTERVAL		600		/* sec */
#define DEFAULT_SYSLOG_LEVEL			DISABLE
#define DEFAULT_TEXTLOG_LEVEL			WARNING
#define DEFAULT_MAINTENANCE_TIME		"00:02:00"
#define DEFAULT_REPOSITORY_KEEPDAY		7		/* day */
#define DEFAULT_LOG_MAINTENANCE_COMMAND	default_log_maintenance_command()
#define DEFAULT_LONG_LOCK_THREASHOLD	30		/* sec */
#define DEFAULT_STAT_STATEMENTS_MAX		30
#define LONG_TRANSACTION_THRESHOLD		1.0		/* sec */

#if PG_VERSION_NUM < 80400
#define DEFAULT_ENABLE_MAINTENANCE		"3"		/* snapshot + log */
#else
#define DEFAULT_ENABLE_MAINTENANCE		"on"	/* snapshot + log */
#endif

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

static char	   *excluded_dbnames = NULL;
static char	   *excluded_schemas = NULL;
static char	   *repository_server = NULL;
static int		sampling_interval = DEFAULT_SAMPLING_INTERVAL;
static int		snapshot_interval = DEFAULT_SNAPSHOT_INTERVAL;
static char	   *syslog_line_prefix = NULL;
static int		syslog_min_messages = DEFAULT_SYSLOG_LEVEL;
static char	   *textlog_filename = NULL;
static char	   *textlog_line_prefix = NULL;
static int		textlog_min_messages = DEFAULT_TEXTLOG_LEVEL;
static int		textlog_permission = 0600;
static bool		adjust_log_level = false;
static char	   *adjust_log_info = NULL;
static char	   *adjust_log_notice = NULL;
static char	   *adjust_log_warning = NULL;
static char	   *adjust_log_error = NULL;
static char	   *adjust_log_log = NULL;
static char	   *adjust_log_fatal = NULL;
static char	   *textlog_nologging_users = NULL;
static char	   *enable_maintenance = NULL;
static char	   *maintenance_time = NULL;
static int		repository_keepday = DEFAULT_REPOSITORY_KEEPDAY;
static char	   *log_maintenance_command = NULL;
static int		long_lock_threashold = DEFAULT_LONG_LOCK_THREASHOLD;
static int		stat_statements_max = DEFAULT_STAT_STATEMENTS_MAX;
static char	   *stat_statements_exclude_users = NULL;

/*---- Function declarations ----*/

PG_FUNCTION_INFO_V1(statsinfo_sample);
PG_FUNCTION_INFO_V1(statsinfo_activity);
PG_FUNCTION_INFO_V1(statsinfo_snapshot);
PG_FUNCTION_INFO_V1(statsinfo_maintenance);
PG_FUNCTION_INFO_V1(statsinfo_tablespaces);
PG_FUNCTION_INFO_V1(statsinfo_restart);
PG_FUNCTION_INFO_V1(statsinfo_cpustats);
PG_FUNCTION_INFO_V1(statsinfo_cpustats_noarg);
PG_FUNCTION_INFO_V1(statsinfo_devicestats);
PG_FUNCTION_INFO_V1(statsinfo_devicestats_noarg);
PG_FUNCTION_INFO_V1(statsinfo_loadavg);
PG_FUNCTION_INFO_V1(statsinfo_memory);
PG_FUNCTION_INFO_V1(statsinfo_profile);

extern Datum PGUT_EXPORT statsinfo_sample(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_activity(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_snapshot(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_maintenance(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_tablespaces(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_restart(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_cpustats(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_cpustats_noarg(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_devicestats(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_devicestats_noarg(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_loadavg(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_memory(PG_FUNCTION_ARGS);
extern Datum PGUT_EXPORT statsinfo_profile(PG_FUNCTION_ARGS);

extern PGUT_EXPORT void	_PG_init(void);
extern PGUT_EXPORT void	_PG_fini(void);
extern PGUT_EXPORT void	init_last_xact_activity(void);
extern PGUT_EXPORT void	fini_last_xact_activity(void);

/*----  Internal declarations ----*/

static void inet_to_cstring(const SockAddr *addr, char host[NI_MAXHOST]);
static void StartStatsinfoLauncher(void);
static void StatsinfoLauncherMain(void);
static void sil_sigchld_handler(SIGNAL_ARGS);
static void sil_exit(SIGNAL_ARGS);
static pid_t exec_background_process(char cmd[]);
static uint64 get_sysident(void);
static void must_be_superuser(void);
static int get_devinfo(const char *path, Datum values[], bool nulls[]);
static char *get_archive_path(void);
static void adjust_log_destination(GucContext context, GucSource source);
static int get_log_min_messages(void);
static pid_t get_postmaster_pid(void);
static bool verify_log_filename(const char *filename);
static bool verify_timestr(const char *timestr);
static bool postmaster_is_alive(void);

#if PG_VERSION_NUM >= 90100
static bool check_textlog_filename(char **newval, void **extra, GucSource source);
static bool check_enable_maintenance(char **newval, void **extra, GucSource source);
static bool check_maintenance_time(char **newval, void **extra, GucSource source);
#else
static const char *assign_textlog_filename(const char *newval, bool doit, GucSource source);
static const char *assign_enable_maintenance(const char *newval, bool doit, GucSource source);
static const char *assign_maintenance_time(const char *newval, bool doit, GucSource source);
#endif

static Datum get_cpustats(FunctionCallInfo fcinfo,
	int64 prev_cpu_user, int64 prev_cpu_system, int64 prev_cpu_idle, int64 prev_cpu_iowait);
static Datum get_devicestats(FunctionCallInfo fcinfo, ArrayType *devicestats);
static int exec_grep(const char *filename, const char *regex, List **records);
static int exec_split(const char *rawstring, const char *regex, List **fields);
static bool parse_int64(const char *value, int64 *result);
static bool parse_float8(const char *value, double *result);

#if PG_VERSION_NUM < 80400
static bool parse_bool(const char *value, bool *result);
static const char *elevel_to_str(int elevel);
#endif

static char *b_trim(char *str);
static Datum BuildArrayType(List *values, Oid elmtype, Datum(*convert)(void *));
static Datum _CStringGetTextDatum(void *ptr);
static HeapTupleHeader search_devicestats(ArrayType *devicestats, const char *device_name);

#if PG_VERSION_NUM < 80400 || defined(WIN32)
static int str_to_elevel(const char *name, const char *str,
						 const struct config_enum_entry *options);
#endif

#if PG_VERSION_NUM < 80400
static char	   *syslog_min_messages_str;
static char	   *textlog_min_messages_str;
static const char *assign_syslog_min_messages(const char *newval, bool doit, GucSource source);
static const char *assign_textlog_min_messages(const char *newval, bool doit, GucSource source);
static const char *assign_elevel(const char *name, int *var, const char *newval, bool doit);

/* 8.3 or earlier versions can work only with PGC_USERSET */
#undef PGC_SIGHUP
#define PGC_SIGHUP		PGC_USERSET

#endif

/* sampled statistics */
typedef struct Stats
{
	int			samples;

	/* from pg_stat_activity */
	int			idle;
	int			idle_in_xact;
	int			waiting;
	int			running;

	/* longest transaction */
	int			max_xact_pid;
	TimestampTz	max_xact_start;
	double		max_xact_duration;	/* in sec */
	char		max_xact_client[NI_MAXHOST];
	char		max_xact_query[1];	/* VARIABLE LENGTH ARRAY - MUST BE LAST */
} Stats;

static Stats	*stats;

/* flags for pg_statsinfo launcher */
static volatile bool need_exit = false;
static volatile bool got_SIGCHLD = false;

/*
 * statsinfo_sample - sample statistics for server instance.
 */
Datum
statsinfo_sample(PG_FUNCTION_ARGS)
{
	TimestampTz	now;
	int			backends;
	int			idle;
	int			idle_in_xact;
	int			waiting;
	int			running;
	int			i;

	must_be_superuser();

	if (stats == NULL)
	{
		stats = (Stats *) malloc(offsetof(Stats, max_xact_query) +
								 pgstat_track_activity_query_size);
		if (stats == NULL)
			ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));

		memset(stats, 0, sizeof(*stats));
	}

	now = GetCurrentTimestamp();
	backends = pgstat_fetch_stat_numbackends();
	idle = idle_in_xact = waiting = running = 0;

	for (i = 1; i <= backends; i++)
	{
		PgBackendStatus    *be;
		long				secs;
		int					usecs;
		double				duration;
		PGPROC			   *proc;

		be = pgstat_fetch_stat_beentry(i);
		if (!be)
			continue;

		/*
		 * sample idle transactions
		 */
		if (be->st_procpid == MyProcPid)
			;	/* exclude myself */
		else if (be->st_waiting)
			waiting++;
#if PG_VERSION_NUM >= 90200
		else if (be->st_state == STATE_IDLE)
			idle++;
		else if (be->st_state == STATE_IDLEINTRANSACTION)
			idle_in_xact++;
		else if (be->st_state == STATE_RUNNING)
			running++;
#else
		else if (be->st_activity[0] != '\0')
		{
			if (strcmp(be->st_activity, "<IDLE>") == 0)
				idle++;
			else if (strcmp(be->st_activity, "<IDLE> in transaction") == 0)
				idle_in_xact++;
			else
				running++;
		}
#endif

		/*
		 * sample long transactions, but exclude vacuuming processes.
		 */
		if (be->st_xact_start_timestamp == 0)
			continue;

		TimestampDifference(be->st_xact_start_timestamp, now, &secs, &usecs);
		duration = secs + usecs / 1000000.0;
		if (duration < stats->max_xact_duration ||
			duration < LONG_TRANSACTION_THRESHOLD)
			continue;

		/* XXX: needs lock? */
#if PG_VERSION_NUM >= 90200
		if ((proc = BackendPidGetProc(be->st_procpid)) == NULL ||
			(ProcGlobal->allPgXact[proc->pgprocno].vacuumFlags & PROC_IN_VACUUM))
			continue;

		if (be->st_state == STATE_IDLEINTRANSACTION)
			strlcpy(stats->max_xact_query,
				"<IDLE> in transaction", pgstat_track_activity_query_size);
		else
			strlcpy(stats->max_xact_query,
				be->st_activity, pgstat_track_activity_query_size);
#else
		if ((proc = BackendPidGetProc(be->st_procpid)) == NULL ||
			(proc->vacuumFlags & PROC_IN_VACUUM))
			continue;

		strlcpy(stats->max_xact_query,
			be->st_activity, pgstat_track_activity_query_size);
#endif

		stats->max_xact_pid = be->st_procpid;
		stats->max_xact_start = be->st_xact_start_timestamp;
		stats->max_xact_duration = duration;
		inet_to_cstring(&be->st_clientaddr, stats->max_xact_client);
	}

	stats->idle += idle;
	stats->idle_in_xact += idle_in_xact;
	stats->waiting += waiting;
	stats->running += running;

	stats->samples++;

	PG_RETURN_VOID();
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

#define NUM_ACTIVITY_COLS		9

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

	if (stats != NULL && stats->samples > 0)
	{
		double		samples = stats->samples;

		memset(nulls, 0, sizeof(nulls));

		i = 0;
		values[i++] = Float8GetDatum(stats->idle / samples);
		values[i++] = Float8GetDatum(stats->idle_in_xact / samples);
		values[i++] = Float8GetDatum(stats->waiting / samples);
		values[i++] = Float8GetDatum(stats->running / samples);

		if (stats->max_xact_client[0])
			values[i++] = CStringGetTextDatum(stats->max_xact_client);
		else
			nulls[i++] = true;
		if (stats->max_xact_pid != 0)
		{
			values[i++] = Int32GetDatum(stats->max_xact_pid);
			values[i++] = TimestampTzGetDatum(stats->max_xact_start);
			values[i++] = Float8GetDatum(stats->max_xact_duration);
			values[i++] = CStringGetTextDatum(stats->max_xact_query);
		}
		else
		{
			nulls[i++] = true;
			nulls[i++] = true;
			nulls[i++] = true;
			nulls[i++] = true;
		}
		Assert(i == lengthof(values));

		/* reset stats */
		memset(stats, 0, sizeof(*stats));
	}
	else
	{
		for (i = 0; i < lengthof(nulls); i++)
			nulls[i] = true;
	}

	tuple = heap_form_tuple(tupdesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
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
#if PG_VERSION_NUM >= 80400
	DefineCustomEnumVariable(GUC_PREFIX ".syslog_min_messages",
							 "Sets the message levels that are system-logged.",
							 NULL,
							 &syslog_min_messages,
							 DEFAULT_SYSLOG_LEVEL,
							 elevel_options,
							 PGC_SIGHUP,
							 0,
#if PG_VERSION_NUM >= 90100
							 NULL,
#endif
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
#if PG_VERSION_NUM >= 90100
							 NULL,
#endif
							 NULL,
							 NULL);
#else
	DefineCustomStringVariable(GUC_PREFIX ".syslog_min_messages",
							   "Sets the message levels that are system-logged.",
							   NULL,
							   &syslog_min_messages_str,
							   elevel_to_str(DEFAULT_SYSLOG_LEVEL),
							   PGC_SIGHUP,
							   0,
							   assign_syslog_min_messages,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_min_messages",
							   "Sets the message levels that are text-logged.",
							   NULL,
							   &textlog_min_messages_str,
							   elevel_to_str(DEFAULT_TEXTLOG_LEVEL),
							   PGC_SIGHUP,
							   0,
							   assign_textlog_min_messages,
							   NULL);
#endif

	DefineCustomStringVariable(GUC_PREFIX ".textlog_filename",
							   "Sets the latest file name for textlog.",
							   NULL,
							   &textlog_filename,
							   "postgresql.log",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   check_textlog_filename,
							   NULL,
#else
						       assign_textlog_filename,
#endif
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_line_prefix",
							   "Controls information prefixed to each textlog line.",
							   "If blank, no prefix is used.",
							   &textlog_line_prefix,
							   "%t %p ",
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".syslog_line_prefix",
							   "Controls information prefixed to each syslog line.",
							   "If blank, no prefix is used.",
							   &syslog_line_prefix,
							   "%t %p ",
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
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
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".excluded_dbnames",
							   "Selects which dbnames are excluded by pg_statsinfo.",
							   NULL,
							   &excluded_dbnames,
							   "template0, template1",
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".excluded_schemas",
							   "Selects which schemas are excluded by pg_statsinfo.",
							   NULL,
							   &excluded_schemas,
							   "pg_catalog,pg_toast,information_schema",
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
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
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
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
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".repository_server",
							   "Connection string for repository database.",
							   NULL,
							   &repository_server,
							   default_repository_server,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomBoolVariable(GUC_PREFIX ".adjust_log_level",
							 "Enable the log level adjustment.",
							 NULL,
							 &adjust_log_level,
							 false,
							 PGC_SIGHUP,
							 GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							 NULL,
#endif
							 NULL,
							 NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_info",
							   "Selects SQL-STATE that want to change log level to 'INFO'.",
							   NULL,
							   &adjust_log_info,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_notice",
							   "Selects SQL-STATE that want to change log level to 'NOTICE'.",
							   NULL,
							   &adjust_log_notice,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_warning",
							   "Selects SQL-STATE that want to change log level to 'WARNING'.",
							   NULL,
							   &adjust_log_warning,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_error",
							   "Selects SQL-STATE that want to change log level to 'ERROR'.",
							   NULL,
							   &adjust_log_error,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_log",
							   "Selects SQL-STATE that want to change log level to 'LOG'.",
							   NULL,
							   &adjust_log_log,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".adjust_log_fatal",
							   "Selects SQL-STATE that want to change log level to 'FATAL'.",
							   NULL,
							   &adjust_log_fatal,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".textlog_nologging_users",
							   "Sets dbusers that doesn't logging to textlog.",
							   NULL,
							   &textlog_nologging_users,
							   "",
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".enable_maintenance",
							   "Sets the maintenance mode.",
							   NULL,
							   &enable_maintenance,
							   DEFAULT_ENABLE_MAINTENANCE,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   check_enable_maintenance,
							   NULL,
#else
						       assign_enable_maintenance,
#endif
							   NULL);

	DefineCustomStringVariable(GUC_PREFIX ".maintenance_time",
							   "Sets the maintenance time.",
							   NULL,
							   &maintenance_time,
							   DEFAULT_MAINTENANCE_TIME,
							   PGC_SIGHUP,
							   GUC_SUPERUSER_ONLY,
#if PG_VERSION_NUM >= 90100
							   check_maintenance_time,
							   NULL,
#else
						       assign_maintenance_time,
#endif
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
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".log_maintenance_command",
							   "Sets the shell command that will be called to the log maintenance.",
							   NULL,
							   &log_maintenance_command,
							   DEFAULT_LOG_MAINTENANCE_COMMAND,
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	DefineCustomIntVariable(GUC_PREFIX ".long_lock_threashold",
							"Sets the threshold of lock wait time.",
							NULL,
							&long_lock_threashold,
							DEFAULT_LONG_LOCK_THREASHOLD,
							0,
							INT_MAX,
							PGC_SIGHUP,
							0,
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
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
#if PG_VERSION_NUM >= 90100
							NULL,
#endif
							NULL,
							NULL);

	DefineCustomStringVariable(GUC_PREFIX ".stat_statements_exclude_users",
							   "Sets dbusers that doesn't collect statistics of statement from pg_stat_statements.",
							   NULL,
							   &stat_statements_exclude_users,
							   "",
							   PGC_SIGHUP,
							   0,
#if PG_VERSION_NUM >= 90100
							   NULL,
#endif
							   NULL,
							   NULL);

	EmitWarningsOnPlaceholders("pg_statsinfo");

	if (IsUnderPostmaster)
		return;

	/*
	 * Check supported parameters combinations.
	 */
	if (get_log_min_messages() >= FATAL)
		ereport(FATAL,
			(errmsg(LOG_PREFIX "unsupported log_min_messages: %s",
					GetConfigOption("log_min_messages", false)),
			 errhint("must be same or more verbose than 'log'")));
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
#if PG_VERSION_NUM >= 80400
	if (!pgstat_track_functions)
		SetConfigOption("track_functions", "all",
						PGC_POSTMASTER, PGC_S_OVERRIDE);
#endif
#endif /* ADJUST_NON_CRITICAL_SETTINGS */

#if PG_VERSION_NUM >= 80400
	/* Install xact_last_activity */
	init_last_xact_activity();
#endif

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
#if PG_VERSION_NUM >= 80400
	/* Uninstall xact_last_activity */
	fini_last_xact_activity();
#endif
}

/*
 * statsinfo_restart - Restart statsinfo background process.
 */
Datum
statsinfo_restart(PG_FUNCTION_ARGS)
{
	char	cmd[MAXPGPATH];
	int		save_log_min_messages = 0;

	must_be_superuser();

	/* send log to terminate existing daemon. */
	if (log_min_messages >= FATAL)
	{
		/* adjust elevel to LOG so that LOGMSG_RESTART must be written. */
		save_log_min_messages = log_min_messages;
		log_min_messages = LOG;
	}
	elog(LOG, LOGMSG_RESTART);
	if (save_log_min_messages > 0)
	{
		log_min_messages = save_log_min_messages;
	}

	/* short sleep to ensure message is written */
	pg_usleep(100 * 1000);
	/*
	 * FIXME: server logs written during the sleep might not be routed by
	 * pg_statsinfo daemon, but I have no idea to ensure to place the
	 * LOGMSG_RESTART message at the end of previous log file...
	 */

	/* force rotate the log file */
	DirectFunctionCall1(pg_rotate_logfile, (Datum) 0);

	/* wait for the previous daemon's exit and log rotation */
	pg_usleep(500 * 1000);

	/* spawn a new daemon process */
	exec_background_process(cmd);

	/*
	 * return the command line for the new daemon; Note that we cannot
	 * return the child pid because it is different from the pid of statsinfo
	 * daemon because the child process will call daemon().
	 */
	PG_RETURN_TEXT_P(cstring_to_text(cmd));
}

#define FILE_CPUSTAT			"/proc/stat"
#define NUM_CPUSTATS_COLS		9
#define NUM_STAT_FIELDS_MIN		6

/* not support a kernel that does not have the required fields at "/proc/stat" */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,41)
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

/*
 * statsinfo_devicestats - get device information
 */
Datum
statsinfo_devicestats(PG_FUNCTION_ARGS)
{
	ArrayType	*devicestats = NULL;

	if (!PG_ARGISNULL(0))
		devicestats = PG_GETARG_ARRAYTYPE_P(0);

	PG_RETURN_DATUM(get_devicestats(fcinfo, devicestats));
}

/*
 * statsinfo_devicestats - get device information 
 * (remains of the old interface)
 */
Datum
statsinfo_devicestats_noarg(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(get_devicestats(fcinfo, NULL));
}

#define FILE_DISKSTATS					"/proc/diskstats"
#define NUM_DEVICESTATS_COLS			15
#define TYPE_DEVICE_TABLESPACES			TEXTOID
#define NUM_DISKSTATS_FIELDS			14
#define NUM_DISKSTATS_PARTITION_FIELDS	7
#define SQL_SELECT_TABLESPACES "\
SELECT \
	device, name \
FROM \
	statsinfo.tablespaces \
WHERE \
	device IS NOT NULL \
ORDER BY device"

#define ARRNELEMS(x)	ArrayGetNItems(ARR_NDIM(x), ARR_DIMS(x))
#define ARRPTR(x)		((HeapTupleHeader) ARR_DATA_PTR(x))

/*
 * statsinfo_devicestats - get device information
 */
static Datum
get_devicestats(FunctionCallInfo fcinfo, ArrayType *devicestats)
{
	ReturnSetInfo	*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc		 tupdesc;
	Tuplestorestate	*tupstore;
	MemoryContext	 per_query_ctx;
	MemoryContext	 oldcontext;
	SPITupleTable	*tuptable;
	Datum			 values[NUM_DEVICESTATS_COLS];
	bool			 nulls[NUM_DEVICESTATS_COLS];
	List			*spclist = NIL;
	char			*prev_device = NULL;
	int				 row;

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

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI connect failure");

	execute(SPI_OK_SELECT, SQL_SELECT_TABLESPACES);
	tuptable = SPI_tuptable;

	for (row = 0; row < SPI_processed; row++)
	{
		HeapTupleHeader prev_devicestats;
		char *device;
		char *spcname;
		char *dev_major;
		char *dev_minor;
		char *dev_name = NULL;
		int64 readsector;
		int64 readtime;
		int64 writesector;
		int64 writetime;
		int64 ioqueue;
		int64 iototaltime;
		char *record;
		char  regex[64];
		List *devicenum = NIL;
		List *records = NIL;
		List *fields = NIL;
		int   nfield;

		device = SPI_getvalue(tuptable->vals[row], tuptable->tupdesc, 1);
		spcname = SPI_getvalue(tuptable->vals[row], tuptable->tupdesc, 2);

		if (prev_device)
		{
			if (strcmp(device, prev_device) == 0)
			{
				spclist = lappend(spclist, spcname);
				continue;
			}
			/* device_tblspaces */
			values[14] = BuildArrayType(spclist, TYPE_DEVICE_TABLESPACES, _CStringGetTextDatum);
			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}

		/* <device_mejor>:<device_minor> */
		exec_split(device, ":", &devicenum);

		dev_major = (char *) list_nth(devicenum, 0);
		dev_minor = (char *) list_nth(devicenum, 1);

		snprintf(regex, lengthof(regex), "^\\s*%s\\s+%s\\s+", dev_major, dev_minor);

		/* extract device information */
		if (exec_grep(FILE_DISKSTATS, regex, &records) <= 0)
		{
			ereport(DEBUG2,
				(errmsg("device information of \"%s\" used by tablespace \"%s\" does not exist in \"%s\"",
					device, spcname, FILE_DISKSTATS)));
			prev_device = NULL;
			spclist = list_truncate(spclist, 0);
			continue;
		}

		record = b_trim((char *) list_nth(records, 0));

		nfield = exec_split(record, "\\s+", &fields);

		memset(nulls, 0, sizeof(nulls));
		memset(values, 0, sizeof(values));
		spclist = list_truncate(spclist, 0);

		if (nfield  == NUM_DISKSTATS_FIELDS)
		{
			/* device_major */
			values[0] = CStringGetTextDatum(dev_major);

			/* device_minor */
			values[1] = CStringGetTextDatum(dev_minor);

			/* device_name */
			dev_name = list_nth(fields, 2);
			values[2] = CStringGetTextDatum(dev_name);

			/* device_readsector */
			parse_int64(list_nth(fields, 5), &readsector);
			values[3] = Int64GetDatum(readsector);

			/* device_readtime */
			parse_int64(list_nth(fields, 6), &readtime);
			values[4] = Int64GetDatum(readtime);

			/* device_writesector */
			parse_int64(list_nth(fields, 9), &writesector);
			values[5] = Int64GetDatum(writesector);

			/* device_writetime */
			parse_int64(list_nth(fields, 10), &writetime);
			values[6] = Int64GetDatum(writetime);

			/* device_queue */
			parse_int64(list_nth(fields, 11), &ioqueue);
			values[7] = Int64GetDatum(ioqueue);

			/* device_iototaltime */
			parse_int64(list_nth(fields, 13), &iototaltime);
			values[8] = Int64GetDatum(iototaltime);
		}
		else if (nfield == NUM_DISKSTATS_PARTITION_FIELDS)
		{
			/* device_major */
			values[0] = CStringGetTextDatum(dev_major);

			/* device_minor */
			values[1] = CStringGetTextDatum(dev_minor);

			/* device_name */
			dev_name = list_nth(fields, 2);
			values[2] = CStringGetTextDatum(dev_name);

			/* device_readsector */
			parse_int64(list_nth(fields, 4), &readsector);
			values[3] = Int64GetDatum(readsector);

			/* device_readtime */
			nulls[4] = true;

			/* device_writesector */
			parse_int64(list_nth(fields, 6), &writesector);
			values[5] = Int64GetDatum(writesector);

			/* device_writetime */
			nulls[6] = true;

			/* device_queue */
			nulls[7] = true;

			/* device_iototaltime */
			nulls[8] = true;
		}
		else
			ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("unexpected file format: \"%s\"", FILE_DISKSTATS),
				 errdetail("number of fields is not corresponding")));

		/* set the overflow flag if value is smaller than previous value */
		prev_devicestats = search_devicestats(devicestats, dev_name);

		if (prev_devicestats)
		{
			int64 prev_readsector;
			int64 prev_readtime;
			int64 prev_writesector;
			int64 prev_writetime;
			int64 prev_iototaltime;
			bool isnull;

			prev_readsector = DatumGetInt64(GetAttributeByNum(prev_devicestats, 2, &isnull));
			prev_readtime = DatumGetInt64(GetAttributeByNum(prev_devicestats, 3, &isnull));
			prev_writesector = DatumGetInt64(GetAttributeByNum(prev_devicestats, 4, &isnull));
			prev_writetime = DatumGetInt64(GetAttributeByNum(prev_devicestats, 5, &isnull));
			prev_iototaltime = DatumGetInt64(GetAttributeByNum(prev_devicestats, 6, &isnull));

			/* overflow_drs */
			if (readsector < prev_readsector)
				values[9] = Int16GetDatum(1);
			else
				values[9] = Int16GetDatum(0);

			/* overflow_drt */
			if (nfield  == NUM_DISKSTATS_FIELDS && readtime < prev_readtime)
				values[10] = Int16GetDatum(1);
			else
				values[10] = Int16GetDatum(0);

			/* overflow_dws */
			if (writesector < prev_writesector)
				values[11] = Int16GetDatum(1);
			else
				values[11] = Int16GetDatum(0);

			/* overflow_dwt */
			if (nfield  == NUM_DISKSTATS_FIELDS && writetime < prev_writetime)
				values[12] = Int16GetDatum(1);
			else
				values[12] = Int16GetDatum(0);

			/* overflow_dit */
			if (nfield  == NUM_DISKSTATS_FIELDS && iototaltime < prev_iototaltime)
				values[13] = Int16GetDatum(1);
			else
				values[13] = Int16GetDatum(0);
		}
		else
		{
			/* overflow_drs */
			values[9] = Int16GetDatum(0);

			/* overflow_drt */
			values[10] = Int16GetDatum(0);

			/* overflow_dws */
			values[11] = Int16GetDatum(0);

			/* overflow_dwt */
			values[12] = Int16GetDatum(0);

			/* overflow_dit */
			values[13] = Int16GetDatum(0);
		}

		spclist = lappend(spclist, spcname);
		prev_device = device;
	}

	/* store the last tuple */
	if (list_length(spclist) > 0)
	{
		/* device_tblspaces */
		values[14] = BuildArrayType(spclist, TYPE_DEVICE_TABLESPACES, _CStringGetTextDatum);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
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

static void
checked_write(int fd, const void *buf, int size)
{
	if (write(fd, buf, size) != size)
	{
		int		save_errno = errno;

		close(fd);
		errno = save_errno ? save_errno : ENOSPC;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to pipe: %m")));
	}
}

static void
send_end(int fd)
{
	uint32	zero = 0;

	checked_write(fd, &zero, sizeof(zero));
}

static void
send_str(int fd, const char *key, const char *value)
{
	uint32	size;

	/* key */
	size = strlen(key);
	checked_write(fd, &size, sizeof(size));
	checked_write(fd, key, size);
	/* value */
	size = strlen(value);
	checked_write(fd, &size, sizeof(size));
	checked_write(fd, value, size);
}

static void
send_i32(int fd, const char *key, int value)
{
	char	buf[32];

	snprintf(buf, lengthof(buf), "%d", value);
	send_str(fd, key, buf);
}

static void
send_u64(int fd, const char *key, uint64 value)
{
	char	buf[32];

	snprintf(buf, lengthof(buf), UINT64_FORMAT, value);
	send_str(fd, key, buf);
}

/*
 * StartStatsinfoLauncher - Main entry point for pg_statsinfo launcher process.
 */
static void
StartStatsinfoLauncher(void)
{
	/*
	 * invoke pg_statsinfo launcher processs
	 */
	switch (fork_process())
	{
		case -1:
			ereport(LOG,
				(errmsg("could not fork pg_statsinfo launcher process: %m")));
			break;
		case 0:
			/* in child process */
			/* Lose the postmaster's on-exit routines */
			on_exit_reset();

			StatsinfoLauncherMain();
			break;
		default:
			break;
	}

	return;
}

#define LAUNCH_RETRY_PERIOD		300	/* sec */
#define LAUNCH_RETRY_MAX		5

/*
 * StatsinfoLauncherMain - Main loop for the pg_statsinfo launcher process.
 */
static void
StatsinfoLauncherMain(void)
{
	int			StatsinfoPID;
	int			launch_retry = 0;
	pg_time_t	launch_time;
	char		cmd[MAXPGPATH];

	/* we are postmaster subprocess now */
	IsUnderPostmaster = true;

	/* Identify myself via ps */
	init_ps_display("pg_statsinfo launcher process", "", "", "");

	/* delay for the preparation of syslogger */
	pg_usleep(1000000L);	/* 1s */

	ereport(LOG,
		(errmsg("pg_statsinfo launcher started")));

	/* Set up signal handlers */
	pqsignal(SIGHUP, sil_exit);
	pqsignal(SIGINT, sil_exit);
	pqsignal(SIGTERM, sil_exit);
	pqsignal(SIGQUIT, sil_exit);
	pqsignal(SIGALRM, sil_exit);
	pqsignal(SIGPIPE, sil_exit);
	pqsignal(SIGUSR1, sil_exit);
	pqsignal(SIGUSR2, sil_exit);
	pqsignal(SIGCHLD, sil_sigchld_handler);

	/* launch a pg_statsinfod process */
	StatsinfoPID = exec_background_process(cmd);
	launch_time = (pg_time_t) time(NULL);

	for (;;)
	{
		/* pg_statsinfo launcher quits either when the postmaster dies */
		if (!postmaster_is_alive())
			break;

		/* have received a signal that terminate process */
		if (need_exit)
			break;

		/* pg_statsinfod process died */
		if (got_SIGCHLD)
		{
			int status;

			waitpid(StatsinfoPID, &status, WNOHANG);

			/* pg_statsinfod normally end, terminate the pg_statsinfo launcher */
			if (status == 0)
				break;

			/* 
			 * if the pg_statsinfod was aborted with fatal error,
			 * then terminate the pg_statsinfo launcher
			 */
			if (WIFEXITED(status) && WEXITSTATUS(status) == STATSINFO_EXIT_FAILED)
			{
				ereport(WARNING,
					(errmsg("pg_statsinfod is aborted with fatal error, "
							"terminate the pg_statsinfo launcher")));
				break;
			}

			/* pg_statsinfod abnormally end, relaunch new pg_statsinfod process */
			ereport(WARNING,
				(errmsg("pg_statsinfod is aborted")));

			/* 
			 * if the pg_statsinfod was aborted continuously,
			 * then terminate the pg_statsinfo launcher
			 */
			if (((pg_time_t) time(NULL) - launch_time) <= LAUNCH_RETRY_PERIOD)
			{
				if (launch_retry >= LAUNCH_RETRY_MAX)
				{
					ereport(WARNING,
					(errmsg("pg_statsinfod is aborted continuously, "
							"terminate the pg_statsinfo launcher")));
					break;
				}
			}
			else
				launch_retry = 0;

			ereport(LOG,
				(errmsg("relaunch a pg_statsinfod process")));

			got_SIGCHLD = false;
			StatsinfoPID = exec_background_process(cmd);
			launch_time = (pg_time_t) time(NULL);

			launch_retry++;
		}

		pg_usleep(100000L);		/* 100ms */
	}

	/* Normal exit from the pg_statsinfo launcher is here */
	ereport(LOG,
		(errmsg("pg_statsinfo launcher shutting down")));

	proc_exit(0);
}

/*
 * exec_background_process - Start statsinfo background process.
 */
static pid_t
exec_background_process(char cmd[])
{
	char		binpath[MAXPGPATH];
	char		share_path[MAXPGPATH];
	uint64		sysident;
	int			fd;
	pid_t		fpid;
	pid_t		postmaster_pid = get_postmaster_pid();
	pg_time_t	log_ts;
	pg_tz	   *log_tz;

	log_ts = (pg_time_t) time(NULL);
	log_tz = pg_tzset(GetConfigOption("log_timezone", false));

	/* $PGHOME/bin */
	strlcpy(binpath, my_exec_path, MAXPGPATH);
	get_parent_directory(binpath);

	/* $PGHOME/share */
	get_share_path(my_exec_path, share_path);

	/* ControlFile: system_identifier */
	sysident = get_sysident();

	/* Make command line. Add postmaster pid only for ps display */
	snprintf(cmd, MAXPGPATH, "%s/%s %d", binpath, PROGRAM_NAME, postmaster_pid);

	/* Execute a background process. */
	fpid = forkexec(cmd, &fd);
	if (fpid == 0 || fd < 0)
		elog(ERROR, LOG_PREFIX "could not execute background process");

	/* send GUC variables to background process. */
	send_u64(fd, "instance_id", sysident);
	send_i32(fd, "postmaster_pid", postmaster_pid);
	send_str(fd, "port", GetConfigOption("port", false));
	send_str(fd, "server_version_num", GetConfigOption("server_version_num", false));
	send_str(fd, "server_version_string", GetConfigOption("server_version", false));
	send_str(fd, "share_path", share_path);
	send_i32(fd, "server_encoding", GetDatabaseEncoding());
	send_str(fd, "data_directory", DataDir);
	send_str(fd, "log_timezone", pg_localtime(&log_ts, log_tz)->tm_zone);
	send_str(fd, ":debug", _("DEBUG"));
	send_str(fd, ":info", _("INFO"));
	send_str(fd, ":notice", _("NOTICE"));
	send_str(fd, ":log", _("LOG"));
	send_str(fd, ":warning", _("WARNING"));
	send_str(fd, ":error", _("ERROR"));
	send_str(fd, ":fatal", _("FATAL"));
	send_str(fd, ":panic", _("PANIC"));
	send_str(fd, ":shutdown", _(MSG_SHUTDOWN));
	send_str(fd, ":shutdown_smart", _(MSG_SHUTDOWN_SMART));
	send_str(fd, ":shutdown_fast", _(MSG_SHUTDOWN_FAST));
	send_str(fd, ":shutdown_immediate", _(MSG_SHUTDOWN_IMMEDIATE));
	send_str(fd, ":sighup", _(MSG_SIGHUP));
	send_str(fd, ":autovacuum", _(MSG_AUTOVACUUM));
	send_str(fd, ":autoanalyze", _(MSG_AUTOANALYZE));
	send_str(fd, ":checkpoint_starting", _(MSG_CHECKPOINT_STARTING));
	send_str(fd, ":checkpoint_complete", _(MSG_CHECKPOINT_COMPLETE));
	send_end(fd);
	close(fd);

	return fpid;
}

/* SIGCHLD: pg_statsinfod process died */
static void
sil_sigchld_handler(SIGNAL_ARGS)
{
	got_SIGCHLD = true;
}

static void
sil_exit(SIGNAL_ARGS)
{
	need_exit = true;
}

/*
 * Read control file. We cannot retrieve it from "Control File" shared memory
 * because the shared memory might not be initialized yet.
 */
static uint64
get_sysident(void)
{
	ControlFileData	ctrl;

	if (!readControlFile(&ctrl, DataDir))
		elog(ERROR,
			LOG_PREFIX "could not read control file: %m");

	return ctrl.system_identifier;
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
	HeapScanDesc		scan;
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

	relation = heap_open(TableSpaceRelationId, AccessShareLock);

	scan = heap_beginscan(relation, SnapshotNow, 0, NULL);
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Form_pg_tablespace form = (Form_pg_tablespace) GETSTRUCT(tuple);
		Datum			datum;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		i = 0;

		/* oid */
		values[i++] = ObjectIdGetDatum(HeapTupleGetOid(tuple));

		/* name */
		values[i++] = CStringGetTextDatum(NameStr(form->spcname));

		/* location */
		if (HeapTupleGetOid(tuple) == DEFAULTTABLESPACE_OID ||
			HeapTupleGetOid(tuple) == GLOBALTABLESPACE_OID)
			datum = CStringGetTextDatum(DataDir);
		else
		{
#if PG_VERSION_NUM >= 90200
			datum = DirectFunctionCall1(pg_tablespace_location,
										ObjectIdGetDatum(HeapTupleGetOid(tuple)));
#else
			bool isnull;
			datum = fastgetattr(tuple, Anum_pg_tablespace_spclocation,
								RelationGetDescr(relation), &isnull);
			/* resolve symlink */
			if ((len = readlink(TextDatumGetCString(datum),
								location, lengthof(location))) > 0)
			{
				location[len] = '\0';
				datum = CStringGetTextDatum(location);
			}
#endif
		}
		values[i++] = datum;

		/* device */
		i += get_devinfo(TextDatumGetCString(datum), values + i, nulls + i);

		/* spcoptions */
#if PG_VERSION_NUM >= 90000
		values[i] = fastgetattr(tuple, Anum_pg_tablespace_spcoptions,
								  RelationGetDescr(relation), &nulls[i]);
		i++;
#else
		nulls[i++] = true;
#endif

		Assert(i == TABLESPACES_COLS);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	heap_endscan(scan);

	heap_close(relation, AccessShareLock);

	/* append pg_xlog if symlink */
	join_path_components(pg_xlog, DataDir, "pg_xlog");
	if ((len = readlink(pg_xlog, location, lengthof(location))) > 0)
	{
		location[len] = '\0';
		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));
		i = 0;

		nulls[i++] = true;
		values[i++] = CStringGetTextDatum("<pg_xlog>");
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
		values[i++] = CStringGetTextDatum("<pg_xlog_archive>");
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

static int
get_log_min_messages(void)
{
#ifndef WIN32
	return log_min_messages;
#else
	/*
	 * log_min_messages is not available on Windows because the variable is
	 * not dllexport'ed. Instead, reparse config option text.
	 */
	return str_to_elevel("log_min_messages",
						 GetConfigOption("log_min_messages", false),
						 server_message_level_options);
#endif
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

#if PG_VERSION_NUM >= 90100
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
	int			 mode = 0x00;
	char		 mode_string[32];

	if (parse_bool(*newval, &bool_val))
	{
		if (bool_val)
		{
			mode |= MAINTENANCE_MODE_SNAPSHOT;
			mode |= MAINTENANCE_MODE_LOG;
		}
		snprintf(mode_string, sizeof(mode_string), "%d", mode);
		*newval = strdup(mode_string);
		return true;
	}

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

		if (pg_strcasecmp(tok, "snapshot") == 0)
			mode |= MAINTENANCE_MODE_SNAPSHOT;
		else if (pg_strcasecmp(tok, "log") == 0)
			mode |= MAINTENANCE_MODE_LOG;
		else
		{
			GUC_check_errdetail(GUC_PREFIX ".enable_maintenance unrecognized keyword: \"%s\"", tok);
			goto error;
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	snprintf(mode_string, sizeof(mode_string), "%d", mode);
	*newval = strdup(mode_string);
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
#else
/* forbid empty filename and reserved characters */
static const char *
assign_textlog_filename(const char *newval, bool doit, GucSource source)
{
	if (!newval[0])
	{
		ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".textlog_filename must not be emtpy")));
		return NULL;
	}
	if (strpbrk(newval, "/\\?*:|\"<>"))
	{
		ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".textlog_filename must not contain reserved characters: %s", newval)));
		return NULL;
	}

	return newval;
}

/* forbid unrecognized keyword for maintenance mode */
static const char *
assign_enable_maintenance(const char *newval, bool doit, GucSource source)
{
	char		*rawstring;
	List		*elemlist;
	ListCell	*cell;
	bool		 bool_val;
	int			 mode = 0x00;
	char		 mode_string[32];

	if (parse_bool(newval, &bool_val))
	{
		if (bool_val)
		{
			mode |= MAINTENANCE_MODE_SNAPSHOT;
			mode |= MAINTENANCE_MODE_LOG;
		}
		snprintf(mode_string, sizeof(mode_string), "%d", mode);
		return strdup(mode_string);
	}

	/* Need a modifiable copy of string */
	rawstring = pstrdup(newval);

	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		pfree(rawstring);
		list_free(elemlist);
		ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".enable_maintenance list syntax is invalid")));
		return NULL;
	}

	foreach(cell, elemlist)
	{
		char *tok = (char *) lfirst(cell);

		if (pg_strcasecmp(tok, "snapshot") == 0)
			mode |= MAINTENANCE_MODE_SNAPSHOT;
		else if (pg_strcasecmp(tok, "log") == 0)
			mode |= MAINTENANCE_MODE_LOG;
		else
		{
			pfree(rawstring);
			list_free(elemlist);
			ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".enable_maintenance unrecognized keyword: \"%s\"", tok)));
			return NULL;
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	snprintf(mode_string, sizeof(mode_string), "%d", mode);
	return strdup(mode_string);
}

/* forbid empty and invalid time format */
static const char *
assign_maintenance_time(const char *newval, bool doit, GucSource source)
{
	if (!newval[0])
	{
		ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".maintenance_time must not be emtpy, use default (\"%s\")",
				 	DEFAULT_MAINTENANCE_TIME)));
		return NULL;
	}
	if (!verify_timestr(newval))
	{
		ereport(GUC_complaint_elevel(source),
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg(GUC_PREFIX ".maintenance_time invalid syntax for time: %s, use default (\"%s\")",
				 	newval, DEFAULT_MAINTENANCE_TIME),
				 errhint("format should be [hh:mm:ss]")));
		return NULL;
	}

	return newval;
}
#endif

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

#if PG_VERSION_NUM < 80400 || defined(WIN32)
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

#if PG_VERSION_NUM < 80400
static const char *
assign_syslog_min_messages(const char *newval, bool doit, GucSource source)
{
	return (assign_elevel("syslog_min_messages", &syslog_min_messages, newval, doit));
}

static const char *
assign_textlog_min_messages(const char *newval, bool doit, GucSource source)
{
	return (assign_elevel("textlog_min_messages", &textlog_min_messages, newval, doit));
}

static const char *
assign_elevel(const char *name, int *var, const char *newval, bool doit)
{
	int		value = str_to_elevel(name, newval, elevel_options);

	if (doit)
		(*var) = value;
	return newval;	/* OK */
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

#if PG_VERSION_NUM >= 90100
	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED, DEFAULT_COLLATION_OID);
#else
	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED);
#endif
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

#if PG_VERSION_NUM >= 90100
	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED, DEFAULT_COLLATION_OID);
#else
	ret = pg_regcomp(&reg_t, pattern, pattern_len, REG_ADVANCED);
#endif
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

#if PG_VERSION_NUM < 80400
/*
 * Try to interpret value as boolean value.  Valid values are: true,
 * false, yes, no, on, off, 1, 0; as well as unique prefixes thereof.
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 */
static bool
parse_bool(const char *value, bool *result)
{
	size_t		len = strlen(value);

	if (pg_strncasecmp(value, "true", len) == 0)
	{
		if (result)
			*result = true;
	}
	else if (pg_strncasecmp(value, "false", len) == 0)
	{
		if (result)
			*result = false;
	}

	else if (pg_strncasecmp(value, "yes", len) == 0)
	{
		if (result)
			*result = true;
	}
	else if (pg_strncasecmp(value, "no", len) == 0)
	{
		if (result)
			*result = false;
	}

	/* 'o' is not unique enough */
	else if (pg_strncasecmp(value, "on", (len > 2 ? len : 2)) == 0)
	{
		if (result)
			*result = true;
	}
	else if (pg_strncasecmp(value, "off", (len > 2 ? len : 2)) == 0)
	{
		if (result)
			*result = false;
	}

	else if (pg_strcasecmp(value, "1") == 0)
	{
		if (result)
			*result = true;
	}
	else if (pg_strcasecmp(value, "0") == 0)
	{
		if (result)
			*result = false;
	}

	else
	{
		if (result)
			*result = false; /* suppress compiler warning */
		return false;
	}
	return true;
}
#endif

/*
 * Note: this function modify the argument string
 */
static char *
b_trim(char *str)
{
	size_t	 len;
	char	*start;

	if (str == NULL)
		return NULL;

	/* remove space character from prefix */
	len = strlen(str);
	while (len > 0 && isspace(str[len - 1])) { len--; }
	str[len] = '\0';

	/* remove space character from suffix */
	start = str;
	while (isspace(start[0])) { start++; }
	memmove(str, start, strlen(start) + 1);

	return str;
}

static Datum
BuildArrayType(List *values, Oid elmtype, Datum(*convert)(void *))
{
	ArrayType	*array_t;
	ListCell	*cell;
	Datum		*elems;
	int16		 typlen;
	bool		 typbyval;
	char		 typalign;
	int			 val_size;
	int			 i;

	val_size = list_length(values);
	get_typlenbyvalalign(elmtype, &typlen, &typbyval, &typalign);

	elems = palloc(sizeof(Datum) * val_size + 1);

	i = 0;
	foreach(cell, values)
		elems[i++] = convert(lfirst(cell));

	array_t = construct_array(elems, val_size, elmtype, typlen, typbyval, typalign);
	return PointerGetDatum(array_t);
}

static Datum
_CStringGetTextDatum(void *ptr)
{
	return CStringGetTextDatum((char *) ptr);
}

static HeapTupleHeader
search_devicestats(ArrayType *devicestats, const char *device_name)
{
	int16	 elmlen;
	bool	 elmbyval;
	char	 elmalign;
	Datum	*elems;
	bool	*elemnulls;
	int		 nelems;
	int		 i;

	if (devicestats == NULL || device_name == NULL)
		return NULL;

	get_typlenbyvalalign(
		ARR_ELEMTYPE(devicestats), &elmlen, &elmbyval, &elmalign);

	deconstruct_array(devicestats, ARR_ELEMTYPE(devicestats),
			elmlen, elmbyval, elmalign, &elems, &elemnulls, &nelems);

	for (i = 0; i < nelems; i++)
	{
		HeapTupleHeader tuple = (HeapTupleHeader) elems[i];
		char *dev_name;
		bool isnull;

		dev_name = TextDatumGetCString(GetAttributeByNum(tuple, 1, &isnull));
		if (strcmp(device_name, dev_name) == 0)
			return tuple;
	}
	/* not found */
	return NULL;
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
