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

static void logger_parse(Logger *logger, const char *pg_log);

static void logger_route(Logger *logger, const Log *log);

static void init_log(Log *log, const char *buf, size_t len, const size_t fields[]);
static void logger_close(Logger *logger);
static bool logger_next(Logger *logger, const char *pg_log);
static void get_csvlog(char csvlog[], const char *prev, const char *pg_log);
static void adjust_log(Log *log);
static void replace_extension(char path[], const char *extension);
static void assign_textlog_path(Logger *logger, const char *pg_log);
static void assign_csvlog_path(Logger *logger, const char *pg_log, const char *csvlog);
static List *add_adlog(List *adlog_list, int elevel, char *rawstring);
static char *b_trim(char *str);

void
logger_init(void)
{
	pthread_mutex_init(&log_lock, NULL);
	shutdown_message_found = false;
	log_queue = NIL;
	logger_reload_time = 0;	/* any values ok as far as before now */
}

/*
 * logger_main(const char *prev_csv_name)
 */
void *
logger_main(void *arg)
{
	Logger		logger;
	const char *prev_csv_name = (const char *) arg;

	memset(&logger, 0, sizeof(logger));
	initStringInfo(&logger.buf);
	reload_params();
	assign_textlog_path(&logger, my_log_directory);
	assign_csvlog_path(&logger, my_log_directory, prev_csv_name);

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

		logger_parse(&logger, my_log_directory);
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
			logger_parse(&logger, my_log_directory);
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

	return NULL;
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
logger_parse(Logger *logger, const char *pg_log)
{
	while (logger_next(logger, pg_log))
	{
		Log		log;
		int		save_elevel;

		init_log(&log, logger->buf.data, logger->buf.len, logger->fields);

		/* parse performance logs; those messages are NOT routed. */
		if (log.elevel == LOG)
		{
			/* checkpoint ? */
			if (parse_checkpoint(log.message, log.timestamp))
				continue;

			/* autovacuum ? */
			if (parse_autovacuum(log.message, log.timestamp))
				continue;

			/* snapshot requested ? */
			if (strcmp(log.message, LOGMSG_SNAPSHOT) == 0)
			{
				pthread_mutex_lock(&reload_lock);
				free((char *) snapshot_requested);
				snapshot_requested = pgut_strdup(log.detail);
				pthread_mutex_unlock(&reload_lock);
				continue;
			}

			/* maintenance requested ? */
			if (strcmp(log.message, LOGMSG_MAINTENANCE) == 0)
			{
				pthread_mutex_lock(&reload_lock);
				free((char *) maintenance_requested);
				maintenance_requested = pgut_strdup(log.detail);
				pthread_mutex_unlock(&reload_lock);
				continue;
			}

			/* restart requested ? */
			if (strcmp(log.message, LOGMSG_RESTART) == 0)
			{
				shutdown_message_found = true;
				shutdown_progress(SHUTDOWN_REQUESTED);
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

		if (save_elevel == LOG)
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
		 * (Note : Some error messages through system() (eg. recovery_command)
		 *  outputs to the .log file  from postgres's logger, so sometimes
		 *  .log file will not be empty. At the moment we overwrite without check.
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
		 * (Note: Some "cannot stat" error messages are output to *.log,
		 *  so we check logger->fp and csvlog again.
		 */
		 if (stat(textlog, &st) == 0 && st.st_size > 0)
		 {
			if (logger->fp && strcmp(logger->csv_name, csvlog) == 0)
				return false;	/* already parsed log */
		 }

		logger_close(logger);
		assign_textlog_path(logger, pg_log);
		assign_csvlog_path(logger, pg_log, csvlog);

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
	DIR			   *dir;
	struct dirent  *dp;

	Assert(csvlog);
	Assert(prev);
	Assert(prev[0]);
	Assert(pg_log);

	csvlog[0] = '\0';

	if ((dir = opendir(pg_log)) == NULL)
	{
		/* pg_log directory might not exist before syslogger started */
		if (errno != ENOENT)
			ereport(WARNING,
				(errcode_errno(),
				 errmsg("could not open directory \"%s\": ", pg_log)));
		return;
	}

	for (dp = readdir(dir); dp != NULL; dp = readdir(dir))
	{
		const char *extension = strrchr(dp->d_name, '.');

		/* check the extension is .csv */
		if (extension == NULL || strcmp(extension, ".csv") != 0)
			continue;

		/* get the next log of previous parsed log */
		if (strcmp(prev, dp->d_name) >= 0 ||
			(csvlog[0] && strcmp(csvlog, dp->d_name) < 0))
			continue;

		strlcpy(csvlog, dp->d_name, MAXPGPATH);
	}
	closedir(dir);
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
assign_csvlog_path(Logger *logger, const char *pg_log, const char *csvlog)
{
	join_path_components(logger->csv_path, pg_log, csvlog);
	logger->csv_name = logger->csv_path + strlen(pg_log) + 1;
	logger->csv_offset = 0;
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
