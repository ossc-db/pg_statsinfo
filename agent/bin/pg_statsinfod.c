/*
 * pg_statsinfod.c
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#include <signal.h>

#include "miscadmin.h"

const char *PROGRAM_VERSION	= "2.3.0";
const char *PROGRAM_URL		= "http://pgstatsinfo.projects.postgresql.org/";
const char *PROGRAM_EMAIL	= "pgstatsinfo-general@pgfoundry.org";

typedef struct ParamMap
{
	char	   *name;
	bool	  (*assign)(const char *value, void *var);
	void	   *value;
} ParamMap;

/*---- system variables ----*/
char		   *instance_id;			/* system identifier */
static pid_t	postmaster_pid;			/* postmaster's pid */
char		   *postmaster_port;		/* postmaster port number as string */
static char	   *share_path;				/* $PGHOME/share */
static char	   *prev_csv_name;			/* virtual previous csv name */
int				server_version_num;		/* PG_VERSION_NUM */
char		   *server_version_string;	/* PG_VERSION */
int				server_encoding = -1;	/* server character encoding */
char		   *log_timezone_name;
/*---- GUC variables (collector) -------*/
char		   *data_directory;
char		   *excluded_dbnames;
char		   *excluded_schemas;
char		   *stat_statements_max;
char		   *stat_statements_exclude;
int				sampling_interval;
int				snapshot_interval;
/*---- GUC variables (logger) ----------*/
char		   *log_directory;
char		   *log_error_verbosity;
int				syslog_facility;
char		   *syslog_ident;
char		   *syslog_line_prefix;
int				syslog_min_messages;
char		   *textlog_filename;
char		   *textlog_line_prefix;
int				textlog_min_messages;
int				textlog_permission;
bool			adjust_log_level;
char		   *adjust_log_info;
char		   *adjust_log_notice;
char		   *adjust_log_warning;
char		   *adjust_log_error;
char		   *adjust_log_log;
char		   *adjust_log_fatal;
/*---- GUC variables (writer) ----------*/
char		   *repository_server;
bool		    enable_maintenance;
time_t			maintenance_time;
int				repository_keepday;
/*---- message format ----*/
char		   *msg_debug;
char		   *msg_info;
char		   *msg_notice;
char		   *msg_log;
char		   *msg_warning;
char		   *msg_error;
char		   *msg_fatal;
char		   *msg_panic;
char		   *msg_shutdown;
char		   *msg_shutdown_smart;
char		   *msg_shutdown_fast;
char		   *msg_shutdown_immediate;
char		   *msg_sighup;
char		   *msg_autovacuum;
char		   *msg_autoanalyze;
char		   *msg_checkpoint_starting;
char		   *msg_checkpoint_complete;
size_t			checkpoint_starting_prefix_len;
/*--------------------------------------*/

/* current shutdown state */
volatile ShutdownState	shutdown_state;
bool					shutdown_message_found;
static pthread_mutex_t	shutdown_state_lock;

/* threads */
pthread_t	th_collector;
pthread_t	th_logger;
pthread_t	th_writer;

static int help(void);
static bool ensure_schema(PGconn *conn, const char *schema);
static bool assign_int(const char *value, void *var);
static bool assign_elevel(const char *value, void *var);
static bool assign_syslog(const char *value, void *var);
static bool assign_string(const char *value, void *var);
static bool assign_bool(const char *value, void *var);
static bool assign_time(const char *value, void *var);
static void after_readopt(void);
static bool decode_time(const char *field, int *hour, int *min, int *sec);
static int strtoi(const char *nptr, char **endptr, int base);
static bool execute_script(PGconn *conn, const char *script_file);

/* parameters */
static struct ParamMap PARAM_MAP[] =
{
	{"instance_id", assign_string, &instance_id},
	{"postmaster_pid", assign_int, &postmaster_pid},
	{"port", assign_string, &postmaster_port},
	{"share_path", assign_string, &share_path},
	{"prev_csv_name", assign_string, &prev_csv_name},
	{"server_version_num", assign_int, &server_version_num},
	{"server_version_string", assign_string, &server_version_string},
	{"server_encoding", assign_int, &server_encoding},
	{"data_directory", assign_string, &data_directory},
	{"log_timezone", assign_string, &log_timezone_name},
	{"log_directory", assign_string, &log_directory},
	{"log_error_verbosity", assign_string, &log_error_verbosity},
	{"syslog_facility", assign_syslog, &syslog_facility},
	{"syslog_ident", assign_string, &syslog_ident},
	{GUC_PREFIX ".excluded_dbnames", assign_string, &excluded_dbnames},
	{GUC_PREFIX ".excluded_schemas", assign_string, &excluded_schemas},
	{GUC_PREFIX ".stat_statements_max", assign_string, &stat_statements_max},
	{GUC_PREFIX ".stat_statements_exclude", assign_string, &stat_statements_exclude},
	{GUC_PREFIX ".repository_server", assign_string, &repository_server},
	{GUC_PREFIX ".sampling_interval", assign_int, &sampling_interval},
	{GUC_PREFIX ".snapshot_interval", assign_int, &snapshot_interval},
	{GUC_PREFIX ".syslog_line_prefix", assign_string, &syslog_line_prefix},
	{GUC_PREFIX ".syslog_min_messages", assign_elevel, &syslog_min_messages},
	{GUC_PREFIX ".textlog_min_messages", assign_elevel, &textlog_min_messages},
	{GUC_PREFIX ".textlog_filename", assign_string, &textlog_filename},
	{GUC_PREFIX ".textlog_line_prefix", assign_string, &textlog_line_prefix},
	{GUC_PREFIX ".textlog_permission", assign_int, &textlog_permission},
	{GUC_PREFIX ".adjust_log_level", assign_bool, &adjust_log_level},
	{GUC_PREFIX ".adjust_log_info", assign_string, &adjust_log_info},
	{GUC_PREFIX ".adjust_log_notice", assign_string, &adjust_log_notice},
	{GUC_PREFIX ".adjust_log_warning", assign_string, &adjust_log_warning},
	{GUC_PREFIX ".adjust_log_error", assign_string, &adjust_log_error},
	{GUC_PREFIX ".adjust_log_log", assign_string, &adjust_log_log},
	{GUC_PREFIX ".adjust_log_fatal", assign_string, &adjust_log_fatal},
	{GUC_PREFIX ".enable_maintenance", assign_bool, &enable_maintenance},
	{GUC_PREFIX ".maintenance_time", assign_time, &maintenance_time},
	{GUC_PREFIX ".repository_keepday", assign_int, &repository_keepday},
	{":debug", assign_string, &msg_debug},
	{":info", assign_string, &msg_info},
	{":notice", assign_string, &msg_notice},
	{":log", assign_string, &msg_log},
	{":warning", assign_string, &msg_warning},
	{":error", assign_string, &msg_error},
	{":fatal", assign_string, &msg_fatal},
	{":panic", assign_string, &msg_panic},
	{":shutdown", assign_string, &msg_shutdown},
	{":shutdown_smart", assign_string, &msg_shutdown_smart},
	{":shutdown_fast", assign_string, &msg_shutdown_fast},
	{":shutdown_immediate", assign_string, &msg_shutdown_immediate},
	{":sighup", assign_string, &msg_sighup},
	{":autovacuum", assign_string, &msg_autovacuum},
	{":autoanalyze", assign_string, &msg_autoanalyze},
	{":checkpoint_starting", assign_string, &msg_checkpoint_starting},
	{":checkpoint_complete", assign_string, &msg_checkpoint_complete},
	{NULL}
};

static int
isTTY(int fd)
{
#ifndef WIN32
	return isatty(fd);
#else
	return !GetNamedPipeInfo((HANDLE) _get_osfhandle(fd), NULL, NULL, NULL, NULL);
#endif
}

int
main(int argc, char *argv[])
{
	shutdown_state = STARTUP;

	pgut_init(argc, argv);

	/* stdin must be pipe from server */
	if (isTTY(fileno(stdin)))
		return help();

	/* read required parameters from stdin */
	readopt_from_file(stdin);
	fclose(stdin);
	if (instance_id == NULL ||
		postmaster_pid == 0 ||
		postmaster_port == NULL ||
		!pg_valid_server_encoding_id(server_encoding) ||
		data_directory == NULL ||
		log_directory == NULL ||
		share_path == NULL ||
		prev_csv_name == NULL ||
		msg_shutdown == NULL ||
		msg_shutdown_smart == NULL ||
		msg_shutdown_fast == NULL ||
		msg_shutdown_immediate == NULL ||
		msg_sighup == NULL ||
		msg_autovacuum == NULL ||
		msg_autoanalyze == NULL)
	{
		ereport(FATAL,
			(errcode(EINVAL),
			 errmsg("cannot run without required parameters")));
	}

	/* check major version */
	if (server_version_num / 100 != PG_VERSION_NUM / 100)
	{
		ereport(FATAL,
			(errcode(EINVAL),
			 errmsg("incompatible server: version mismatch"),
			 errdetail("Server is version %d, %s was built with version %d",
					   server_version_num, PROGRAM_NAME, PG_VERSION_NUM)));
	}

#if defined(USE_DAEMON) && !defined(WIN32)
	/*
	 * Run as a daemon to avoid postmaster's crash even if the statsinfo
	 * process will crash.
	 */
	if (daemon(true, false) != 0)
		ereport(PANIC,
			(errcode_errno(),
			 errmsg("could not run as a daemon: ")));
#endif

	/* setup libpq default parameters */
	pgut_putenv("PGCONNECT_TIMEOUT", "2");
	pgut_putenv("PGCLIENTENCODING", pg_encoding_to_char(server_encoding));

	/*
	 * set the abort level to FATAL so that the daemon should not be
	 * terminated by ERRORs.
	 */
	pgut_abort_level = FATAL;

	/* init logger, collector, and writer module */
	pthread_mutex_init(&shutdown_state_lock, NULL);
	collector_init();
	logger_init();
	writer_init();

	/* run the modules in each thread */
	shutdown_state = RUNNING;
	elog(LOG, "start");
	pthread_create(&th_collector, NULL, collector_main, NULL);
	pthread_create(&th_writer, NULL, writer_main, NULL);
	pthread_create(&th_logger, NULL, logger_main, prev_csv_name);

	/* join the threads */ 
	pthread_join(th_collector, NULL);
	pthread_join(th_writer, NULL);
	pthread_join(th_logger, NULL);

#ifdef NOT_USED
	if (!shutdown_message_found)
		restart_postmaster();		/* postmaster might be crashed! */
#endif

	return 0;
}

static int
help(void)
{
	printf("%s %s (built with %s)\n",
		PROGRAM_NAME, PROGRAM_VERSION, PACKAGE_STRING);
	printf("  This program must be launched by PostgreSQL server.\n");
	printf("  Add 'pg_statsinfo' to shared_preload_libraries in postgresql.conf.\n");
	printf("\n");
	printf("Read the website for details. <%s>\n", PROGRAM_URL);
	printf("Report bugs to <%s>.\n", PROGRAM_EMAIL);

	return 1;
}

bool
postmaster_is_alive(void)
{
#ifdef WIN32
	static HANDLE hProcess = NULL;
	
	if (hProcess == NULL)
	{
		hProcess = OpenProcess(SYNCHRONIZE, false, postmaster_pid);
		if (hProcess == NULL)
			elog(WARNING, "cannot open process (pid=%u): %d", postmaster_pid, GetLastError());
	}
	return WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT;
#else
	return kill(postmaster_pid, 0) == 0;
#endif
}

/*
 * convert an error level string to an enum value.
 */
int
str_to_elevel(const char *value)
{
	if (msg_debug)
	{
		if (pg_strcasecmp(value, msg_debug) == 0)
			return DEBUG2;
		else if (pg_strcasecmp(value, msg_info) == 0)
			return INFO;
		else if (pg_strcasecmp(value, msg_notice) == 0)
			return NOTICE;
		else if (pg_strcasecmp(value, msg_log) == 0)
			return LOG;
		else if (pg_strcasecmp(value, msg_warning) == 0)
			return WARNING;
		else if (pg_strcasecmp(value, msg_error) == 0)
			return ERROR;
		else if (pg_strcasecmp(value, msg_fatal) == 0)
			return FATAL;
		else if (pg_strcasecmp(value, msg_panic) == 0)
			return PANIC;
	}

	if (pg_strcasecmp(value, "ALERT") == 0)
		return ALERT;
	else if (pg_strcasecmp(value, "DISABLE") == 0)
		return DISABLE;
	else
		return parse_elevel(value);
}

/* additionally support ALERT and DISABLE */
const char *
elevel_to_str(int elevel)
{
	if (msg_debug)
	{
		switch (elevel)
		{
		case DEBUG5:
		case DEBUG4:
		case DEBUG3:
		case DEBUG2:
		case DEBUG1:
			return msg_debug;
		case LOG:
			return msg_log;
		case INFO:
			return msg_info;
		case NOTICE:
			return msg_notice;
		case WARNING:
			return msg_warning;
		case COMMERROR:
		case ERROR:
			return msg_error;
		case FATAL:
			return msg_fatal;
		case PANIC:
			return msg_panic;
		}
	}

	switch (elevel)
	{
	case ALERT:
		return "ALERT";
	case DISABLE:
		return "DISABLE";
	default:
		return format_elevel(elevel);
	}
}

/*
 * Connect to the database and install schema if not installed yet.
 * Returns the same value with *conn.
 */
PGconn *
do_connect(PGconn **conn, const char *info, const char *schema)
{
	/* skip reconnection if connected to the database already */
	if (PQstatus(*conn) == CONNECTION_OK)
		return *conn;

	pgut_disconnect(*conn);
	*conn = pgut_connect(info, NO, DEBUG2);

	if (PQstatus(*conn) == CONNECTION_OK)
	{
		/* adjust setting parameters */
		pgut_command(*conn,
			"SET search_path = 'pg_catalog', 'public'", 0, NULL);

		/* install required schema if requested */
		if (ensure_schema(*conn, schema))
			return *conn;
	}

	/* connection failed */
	pgut_disconnect(*conn);
	*conn = NULL;
	return NULL;
}

/*
 * requires $PGDATA/contrib/pg_{schema}.sql
 */
static bool
ensure_schema(PGconn *conn, const char *schema)
{
	PGresult	   *res;
	bool			ok;
	char			path[MAXPGPATH];

	if (!schema || !schema[0])
		return true;	/* no schema required */

	/* check statsinfo schema exists */
	res = pgut_execute(conn,
			"SELECT nspname FROM pg_namespace WHERE nspname = $1",
			1, &schema);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		return false;
	}
	ok = (PQntuples(res) > 0);
	PQclear(res);
	/* TODO: check installed schema version */
	if (ok)
		return true;	/* ok, installed */

	/* iff schema is "statsrepo", check repository server version */
	if (strcmp(schema, "statsrepo") == 0)
	{
		int		server_version;
		bool	installed;

		server_version = get_server_version(conn);

		if (server_version < 0)
			return false;
		else if (server_version >= 80400)
			/* sets repository schema for partitioning */
			schema = "statsrepo_partition";
		else
			schema = "statsrepo83";

		/* create language 'PL/pgSQL' */
		res = pgut_execute(conn,
			"SELECT 1 FROM pg_language WHERE lanname = 'plpgsql'", 0, NULL);
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			PQclear(res);
			return false;
		}
		installed = (PQntuples(res) > 0);
		PQclear(res);
		if (!installed)
		{
			if (pgut_command(conn,
				"CREATE LANGUAGE plpgsql", 0, NULL) != PGRES_COMMAND_OK)
				return false;
		}
	}

	/* execute script $PGSHARE/contrib/pg_{schema}.sql */
	snprintf(path, MAXPGPATH, "%s/contrib/pg_%s.sql", share_path, schema);
	elog(LOG, "installing schema: %s", schema);
	if (!execute_script(conn, path))
		return false;

	/* execute script $PGSHARE/contrib/pg_statsrepo.alert.sql */
	if (strstr(schema, "statsrepo") != NULL)
	{
		snprintf(path, MAXPGPATH, "%s/contrib/pg_statsrepo_alert.sql", share_path);
		if (!execute_script(conn, path))
			return false;
	}
	return true;
}

/*
 * set shutdown state
 */
void
shutdown_progress(ShutdownState state)
{
	pthread_mutex_lock(&shutdown_state_lock);

	if (shutdown_state < state)
		shutdown_state = state;

	pthread_mutex_unlock(&shutdown_state_lock);
}

static bool
assign_int(const char *value, void *var)
{
	return parse_int32(value, (int32 *) var);
}

static bool
assign_bool(const char *value, void *var)
{
	return parse_bool(value, var);
}

static bool
assign_time(const char *value, void *var)
{
	struct tm	*tm;
	time_t		 now;
	int			 hour, min, sec;

	if (!decode_time(value, &hour, &min, &sec))
		return false;

	now = time(NULL);
	tm = localtime(&now);
	tm->tm_hour = hour;
	tm->tm_min = min;
	tm->tm_sec = sec;

	*(time_t *)var = mktime(tm);
	return true;
}

static bool
assign_elevel(const char *value, void *var)
{
	*((int *) var) = str_to_elevel(value);
	return true;
}

static bool
assign_syslog(const char *value, void *var)
{
	sscanf(value, "local%d", (int *) var);
	if (*(int *) var < 0 || 7 < *(int *) var)
		*(int *) var = 0;
	return true;
}

static bool
assign_string(const char *value, void *var)
{
	free(*(char **)var);
	*(char **)var = pgut_strdup(value);
	return true;
}

static bool
assign_param(const char *name, const char *value)
{
	ParamMap   *param;

	for (param = PARAM_MAP; param->name; param++)
	{
		if (strcmp(name, param->name) == 0)
			break;
	}
	if (param->name == NULL || !param->assign(value, param->value))
	{
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("unexpected parameter: %s = %s", name, value)));
		return false;
	}

	return true;
}

/*
 * Assign options from file. The file format must be:
 *	uint32	name_size
 *	char	name[name_size]
 *	uint32	value_size
 *	char	value[value_size]
 */
void
readopt_from_file(FILE *fp)
{
	StringInfoData	name;
	StringInfoData	value;

	initStringInfo(&name);
	initStringInfo(&value);

	for (;;)
	{
		uint32			size;

		resetStringInfo(&name);
		resetStringInfo(&value);

		/* name-size */
		if (fread(&size, 1, sizeof(size), fp) != sizeof(size))
			goto error;
		if (size == 0)
			goto done;	/* EOF */

		/* name */
		enlargeStringInfo(&name, size);
		if (fread(name.data, 1, size, fp) != size)
			goto error;
		name.data[name.len = size] = '\0';

		/* value-size */
		if (fread(&size, 1, sizeof(size), fp) != sizeof(size))
			goto error;

		/* value */
		enlargeStringInfo(&value, size);
		if (fread(value.data, 1, size, fp) != size)
			goto error;
		value.data[value.len = size] = '\0';

		assign_param(name.data, value.data);
	}

error:
	ereport(ERROR,
		(errcode(EINVAL),
		 errmsg("invalid option file")));
done:
	termStringInfo(&name);
	termStringInfo(&value);

	after_readopt();
}

/*
 * format of res must be (name text, value text).
 */
void
readopt_from_db(PGresult *res)
{
	int			r;
	int			rows;

	rows = PQntuples(res);
	for (r = 0; r < rows; r++)
	{
		const char *name = PQgetvalue(res, r, 0);
		const char *value = PQgetvalue(res, r, 1);

		assign_param(name, value);
	}

	after_readopt();
}

/*
 * called after parameters are reloaded.
 */
static void
after_readopt(void)
{
	/*
	 * We hope the message format for checkpoint starting ends with
	 * "%s%s%s%s%s%s%s" on all locales.
	 */
	checkpoint_starting_prefix_len = 0;
	if (msg_checkpoint_starting)
	{
		const char *flags = strstr(msg_checkpoint_starting, "%s%s%s%s%s%s%s");
		if (flags)
			checkpoint_starting_prefix_len = flags - msg_checkpoint_starting;
	}

	if (!enable_maintenance)
		elog(NOTICE,
			"automatic maintenance is disable. Please note the data size of the repository");
}

/*
 * delay unless shutdown is requested.
 */
void
delay(void)
{
	if (shutdown_state < SHUTDOWN_REQUESTED)
		sleep(1);
}

/*
 * get local timestamp by character string
 * return format : "YYYY-MM-DD HH:MM:SS.FFFFFF"
 */
char *
getlocaltimestamp(void)
{
#ifndef WIN32
	struct timeval	 tv;
	struct tm		*ts;
#else
	SYSTEMTIME		 stTime;
#endif
	char			*tp;

	if ((tp = (char *)pgut_malloc((size_t)32)) == NULL)
		return NULL;

	memset(tp, 0x00, 32);

#ifndef WIN32
	if (gettimeofday(&tv, NULL) != 0)
	{
		free(tp);
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("gettimeofday function call failed")));
		return NULL;
	}

	if ((ts = localtime(&tv.tv_sec)) == NULL)
	{
		free(tp);
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("localtime function call failed")));
		return NULL;
	}

	snprintf(tp, 32, "%04d-%02d-%02d %02d:%02d:%02d.%ld",
			ts->tm_year + 1900,
			ts->tm_mon + 1,
			ts->tm_mday,
			ts->tm_hour,
			ts->tm_min,
			ts->tm_sec,
			tv.tv_usec);
#else
	GetLocalTime(&stTime);

	snprintf(tp, 32, "%04d-%02d-%02d %02d:%02d:%02d.%ld",
			stTime.wYear,
			stTime.wMonth,
			stTime.wDay,
			stTime.wHour,
			stTime.wMinute,
			stTime.wSecond,
			stTime.wMilliseconds);

#endif   /* WIN32 */

	return tp;
}

int
get_server_version(PGconn *conn)
{
	PGresult	*res;
	int			 server_version_num;

	res = pgut_execute(conn, "SHOW server_version_num", 0, NULL);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		server_version_num = -1;
	else
		server_version_num = atoi(PQgetvalue(res, 0, 0));
	PQclear(res);

	return server_version_num;
}

static bool
decode_time(const char *field, int *hour, int *min, int *sec)
{
	char	*cp;

	errno = 0;
	*hour = strtoi(field, &cp, 10);
	if (errno == ERANGE || *cp != ':')
		return false;
	errno = 0;
	*min = strtoi(cp + 1, &cp, 10);
	if (errno == ERANGE)
		return false;
	if (*cp == '\0')
		*sec = 0;
	else if (*cp == ':')
	{
		errno = 0;
		*sec = strtoi(cp + 1, &cp, 10);
		if (errno == ERANGE || *cp != '\0')
			return false;
	}
	else
		return false;

	/* sanity check */
	if (*hour < 0 || *hour > 23 ||
		*min < 0 || *min > 59 ||
		*sec < 0 || *sec > 59)
		return false;

	return true;
}

static int
strtoi(const char *nptr, char **endptr, int base)
{
	long		val;

	val = strtol(nptr, endptr, base);
#ifdef HAVE_LONG_INT_64
	if (val != (long) ((int32) val))
		errno = ERANGE;
#endif
	return (int) val;
}

static bool
execute_script(PGconn *conn, const char *script_file)
{
	FILE		   *fp;
	StringInfoData	buf;
	bool			ok;

	/* read script into buffer. */
	if ((fp = pgut_fopen(script_file, "rt")) == NULL)
		return false;
	initStringInfo(&buf);
	if ((errno = appendStringInfoFile(&buf, fp)) == 0)
	{
		/* execute the read script contents. */
		switch (pgut_command(conn, buf.data, 0, NULL))
		{
			case PGRES_COMMAND_OK:
			case PGRES_TUPLES_OK:
				ok = true;
				break;
			default:
				ok = false;
				break;
		}
	}
	else
	{
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not read file \"%s\": ", script_file)));
		ok = false;
	}

	fclose(fp);
	termStringInfo(&buf);
	return ok;
}
