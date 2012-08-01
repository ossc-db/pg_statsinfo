/*
 * logger.c
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#ifndef WIN32
#include <dirent.h>
#endif

/* pg_statsinfo.control values */
static StatsinfoControlFileData		ControlFile;

static int			cf_fd; 
static time_t		logger_reload_time = 0;
static List		   *adjust_log_list = NIL;

/*---- GUC variables (logger) ----------*/
static char		   *my_log_directory;
static PGErrorVerbosity	my_log_error_verbosity = PGERROR_DEFAULT;
static int			my_syslog_facility;
static char		   *my_syslog_ident;
static char		   *my_syslog_line_prefix;
static int			my_syslog_min_messages;
static char		   *my_textlog_filename;
static char		   *my_textlog_line_prefix;
static int			my_textlog_min_messages;
static int			my_textlog_permission;
static bool			my_adjust_log_level;
static char		   *my_adjust_log_info;
static char		   *my_adjust_log_notice;
static char		   *my_adjust_log_warning;
static char		   *my_adjust_log_error;
static char		   *my_adjust_log_log;
static char		   *my_adjust_log_fatal;
static List		   *my_textlog_nologging_users;
/*-----------------------*/

#if PG_VERSION_NUM < 90000
#define CSV_COLS			22
#else
#define CSV_COLS			23
#endif

/*
 * delay is required because postgres logger might be alive in some seconds
 * after postmaster is terminated.
 */
#define LOGGER_EXIT_DELAY	2

/*
 * delay is required because postgres writes reload log before reloading
 * setting files actually. If we query settings too soon, backends might
 * not yet reload setting files.
 */
#define RELOAD_DELAY		2

typedef struct Logger
{
	/* CSV log file */
	char	csv_path[MAXPGPATH];	/* current log file path */
	char   *csv_name;				/* log file name */
	long	csv_offset;				/* parsed bytes in log file */
	FILE   *fp;						/* open log file */

	/* Text log file */
	FILE   *textlog;
	char	textlog_path[MAXPGPATH];

	/* temp buffer */
	StringInfoData	buf;			/* log buffer */
	size_t	fields[CSV_COLS + 1];	/* field offsets in buffer */
} Logger;

typedef struct AdjustLog
{
	char	*sqlstate;	/* SQL STATE to be adjusted */
	int		 elevel;	/* log level */
} AdjustLog;

static List			   *log_queue = NIL;
static pthread_mutex_t	log_lock;
static int				recursive_level = 0;

static void reload_params(void);
static void logger_recv(Logger *logger);

static void logger_parse(Logger *logger, const char *pg_log, bool only_routing);

static void logger_route(Logger *logger, const Log *log);

static void init_log(Log *log, const char *buf, size_t len, const size_t fields[]);
static void logger_close(Logger *logger);
static bool logger_next(Logger *logger, const char *pg_log);
static void get_csvlog(char csvlog[], const char *prev, const char *pg_log);
static void adjust_log(Log *log);
static void replace_extension(char path[], const char *extension);
static void assign_textlog_path(Logger *logger, const char *pg_log);
static void assign_csvlog_path(Logger *logger, const char *pg_log, const char *csvlog, long offset);
static List *add_adlog(List *adlog_list, int elevel, char *rawstring);
static char *b_trim(char *str);
static bool split_string(char *rawstring, char separator, List **elemlist);
static bool is_nologging_user(const Log *log);
static int csvfilter(const struct dirent *dp);
static void load_controlfile(Logger *logger);
static bool ReadControlFile(void);
static void RewriteControlFile(void);
static void logger_shutdown(void *retval);

void
logger_init(void)
{
	pthread_mutex_init(&log_lock, NULL);
	shutdown_message_found = false;
	log_queue = NIL;
	logger_reload_time = 0;	/* any values ok as far as before now */
}

/*
 * logger_main
 */
void *
logger_main(void *arg)
{
	Logger			  logger;
	int				  entry;
	struct dirent	**dplist;

	memset(&logger, 0, sizeof(logger));
	initStringInfo(&logger.buf);
	reload_params();

	/* sets latest csvlog to the elements of logger */
	for (;;)
	{
		entry = scandir(my_log_directory, &dplist, csvfilter, alphasort);
		if (entry > 0)
			break;
		usleep(200 * 1000);	/* 200ms */
	};
	assign_csvlog_path(&logger, my_log_directory, dplist[entry - 1]->d_name, 0);
	assign_textlog_path(&logger, my_log_directory);

	while (entry--)
		free(dplist[entry]);
	free(dplist);

	/* load the pg_statsinfo.control */
	load_controlfile(&logger);
	ControlFile.state = STATSINFO_RUNNING;

	/* perform the log routing until end of the latest csvlog */
	logger.fp = pgut_fopen(logger.csv_path, "rt");
	logger_parse(&logger, my_log_directory, true);

	/*
	 * Logger should not shutdown before any other threads are alive,
	 * or postmaster exists unless shutdown log is not found.
	 */
	while (shutdown_state < WRITER_SHUTDOWN ||
		   (!shutdown_message_found && postmaster_is_alive()))
	{
		/* update settings if reloaded */
		if (logger_reload_time < collector_reload_time)
		{
			reload_params();

			if (logger.textlog)
			{
				mode_t	mask;

				mask = umask(0777 & ~my_textlog_permission);
				chmod(logger.textlog_path, my_textlog_permission);
				umask(mask);
			}
			else
				assign_textlog_path(&logger, my_log_directory);
		}

		logger_parse(&logger, my_log_directory, false);
		usleep(200 * 1000);	/* 200ms */

		/* check postmaster pid. */
		if (shutdown_state < SHUTDOWN_REQUESTED && !postmaster_is_alive())
			shutdown_progress(SHUTDOWN_REQUESTED);

		logger_recv(&logger);
	}

	/* exit after some delay */
	if (!shutdown_message_found)
	{
		time_t	until = time(NULL) + LOGGER_EXIT_DELAY;

		for (;;)
		{
			logger_parse(&logger, my_log_directory, false);
			logger_recv(&logger);
			if (shutdown_message_found || time(NULL) > until)
				break;
			usleep(200 * 1000);	/* 200ms */
		}
	}

	/* final shutdown message */
	if (shutdown_message_found)
		elog(LOG, "shutdown");
	else
		elog(WARNING, "shutdown because server process exited abnormally");
	logger_recv(&logger);

	logger_close(&logger);
	shutdown_progress(LOGGER_SHUTDOWN);
	ControlFile.state = STATSINFO_SHUTDOWNED;

	/* update pg_statsinfo.control */
	RewriteControlFile();

	return (void *) LOGGER_RETURN_SUCCESS;
}

#ifdef PGUT_OVERRIDE_ELOG

/*
 * We retrieve log_timezone_name from the server because strftime returns
 * platform-depending value, but postgres uses own implementation.
 * Especially, incompatible presentation will be returned on Windows.
 */
static char *
format_log_time(char *buf)
{
	struct timeval	tv;
	struct tm	   *tm;
	time_t			now;
	bool			gmt;
	char			msbuf[8];

	gettimeofday(&tv, NULL);
	now = (time_t) tv.tv_sec;

	/*
	 * Only supports local and GMT timezone because postgres uses own
	 * private timezone catalog. We cannot use it from external processes.
	 */
	gmt = (pg_strcasecmp(log_timezone_name, "GMT") == 0 ||
		   pg_strcasecmp(log_timezone_name, "UTC") == 0);
	tm = gmt ? gmtime(&now) : localtime(&now);
	strftime(buf, LOGTIME_LEN, "%Y-%m-%d %H:%M:%S     ", tm);

	/* 'paste' milliseconds into place... */
	sprintf(msbuf, ".%03d", (int) (tv.tv_usec / 1000));
	strncpy(buf + 19, msbuf, 4);

	/* 'paste' timezone name */
	strcpy(buf + 24, log_timezone_name);

	return buf;
}

/*
 * write messages to stderr for debug or logger is not ready.
 */
static void
write_console(int elevel, const char *msg, const char *detail)
{
	const char *tag = elevel_to_str(elevel);

	if (detail && detail[0])
		fprintf(stderr, "%s: %s\nDETAIL: %s\n", tag, msg, detail);
	else
		fprintf(stderr, "%s: %s\n", tag, msg);
	fflush(stderr);
}

void
pgut_error(int elevel, int code, const char *msg, const char *detail)
{
	Log		   *log;
	char	   *buf;
	size_t		code_len;
	size_t		prefix_len;
	size_t		msg_len;
	size_t		detail_len;

	static char		pid[32];

	/* return if not ready */
	if (shutdown_state < RUNNING || LOGGER_SHUTDOWN <= shutdown_state)
	{
		if (log_required(elevel, pgut_log_level))
			write_console(elevel, msg, detail);
		return;
	}

#ifndef USE_DAEMON
	if (log_required(elevel, pgut_log_level))
		write_console(elevel, msg, detail);
#endif

	/* avoid recursive errors */
	if (pthread_self() == th_logger && recursive_level > 0)
		return;

	if (!pid[0])
		sprintf(pid, "%d", getpid());

	code_len = (code ? LOGCODE_LEN : 0);
	prefix_len = strlen(LOG_PREFIX);
	msg_len = (msg ? strlen(msg) + 1 : 0);
	detail_len = (detail ? strlen(detail) + 1 : 0);
	log = pgut_malloc(sizeof(Log) +
			LOGTIME_LEN + code_len + prefix_len + msg_len + detail_len);
	buf = ((char *) log) + sizeof(Log);

	log->timestamp = format_log_time(buf);
	buf += LOGTIME_LEN;
	log->username = PROGRAM_NAME;
	log->database = "";
	log->pid = pid;
	log->client_addr = "";
	log->session_id = "";
	log->session_line_num = "";
	log->ps_display = PROGRAM_NAME;
	log->session_start = "";
	log->vxid = "";
	log->xid = "";
	log->elevel = elevel;
	if (code != 0)
	{
		log->sqlstate = buf;
		if (code > 0)
			snprintf(buf, code_len, "S%04d", code);
		else
			snprintf(buf, code_len, "SX%03d", -code);
		buf += code_len;
	}
	else
		log->sqlstate = "00000";
	if (msg)
	{
		log->message = buf;
		memcpy(buf, LOG_PREFIX, prefix_len);
		buf += prefix_len;
		memcpy(buf, msg, msg_len);
		buf += msg_len;
	}
	else
		log->message = "";
	if (detail)
	{
		log->detail = buf;
		memcpy(buf, detail, detail_len);
		buf += detail_len;
	}
	else
		log->detail = "";
	log->hint = "";
	log->query = "";
	log->query_pos = "";
	log->context = "";
	log->user_query = "";
	log->user_query_pos = "";
	log->error_location = "";
	log->application_name = PROGRAM_NAME;

	pthread_mutex_lock(&log_lock);
	log_queue = lappend(log_queue, log);
	pthread_mutex_unlock(&log_lock);
}

#endif

static void
reload_params(void)
{
	logger_reload_time = collector_reload_time;
	pthread_mutex_lock(&reload_lock);

	free(my_log_directory);
	if (is_absolute_path(log_directory))
		my_log_directory = pgut_strdup(log_directory);
	else
	{
		my_log_directory = pgut_malloc(MAXPGPATH);
		join_path_components(my_log_directory, data_directory, log_directory);
	}

	/* log_error_verbosity */
	if (pg_strcasecmp(log_error_verbosity, "terse") == 0)
		my_log_error_verbosity = PGERROR_TERSE;
	else if (pg_strcasecmp(log_error_verbosity, "verbose") == 0)
		my_log_error_verbosity = PGERROR_VERBOSE;
	else
		my_log_error_verbosity = PGERROR_DEFAULT;

	/* textlog */
	my_textlog_min_messages = textlog_min_messages;
	free(my_textlog_line_prefix);
	my_textlog_line_prefix = pgut_strdup(textlog_line_prefix);
	free(my_textlog_filename);
	/*
	 * TODO: currently, empty textlog_filename is disallowed in server,
	 * but the ideal behavior might be to use the default log_filename
	 * directly instead of fixed file name.
	 */
	my_textlog_filename = pgut_strdup(textlog_filename);
	my_textlog_permission = textlog_permission & 0666;
	list_destroy(my_textlog_nologging_users, free);
	split_string(textlog_nologging_users, ',', &my_textlog_nologging_users);

	/* syslog */
	my_syslog_min_messages = syslog_min_messages;
	free(my_syslog_line_prefix);
	my_syslog_line_prefix = pgut_strdup(syslog_line_prefix);
	my_syslog_facility = syslog_facility;
	free(my_syslog_ident);
	my_syslog_ident = pgut_strdup(syslog_ident);

	/* adjust log level */
	my_adjust_log_level = adjust_log_level;
	if (my_adjust_log_level)
	{
		free(my_adjust_log_info);
		my_adjust_log_info = pgut_strdup(adjust_log_info);
		free(my_adjust_log_notice);
		my_adjust_log_notice = pgut_strdup(adjust_log_notice);
		free(my_adjust_log_warning);
		my_adjust_log_warning = pgut_strdup(adjust_log_warning);
		free(my_adjust_log_error);
		my_adjust_log_error = pgut_strdup(adjust_log_error);
		free(my_adjust_log_log);
		my_adjust_log_log = pgut_strdup(adjust_log_log);
		free(my_adjust_log_fatal);
		my_adjust_log_fatal = pgut_strdup(adjust_log_fatal);

		list_destroy(adjust_log_list, free);
		adjust_log_list = NIL;
		adjust_log_list = add_adlog(adjust_log_list, FATAL, my_adjust_log_fatal);
		adjust_log_list = add_adlog(adjust_log_list, LOG, my_adjust_log_log);
		adjust_log_list = add_adlog(adjust_log_list, ERROR, my_adjust_log_error);
		adjust_log_list = add_adlog(adjust_log_list, WARNING, my_adjust_log_warning);
		adjust_log_list = add_adlog(adjust_log_list, NOTICE, my_adjust_log_notice);
		adjust_log_list = add_adlog(adjust_log_list, INFO, my_adjust_log_info);
	}

	pgut_log_level = Min(textlog_min_messages, syslog_min_messages);

	pthread_mutex_unlock(&reload_lock);
}

/*
 * logger_recv - receive elog calls
 */
static void
logger_recv(Logger *logger)
{
	List	   *logs;
	ListCell   *cell;

	pthread_mutex_lock(&log_lock);
	logs = log_queue;
	log_queue = NIL;
	pthread_mutex_unlock(&log_lock);

	recursive_level++;
	foreach(cell, logs)
		logger_route(logger, lfirst(cell));
	recursive_level--;

	list_free_deep(logs);
}

/*
 * logger_route
 */
static void
logger_route(Logger *logger, const Log *log)
{
	/* syslog? */
	if (log_required(log->elevel, my_syslog_min_messages))
		write_syslog(log,
					 my_syslog_line_prefix,
					 my_log_error_verbosity,
					 my_syslog_ident,
					 my_syslog_facility);

	/* textlog? */
	if (log_required(log->elevel, my_textlog_min_messages))
	{
		if (logger->textlog == NULL)
		{
			mode_t	mask;

			Assert(logger->textlog_path[0]);

			/* create a new textlog file */
			mask = umask(0777 & ~my_textlog_permission);
			logger->textlog = pgut_fopen(logger->textlog_path, "at");
			umask(mask);
		}

		if (logger->textlog != NULL)
		{
			/* don't write a textlog of the users that are set not logging */
			if (!is_nologging_user(log))
			{
				if (!write_textlog(log,
								   my_textlog_line_prefix,
								   my_log_error_verbosity,
								   logger->textlog))
				{
					/* unexpected error; close the file, and try to reopen */
					fclose(logger->textlog);
					logger->textlog = NULL;
				}
			}
		}
	}
}

static void
init_log(Log *log, const char *buf, size_t len, const size_t fields[])
{
	int		i;

	i = 0;
	log->timestamp = buf + fields[i++];
	log->username = buf + fields[i++];
	log->database = buf + fields[i++];
	log->pid = buf + fields[i++];
	log->client_addr = buf + fields[i++];
	log->session_id = buf + fields[i++];
	log->session_line_num = buf + fields[i++];
	log->ps_display = buf + fields[i++];
	log->session_start = buf + fields[i++];
	log->vxid = buf + fields[i++];
	log->xid = buf + fields[i++];
	log->elevel = str_to_elevel(buf + fields[i++]);
	log->sqlstate = buf + fields[i++];
	log->message = buf + fields[i++];
	log->detail = buf + fields[i++];
	log->hint = buf + fields[i++];
	log->query = buf + fields[i++];
	log->query_pos = buf + fields[i++];
	log->context = buf + fields[i++];
	log->user_query = buf + fields[i++];
	log->user_query_pos = buf + fields[i++];
	log->error_location = buf + fields[i++];
#if PG_VERSION_NUM >= 90000
	log->application_name = buf + fields[i++];
#else
	log->application_name = "";
#endif
	Assert(i == CSV_COLS);
}

/*
 * logger_parse - Parse CSV log and route it into textlog, syslog, or trap.
 */
static void
logger_parse(Logger *logger, const char *pg_log, bool only_routing)
{
	while (logger_next(logger, pg_log))
	{
		Log		log;
		int		save_elevel;

		init_log(&log, logger->buf.data, logger->buf.len, logger->fields);

		/* parse performance logs; those messages are NOT routed. */
		if (log.elevel == LOG)
		{
			if (!only_routing)
			{
				/* checkpoint ? */
				if (parse_checkpoint(log.message, log.timestamp))
					continue;

				/* autovacuum ? */
				if (parse_autovacuum(log.message, log.timestamp))
					continue;
			}

			/* snapshot requested ? */
			if (strcmp(log.message, LOGMSG_SNAPSHOT) == 0)
			{
				if (!only_routing)
				{
					pthread_mutex_lock(&reload_lock);
					free((char *) snapshot_requested);
					snapshot_requested = pgut_strdup(log.detail);
					pthread_mutex_unlock(&reload_lock);
				}
				continue;
			}

			/* maintenance requested ? */
			if (strcmp(log.message, LOGMSG_MAINTENANCE) == 0)
			{
				if (!only_routing)
				{
					pthread_mutex_lock(&reload_lock);
					free((char *) maintenance_requested);
					maintenance_requested = pgut_strdup(log.detail);
					pthread_mutex_unlock(&reload_lock);
				}
				continue;
			}

			/* restart requested ? */
			if (strcmp(log.message, LOGMSG_RESTART) == 0)
			{
				if (!only_routing)
				{
					shutdown_message_found = true;
					shutdown_progress(SHUTDOWN_REQUESTED);
				}
				continue;
			}

#ifdef ADJUST_PERFORMANCE_MESSAGE_LEVEL
			/* performance log? */
			if ((my_textlog_min_messages > INFO ||
				 my_syslog_min_messages > INFO) &&
				 is_performance_message(log.message))
			{
				log.elevel = INFO;
				continue;
			}
#endif
		}

		/*
		 * route the log to syslog and textlog.
		 * if log level adjust enabled, adjust the log level.
		 */
		save_elevel = log.elevel;
		if (my_adjust_log_level)
			adjust_log(&log);
		logger_route(logger, &log);

		/* update pg_statsinfo.control */
		strlcpy(ControlFile.csv_name,
			logger->csv_name, sizeof(ControlFile.csv_name));
		ControlFile.csv_offset = logger->csv_offset;
		RewriteControlFile();

		if (!only_routing && save_elevel == LOG)
		{
			/* setting parameters reloaded ? */
			if (strcmp(log.message, msg_sighup) == 0)
			{
				server_reload_time = time(NULL) + RELOAD_DELAY;
				continue;
			}

			/* shutdown ? */
			if (strcmp(log.message, msg_shutdown) == 0)
			{
				shutdown_message_found = true;
				continue;
			}

			/* shutdown requested ? */
			if (strcmp(log.message, msg_shutdown_smart) == 0 ||
				strcmp(log.message, msg_shutdown_fast) == 0 ||
				strcmp(log.message, msg_shutdown_immediate) == 0)
			{
				shutdown_progress(SHUTDOWN_REQUESTED);
				continue;
			}
		}
	}
}

static void
logger_close(Logger *logger)
{
	/* close the previous log if open */
	if (logger->fp)
	{
		fclose(logger->fp);
		logger->fp = NULL;
	}

	/* close textlog and rename to the same name with csv */
	if (logger->textlog)
	{
		struct stat	st;

		fclose(logger->textlog);
		logger->textlog = NULL;

		/* overwrite existing .log file; it must be empty.
		 * Note:
		 * Some error messages through system() (eg. recovery_command)
		 * outputs to the .log file  from postgres's logger, so sometimes
		 * .log file will not be empty. At the moment we overwrite without check.
		 */
		if (logger->csv_path[0] && stat(logger->csv_path, &st) == 0)
		{
			char	path[MAXPGPATH];

			strlcpy(path, logger->csv_path, MAXPGPATH);
			replace_extension(path, ".log");
			rename(logger->textlog_path, path);
		}
	}
}

/*
 * logger_next
 */
static bool
logger_next(Logger *logger, const char *pg_log)
{
	struct stat	st;
	bool		ret;

	if (logger->fp == NULL ||
		stat(logger->csv_path, &st) != 0 ||
		logger->csv_offset >= st.st_size)
	{
		char	csvlog[MAXPGPATH];
		char	textlog[MAXPGPATH];

		if (shutdown_message_found)
			return false;	/* must end with the current log */

		get_csvlog(csvlog, logger->csv_name, pg_log);

		if (!csvlog[0])
			return false;	/* logfile not found */
		if (logger->fp && strcmp(logger->csv_name, csvlog) == 0)
			return false;	/* no more logs */

		join_path_components(textlog, pg_log, csvlog);
		replace_extension(textlog, ".log");

		/*
		 * csvlog files that have empty *.log have not been parsed yet
		 * because postgres logger make an empty log file.
		 * Note:
		 * Some "cannot stat" error messages are output to *.log,
		 * so we check logger->fp and csvlog again.
		 */
		 if (stat(textlog, &st) == 0 && st.st_size > 0)
		 {
			if (logger->fp && strcmp(logger->csv_name, csvlog) == 0)
				return false;	/* already parsed log */
		 }

		logger_close(logger);
		assign_textlog_path(logger, pg_log);
		assign_csvlog_path(logger, pg_log, csvlog, 0);

		logger->fp = pgut_fopen(logger->csv_path, "rt");
		if (logger->fp == NULL)
			return false;

		elog(DEBUG2, "read csvlog \"%s\"", logger->csv_path);
	}

	clearerr(logger->fp);
	fseek(logger->fp, logger->csv_offset, SEEK_SET);
	ret = read_csv(logger->fp, &logger->buf, CSV_COLS, logger->fields);
	logger->csv_offset = ftell(logger->fp);

	if (!ret)
	{
		int		save_errno = errno;

		/* close the file unless EOF; it means an error */
		if (!feof(logger->fp))
		{
			errno = save_errno;
			ereport(WARNING,
				(errcode_errno(),
				 errmsg("could not read csvlog file \"%s\": ",
					logger->csv_path)));
			fclose(logger->fp);
			logger->fp = NULL;
		}
	}

	return ret;
}

/*
 * Get the csvlog path to parse next, or null-string if no logs.
 */
static void
get_csvlog(char csvlog[], const char *prev, const char *pg_log)
{
	int				  entry;
	struct dirent	**dplist;
	struct stat		  st;
	char			  tmppath[MAXPGPATH];
	int				  i;

	Assert(csvlog);
	Assert(prev);
	Assert(prev[0]);
	Assert(pg_log);

	csvlog[0] = '\0';

	if ((entry = scandir(pg_log, &dplist, csvfilter, alphasort)) < 0)
	{
		/* pg_log directory might not exist before syslogger started */
		if (errno != ENOENT)
			ereport(WARNING,
				(errcode_errno(),
				 errmsg("could not scan directory \"%s\": ", pg_log)));
		return;
	}

	for (i = 0; i < entry; i++)
	{
		/* get the next log of previous parsed log */
		if (strcmp(prev, dplist[i]->d_name) < 0)
		{
			strlcpy(csvlog, dplist[i]->d_name, MAXPGPATH);

			join_path_components(tmppath, pg_log, dplist[i]->d_name);
			if (stat(tmppath, &st) == 0 && st.st_size > 0)
				break;
		}
	}

	while (entry--)
		free(dplist[entry]);
	free(dplist);
}

static void
adjust_log(Log *log)
{
	ListCell	*cell;

	foreach(cell, adjust_log_list)
	{
		AdjustLog *adlog = (AdjustLog *) lfirst(cell);

		if (strcmp(adlog->sqlstate, log->sqlstate) == 0)
		{
			log->elevel = adlog->elevel;
			elog(DEBUG2, "adjust log level -> %d: sqlstate=\"%s\"", log->elevel, log->sqlstate);
			break;
		}
	}
}

static void
replace_extension(char path[], const char *extension)
{
	char *dot;

	if ((dot = strrchr(path, '.')) != NULL)
		strlcpy(dot, extension, MAXPGPATH - (dot - path));
	else
		strlcat(dot, extension, MAXPGPATH);
}

static void
assign_textlog_path(Logger *logger, const char *pg_log)
{
	if (is_absolute_path(my_textlog_filename))
		strlcpy(logger->textlog_path, my_textlog_filename, MAXPGPATH);
	else
		join_path_components(logger->textlog_path, pg_log, my_textlog_filename);
}

static void
assign_csvlog_path(Logger *logger, const char *pg_log, const char *csvlog, long offset)
{
	join_path_components(logger->csv_path, pg_log, csvlog);
	logger->csv_name = logger->csv_path + strlen(pg_log) + 1;
	logger->csv_offset = offset;
}

static List *
add_adlog(List *adlog_list, int elevel, char *rawstring)
{
	char	*token;

	token = strtok(rawstring, ",");
	while (token)
	{
		AdjustLog *adlog = pgut_malloc(sizeof(AdjustLog));;
		adlog->elevel = elevel;
		adlog->sqlstate = b_trim(token);
		adlog_list = lappend(adlog_list, adlog);
		token = strtok(NULL, ",");
	}
	return adlog_list;
}

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

/*
 * Note: this function modify the argument
 */
static bool
split_string(char *rawstring, char separator, List **elemlist)
{
	char	*nextp = rawstring;
	bool	 done = false;

	*elemlist = NIL;

	/* skip leading whitespace */
	while (isspace((unsigned char) *nextp))
		nextp++;

	/* allow empty string */
	if (*nextp == '\0')
		return true;

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char *curname;
		char *endp;

		if (*nextp == '\"')
		{
			/* Quoted name --- collapse quote-quote pairs, no downcasing */
			curname = nextp + 1;
			for (;;)
			{
				endp = strchr(nextp + 1, '\"');
				if (endp == NULL)
					return false; /* mismatched quotes */
				if (endp[1] != '\"')
					break; /* found end of quoted name */
				/* Collapse adjacent quotes into one quote, and look again */
				memmove(endp, endp + 1, strlen(endp));
				nextp = endp;
			}
			/* endp now points at the terminating quote */
			nextp = endp + 1;
		}
		else
		{
			/* Unquoted name --- extends to separator or whitespace */
			curname = nextp;
			while (*nextp && *nextp != separator &&
				   !isspace((unsigned char) *nextp))
				nextp++;
			endp = nextp;
			if (curname == nextp)
				return false; /* empty unquoted name not allowed */
		}

		/* skip trailing whitespace */
		while (isspace((unsigned char) *nextp))
			nextp++;

		if (*nextp == separator)
		{
			nextp++;
			while (isspace((unsigned char) *nextp))
				nextp++; /* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false; /* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to list
		 */
		*elemlist = lappend(*elemlist, pgut_strdup(curname));

		/* Loop back if we didn't reach end of string */
	} while (!done);

	return true;
}

static bool
is_nologging_user(const Log *log)
{
	ListCell	*cell;

	foreach(cell, my_textlog_nologging_users)
	{
		if (strcmp(log->username, (char *) lfirst(cell)) == 0)
		return true;
	}
	return false;
}

static int
csvfilter(const struct dirent *dp)
{
	const char	*extension;

	if (dp->d_type != DT_REG)
		return 0;

	/* check the extension is .csv */
	extension = strrchr(dp->d_name, '.');
	if (extension && strcmp(extension, ".csv") == 0)
		return 1;
	else
		return 0;
}

/*
 * load the previous state from pg_statsinfo.control.
 */
static void
load_controlfile(Logger *logger)
{
	struct stat		st;
	bool			need_load = false;

	if (stat(STATSINFO_CONTROL_FILE, &st) == 0)
	{
		cf_fd = open(STATSINFO_CONTROL_FILE, O_RDWR | PG_BINARY, 0);
		need_load = true;
	}
	else
		cf_fd = open(STATSINFO_CONTROL_FILE,
					O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
					S_IRUSR | S_IWUSR);
	if (cf_fd < 0)
	{
		if (errno == ENOENT)
			return;		/* file not found */

		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not open control file \"%s\": %m",
			 	STATSINFO_CONTROL_FILE)));
		/* shutdown logger thread */
		logger_shutdown((void *) LOGGER_RETURN_FAILED);
	}

	if (!need_load)
		return;

	/*
	 * if state value of pg_statsinfo.control is not "STATSINFO_SHUTDOWNED",
	 * that means the previous pg_statsinfod was abnormally end.
	 * in that case, parse the csvlog between csvlog of the point of abnormal
	 * termination and latest csvlog.
	 */
	if (ReadControlFile())
	{
		char prev_csvlog[MAXPGPATH];

		join_path_components(prev_csvlog, my_log_directory, ControlFile.csv_name);

		if (stat(prev_csvlog, &st) == 0 && ControlFile.csv_offset <= st.st_size)
		{
			/* set the csvlog path and the csvlog offset to the logger */
			assign_csvlog_path(logger, my_log_directory,
			ControlFile.csv_name, ControlFile.csv_offset);
			return;
		}

		/* csvlog which parsed at last is missed */
		ereport(WARNING,
			(errmsg("csvlog file \"%s\" not found or incurrect offset",
				ControlFile.csv_name)));
	}

	/*
	 * could not read the pg_statsinfo.control or incorrect data.
	 * rename the latest textlog file to "<latest-csvlog>.err.<seqid>"
	 * (eg. postgresql-2012-07-01_000000.err.1)
	 */
	if (stat(logger->textlog_path, &st) == 0)
	{
		char new_path[MAXPGPATH];
		char extension[32];
		int seqid = 0;

		for (;;)
		{
			strlcpy(new_path, logger->csv_path, sizeof(new_path));
			snprintf(extension, sizeof(extension), ".err.%d", ++seqid);
			replace_extension(new_path, extension);

			if (stat(new_path, &st) != 0)
				break;
		}

		rename(logger->textlog_path, new_path);
		elog(WARNING,
			"latest textlog file already exists, it renamed to '%s'", new_path);
	}
}

/*
 * I/O routines for pg_statsinfo.control
 *
 * ControlFile is a buffer in memory that holds an image of the contents of
 * pg_statsinfo.control. RewriteControlFile() writes the pg_statsinfo.control
 * file with the contents in buffer. ReadControlFile() loads the buffer from the
 * pg_statsinfo.control file.
 */
static bool
ReadControlFile(void)
{
	pg_crc32	crc;

	Assert(cf_fd > 0);	/* have not been opened the pg_statsinfo.control */

	/* read data */
	if (read(cf_fd, &ControlFile,
			sizeof(ControlFile)) != sizeof(ControlFile))
	{
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not read from control file \"%s\": %m",
			 	STATSINFO_CONTROL_FILE)));
		return false;
	}

	/*
	 * Check for expected pg_statsinfo.control format version.
	 * If this is wrong, the CRC check will likely fail because we'll be
	 * checking the wrong number of bytes.
	 * Complaining about wrong version will probably be more enlightening
	 * than complaining about wrong CRC.
	 */
	if (ControlFile.control_version != STATSINFO_CONTROL_VERSION &&
		((ControlFile.control_version / 100) != (STATSINFO_CONTROL_VERSION / 100)))
	{
		ereport(ERROR,
			(errmsg("pg_statsinfo.control format incompatible"),
			 errdetail("pg_statsinfo.control was created with STATSINFO_CONTROL_VERSION %d (0x%08x), "
			 		   "but the pg_statsinfo was compiled with STATSINFO_CONTROL_VERSION %d (0x%08x)",
					ControlFile.control_version, ControlFile.control_version,
					STATSINFO_CONTROL_VERSION, STATSINFO_CONTROL_VERSION)));
		return false;
	}

	/* check the CRC */
	INIT_CRC32(crc);
	COMP_CRC32(crc,
		(char *) &ControlFile, offsetof(StatsinfoControlFileData, crc));
	FIN_CRC32(crc);

	if (!EQ_CRC32(crc, ControlFile.crc))
	{
		ereport(ERROR,
			(errmsg("incorrect checksum in control file \"%s\"",
				STATSINFO_CONTROL_FILE)));
		return false;
	}

	return true;
}

static void
RewriteControlFile(void)
{
	char	buffer[sizeof(ControlFile)];

	Assert(cf_fd > 0);	/* have not been opened the pg_statsinfo.control */

	/* initialize version and compatibility-check fields */
	ControlFile.control_version = STATSINFO_CONTROL_VERSION;

	/* contents are protected with a CRC */
	INIT_CRC32(ControlFile.crc);
	COMP_CRC32(ControlFile.crc,
		(char *) &ControlFile, offsetof(StatsinfoControlFileData, crc));
	FIN_CRC32(ControlFile.crc);

	memset(buffer, 0, sizeof(ControlFile));
	memcpy(buffer, &ControlFile, sizeof(ControlFile));

	lseek(cf_fd, 0, SEEK_SET);
	errno = 0;
	if (write(cf_fd, buffer, sizeof(ControlFile)) != sizeof(ControlFile))
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not write to control file \"%s\": %m",
			 	STATSINFO_CONTROL_FILE)));
		/* shutdown logger thread */
		logger_shutdown((void *) LOGGER_RETURN_FAILED);
	}
}

static void
logger_shutdown(void *retval)
{
	/* notify shutdown request to other threads */
	if (shutdown_state < SHUTDOWN_REQUESTED)
		shutdown_progress(SHUTDOWN_REQUESTED);

	/* wait until the end of the other threads */
	while (shutdown_state < WRITER_SHUTDOWN)
		usleep(200 * 1000);	/* 200ms */

	/* exit the logger thread */
	shutdown_progress(LOGGER_SHUTDOWN);
	pthread_exit(retval);
}
