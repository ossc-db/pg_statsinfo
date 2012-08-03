/*-------------------------------------------------------------------------
 *
 * pg_statsinfod.h
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PG_STATSINFOD_H
#define PG_STATSINFOD_H

#include "postgres_fe.h"
#include "pgut/pgut.h"
#include "pgut/pgut-list.h"
#include "pgut/pgut-pthread.h"

#include "../common.h"

///#define USE_DAEMON				/* become daemon? */

#define DB_MAX_RETRY		10		/* max retry count for database operations */
#define LOGTIME_LEN			40		/* buffer size for timestamp */
#define LOGCODE_LEN			6		/* buffer size for sqlcode */
#define SECS_PER_DAY		86400	/* seconds per day */

#define STATSINFO_CONTROL_FILE		"pg_statsinfo.control"
#define STATSINFO_CONTROL_VERSION	20400

#define LOGGER_RETURN_SUCCESS		0x00
#define LOGGER_RETURN_FAILED		0xff

/* read settings */
#define SQL_SELECT_CUSTOM_SETTINGS "\
SELECT \
	t.name, \
	s.setting \
FROM \
	(VALUES \
		('log_directory'), \
		('log_error_verbosity'), \
		('syslog_facility'), \
		('syslog_ident'), \
		('" GUC_PREFIX ".syslog_min_messages'), \
		('" GUC_PREFIX ".textlog_min_messages'), \
		('" GUC_PREFIX ".textlog_filename'), \
		('" GUC_PREFIX ".textlog_line_prefix'), \
		('" GUC_PREFIX ".syslog_line_prefix'), \
		('" GUC_PREFIX ".textlog_permission'), \
		('" GUC_PREFIX ".excluded_dbnames'), \
		('" GUC_PREFIX ".excluded_schemas'), \
		('" GUC_PREFIX ".stat_statements_max'), \
		('" GUC_PREFIX ".stat_statements_exclude_users'), \
		('" GUC_PREFIX ".sampling_interval'), \
		('" GUC_PREFIX ".snapshot_interval'), \
		('" GUC_PREFIX ".repository_server'), \
		('" GUC_PREFIX ".adjust_log_level'), \
		('" GUC_PREFIX ".adjust_log_info'), \
		('" GUC_PREFIX ".adjust_log_notice'), \
		('" GUC_PREFIX ".adjust_log_warning'), \
		('" GUC_PREFIX ".adjust_log_error'), \
		('" GUC_PREFIX ".adjust_log_log'), \
		('" GUC_PREFIX ".adjust_log_fatal'), \
		('" GUC_PREFIX ".textlog_nologging_users'), \
		('" GUC_PREFIX ".enable_maintenance'), \
		('" GUC_PREFIX ".maintenance_time'), \
		('" GUC_PREFIX ".repository_keepday'), \
		('" GUC_PREFIX ".log_maintenance_command')) AS t(name) \
	LEFT JOIN pg_settings s \
	ON t.name = s.name"

/* shutdown state */
typedef enum ShutdownState
{
	STARTUP,
	RUNNING,
	SHUTDOWN_REQUESTED,
	COLLECTOR_SHUTDOWN,
	WRITER_SHUTDOWN,
	LOGGER_SHUTDOWN
} ShutdownState;

/* writer queue type */
typedef enum WriterQueueType
{
	QUEUE_SNAPSHOT,
	QUEUE_CHECKPOINT,
	QUEUE_AUTOVACUUM
} WriterQueueType;

/*
 * System status indicator
 * Note: this is stored in pg_statsinfo.control
 */
typedef enum StatsinfoState
{
	STATSINFO_RUNNING,
	STATSINFO_SHUTDOWNED
} StatsinfoState;

/* pg_statsinfod.c */
extern char		   *instance_id;
extern char		   *postmaster_port;
extern int			server_version_num;
extern char		   *server_version_string;
extern int			server_encoding;
extern char		   *log_timezone_name;
/*---- GUC variables (collector) -------*/
extern char		   *data_directory;
extern char		   *excluded_dbnames;
extern char		   *excluded_schemas;
extern char		   *stat_statements_max;
extern char		   *stat_statements_exclude_users;
extern int			sampling_interval;
extern int			snapshot_interval;
/*---- GUC variables (logger) ----------*/
extern char		   *log_directory;
extern char		   *log_error_verbosity;
extern int			syslog_facility;
extern char		   *syslog_ident;
extern char		   *syslog_line_prefix;
extern int			syslog_min_messages;
extern char		   *textlog_filename;
extern char		   *textlog_line_prefix;
extern int			textlog_min_messages;
extern int			textlog_permission;
extern bool			adjust_log_level;
extern char		   *adjust_log_info;
extern char		   *adjust_log_notice;
extern char		   *adjust_log_warning;
extern char		   *adjust_log_error;
extern char		   *adjust_log_log;
extern char		   *adjust_log_fatal;
extern char		   *textlog_nologging_users;
/*---- GUC variables (writer) ----------*/
extern char		   *repository_server;
extern int		    enable_maintenance;
extern time_t		maintenance_time;
extern int			repository_keepday;
extern char		   *log_maintenance_command;
/*---- message format ----*/
extern char		   *msg_debug;
extern char		   *msg_info;
extern char		   *msg_notice;
extern char		   *msg_log;
extern char		   *msg_warning;
extern char		   *msg_error;
extern char		   *msg_fatal;
extern char		   *msg_panic;
extern char		   *msg_shutdown;
extern char		   *msg_shutdown_smart;
extern char		   *msg_shutdown_fast;
extern char		   *msg_shutdown_immediate;
extern char		   *msg_sighup;
extern char		   *msg_autovacuum;
extern char		   *msg_autoanalyze;
extern char		   *msg_checkpoint_starting;
extern char		   *msg_checkpoint_complete;
extern size_t		checkpoint_starting_prefix_len;
/*--------------------------------------*/

extern volatile ShutdownState	shutdown_state;
extern bool						shutdown_message_found;

/* threads */
extern pthread_t	th_collector;
extern pthread_t	th_logger;
extern pthread_t	th_writer;

/* collector.c */
extern pthread_mutex_t	reload_lock;
extern pthread_mutex_t	maintenance_lock;
extern volatile time_t	server_reload_time;
extern volatile time_t	collector_reload_time;
extern volatile char   *snapshot_requested;
extern volatile char   *maintenance_requested;

/* queue item for writer */
typedef struct QueueItem	QueueItem;
typedef void (*QueueItemFree)(QueueItem *item);
typedef bool (*QueueItemExec)(QueueItem *item, PGconn *conn, const char *instid);

struct QueueItem
{
	QueueItemFree	free;
	QueueItemExec	exec;
	int				type;	/* queue type */
	int				retry;	/* retry count */
};

/* Log line for logger */
typedef struct Log
{
	const char *timestamp;
	const char *username;
	const char *database;
	const char *pid;
	const char *client_addr;
	const char *session_id;
	const char *session_line_num;
	const char *ps_display;
	const char *session_start;
	const char *vxid;
	const char *xid;
	int			elevel;
	const char *sqlstate;
	const char *message;
	const char *detail;
	const char *hint;
	const char *query;
	const char *query_pos;
	const char *context;
	const char *user_query;
	const char *user_query_pos;
	const char *error_location;
	const char *application_name;
} Log;

/* Contents of pg_statsinfo.control */
typedef struct StatsinfoControlFileData
{
	uint32	control_version;		/* STATSINFO_CONTROL_VERSION */
	char	csv_name[MAXPGPATH];	/* latest parsed csvlog file name */
	long	csv_offset;				/* latest parsed csvlog file offset */
	StatsinfoState	state;			/* see enum above */
	pg_crc32	crc;				/* CRC of all above ... MUST BE LAST! */
} StatsinfoControlFileData;

/* collector.c */
extern void collector_init(void);
extern void *collector_main(void *arg);
extern PGconn *collector_connect(const char *db);
/* snapshot.c */
extern QueueItem *get_snapshot(char *comment);
extern void readopt_from_file(FILE *fp);
extern void readopt_from_db(PGresult *res);

/* logger.c */
extern void logger_init(void);
extern void *logger_main(void *arg);
/* logger_in.c */
extern bool read_csv(FILE *fp, StringInfo buf, int ncolumns, size_t *columns);
extern bool match(const char *str, const char *pattern);
extern List *capture(const char *str, const char *pattern, int nparams);
/* logger_out.c */
extern void write_syslog(const Log *log, const char *prefix,
				PGErrorVerbosity verbosity, const char *ident, int facility);
extern bool write_textlog(const Log *log, const char *prefix,
				PGErrorVerbosity verbosity, FILE *fp);
/* checkpoint.c */
extern bool parse_checkpoint(const char *message, const char *timestamp);
/* autovacuum.c */
extern bool parse_autovacuum(const char *message, const char *timestamp);

/* writer.c */
extern void writer_init(void);
extern void *writer_main(void *arg);
extern void writer_send(QueueItem *item);
extern bool writer_has_queue(WriterQueueType type);
/* maintenance.c */
extern void maintenance_snapshot(time_t repository_keepday);
extern pid_t maintenance_log(const char *command, int *fd_err);
bool check_maintenance_log(pid_t log_maintenance_pid, int fd_err);

/* pg_statsinfod.c */
extern bool postmaster_is_alive(void);
extern PGconn *do_connect(PGconn **conn, const char *info, const char *schema);
extern int str_to_elevel(const char *value);
extern const char *elevel_to_str(int elevel);
extern void shutdown_progress(ShutdownState state);
extern void delay(void);
extern char *getlocaltimestamp(void);
extern int get_server_version(PGconn *conn);

#endif   /* PG_STATSINFOD_H */
