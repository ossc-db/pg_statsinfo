/*
 * collector.c:
 *
 * Copyright (c) 2009-2020, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#include <time.h>

/* maintenance mode */
#define MaintenanceModeIsSnapshot(mode)	( mode & MAINTENANCE_MODE_SNAPSHOT )
#define MaintenanceModeIsLog(mode)		( mode & MAINTENANCE_MODE_LOG )
#define MaintenanceModeIsRepoLog(mode)	( mode & MAINTENANCE_MODE_REPOLOG )

pthread_mutex_t	reload_lock;
pthread_mutex_t	maintenance_lock;
volatile time_t	collector_reload_time;
volatile char  *snapshot_requested;
volatile char  *maintenance_requested;
volatile char  *postmaster_start_time;

static PGconn  *collector_conn = NULL;

static void reload_params(void);
static void do_sample(void);
static void do_snapshot(char *comment);
static bool update_hardware_info(void);
static void get_server_encoding(void);
static void collector_disconnect(void);
bool extract_dbname(const char *conninfo, char *dbname, size_t size);
static void get_postmaster_start_time(void);

typedef struct HardWareInfo{
	QueueItem	 base;

	PGresult	*cpuinfo;
	PGresult	*meminfo;
} HardWareInfo;
static PGresult *get_hardwareinfo(PGconn *conn, const char *sql);
static void Update_HardwareInfo_free(HardWareInfo *hw);
static bool Update_HardwareInfo_exec(HardWareInfo *hw, PGconn *conn, const char *instid);

void
collector_init(void)
{
	pthread_mutex_init(&reload_lock, NULL);
	pthread_mutex_init(&maintenance_lock, NULL);

	collector_reload_time = time(NULL);
}

/*
 * collector_main
 */
void *
collector_main(void *arg)
{
	time_t		now;
	time_t		next_sample;
	time_t		next_snapshot;
	pid_t		log_maintenance_pid = 0;
	int			fd_err;
	bool		need_hw_update = true;

	now = time(NULL);
	next_sample = get_next_time(now, sampling_interval);
	next_snapshot = get_next_time(now, snapshot_interval);

	/* we set actual server encoding to libpq default params. */
	get_server_encoding();

	/* get postmaster start time */
	get_postmaster_start_time();

	/* if already passed maintenance time, set one day after */
	if (now >= maintenance_time)
		maintenance_time = maintenance_time + (1 * SECS_PER_DAY);

	while (shutdown_state < SHUTDOWN_REQUESTED)
	{
		now = time(NULL);

		/* reload configuration */
		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			reload_params();
			collector_reload_time = now;
		}

		/* sample */
		if (now >= next_sample)
		{
			elog(DEBUG2, "sample (%d sec for next snapshot)", (int) (next_snapshot - now));
			do_sample();
			now = time(NULL);
			next_sample = get_next_time(now, sampling_interval);
		}

		/* snapshot by manual */
		if (snapshot_requested)
		{
			char *comment;

			pthread_mutex_lock(&reload_lock);
			comment = (char *) snapshot_requested;
			snapshot_requested = NULL;
			pthread_mutex_unlock(&reload_lock);

			if (comment)
				do_snapshot(comment);
		}

		/* snapshot by time */
		if (now >= next_snapshot)
		{
			do_snapshot(NULL);
			now = time(NULL);
			next_snapshot = get_next_time(now, snapshot_interval);
		}

		/* maintenance by manual */
		if (maintenance_requested)
		{
			time_t repository_keep_period;

			pthread_mutex_lock(&reload_lock);
			repository_keep_period = atol((char *) maintenance_requested);
			maintenance_requested = NULL;
			pthread_mutex_unlock(&reload_lock);

			maintenance_snapshot(repository_keep_period);
		}

		/* maintenance by time */
		if (enable_maintenance && now >= maintenance_time)
		{
			if (MaintenanceModeIsSnapshot(enable_maintenance))
			{
				time_t repository_keep_period;
				struct tm *tm;

				/* calculate retention period on the basis of today's 0:00 AM */
				tm = localtime(&now);
				tm->tm_hour = 0;
				tm->tm_min = 0;
				tm->tm_sec = 0;
				repository_keep_period = mktime(tm) - ((time_t) repository_keepday * SECS_PER_DAY);

				maintenance_snapshot(repository_keep_period);
			}

			if (MaintenanceModeIsRepoLog(enable_maintenance))
			{
				time_t repolog_keep_period;
				struct tm *tm;

				/* calculate retention period on the basis of today's 0:00 AM */
				tm = localtime(&now);
				tm->tm_hour = 0;
				tm->tm_min = 0;
				tm->tm_sec = 0;
				repolog_keep_period = mktime(tm) - ((time_t) repolog_keepday * SECS_PER_DAY);

				maintenance_repolog(repolog_keep_period);
			}

			if (MaintenanceModeIsLog(enable_maintenance))
			{
				if (log_maintenance_pid <= 0)
				{
					if ((log_maintenance_pid = maintenance_log(log_maintenance_command, &fd_err)) < 0)
						elog(ERROR, "could not run the log maintenance command");
				}
				else
					elog(WARNING,
						"previous log maintenance is not complete, "
						"so current log maintenance was skipped");
			}

			maintenance_time = maintenance_time + (1 * SECS_PER_DAY);
		}

		/* check the status of log maintenance command */
		if (log_maintenance_pid > 0 &&
			check_maintenance_log(log_maintenance_pid, fd_err))
		{
			/* log maintenance command has been completed */
			log_maintenance_pid = 0;
		}

		if (need_hw_update)
		{
			if (update_hardware_info())
				need_hw_update = false;
		}

		usleep(200 * 1000);	/* 200ms */
	}

	collector_disconnect();
	shutdown_progress(COLLECTOR_SHUTDOWN);

	return NULL;
}

static void
reload_params(void)
{
	char	*prev_target_server;

	pthread_mutex_lock(&reload_lock);

	prev_target_server = pgut_strdup(target_server);

	/* read configuration from launcher */
	readopt_from_file(stdin);

	/* if already passed maintenance time, set one day after */
	if (time(NULL) >= maintenance_time)
		maintenance_time = maintenance_time + (1 * SECS_PER_DAY);

	/* if the target_server has changed then disconnect current connection */
	if (strcmp(target_server, prev_target_server) != 0)
		collector_disconnect();

	free(prev_target_server);

	pthread_mutex_unlock(&reload_lock);
}

static void
do_sample(void)
{
	PGconn	   *conn;
	int			retry;

	for (retry = 0;
		 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
		 delay(), retry++)
	{
		/* connect to postgres database and ensure functions are installed */
		if ((conn = collector_connect(NULL)) == NULL)
			continue;

		pgut_command(conn, "SELECT statsinfo.sample()", 0, NULL);
		break;	/* ok */
	}
}

/*
 * ownership of comment will be granted to snapshot item.
 */
static void
do_snapshot(char *comment)
{
	QueueItem	*snap = NULL;

	/* skip current snapshot if previous snapshot still not complete */
	if (writer_has_queue(QUEUE_SNAPSHOT))
	{
		elog(WARNING, "previous snapshot is not complete, so current snapshot was skipped");
		free(comment);
		return;
	}

	/* exclusive control during snapshot and maintenance */
	pthread_mutex_lock(&maintenance_lock);
	snap = get_snapshot(comment);
	pthread_mutex_unlock(&maintenance_lock);

	if (snap != NULL)
		writer_send(snap);
	else
		free(comment);
}

static bool
update_hardware_info(void)
{
	PGconn			*conn;
	PGresult		*res_cpu;
	PGresult		*res_mem;

	/* connect to postgres database and ensure functions are installed */
	if ((conn = collector_connect(NULL)) == NULL)
		return false;

	/* get hardware information */
	res_cpu = get_hardwareinfo(conn,
		"SELECT "
		"  vendor_id, model_name, cpu_mhz, processors, "
		"  threads_per_core, cores_per_socket, sockets "
		"FROM statsinfo.cpuinfo()");

	res_mem = get_hardwareinfo(conn,
		"SELECT mem_total FROM statsinfo.meminfo()");

	if ((res_cpu) && (res_mem))
	{
		/* request update of hardware information */
		HardWareInfo	*hwinfo;

		hwinfo = pgut_new(HardWareInfo);
		hwinfo->base.type = QUEUE_HWINFO;
		hwinfo->base.free = (QueueItemFree) Update_HardwareInfo_free;
		hwinfo->base.exec = (QueueItemExec) Update_HardwareInfo_exec;
		hwinfo->cpuinfo = res_cpu;
		hwinfo->meminfo = res_mem;

		writer_send((QueueItem *) hwinfo);
		return true;
	}
	else
	{
		PQclear(res_cpu);
		PQclear(res_mem);
		return false;
	}
}

static PGresult *
get_hardwareinfo(PGconn *conn, const char *sql)
{
	PGresult	*res;

	res = pgut_execute(conn, sql, 0, NULL);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		return NULL;
	}
	return res;
}

static void
Update_HardwareInfo_free(HardWareInfo *hwinfo)
{
	if (hwinfo)
	{
		PQclear(hwinfo->cpuinfo);
		PQclear(hwinfo->meminfo);
		free(hwinfo);
	}
}

/*
 * Update the repository only on the first boot or when the hardware information changes.
 */
static bool
Update_HardwareInfo_exec(HardWareInfo *hwinfo, PGconn *conn, const char *instid)
{
	ExecStatusType	 res;

	const char	*params[8];
	const char	*sql;

	if (pgut_command(conn, "BEGIN", 0, NULL) != PGRES_COMMAND_OK)
		goto error;
	
	/* cpu information */
	if (hwinfo->cpuinfo)
	{
		Assert( PQnfields(hwinfo->cpuinfo) == 7 );

		params[0] = instid;
		params[1] = PQgetvalue(hwinfo->cpuinfo, 0, 0); /* vendor_id */
		params[2] = PQgetvalue(hwinfo->cpuinfo, 0, 1); /* model_name */
		params[3] = PQgetvalue(hwinfo->cpuinfo, 0, 2); /* cpu_mhz */
		params[4] = PQgetvalue(hwinfo->cpuinfo, 0, 3); /* processors */
		params[5] = PQgetvalue(hwinfo->cpuinfo, 0, 4); /* threads_per_core */
		params[6] = PQgetvalue(hwinfo->cpuinfo, 0, 5); /* cores_per_socket */
		params[7] = PQgetvalue(hwinfo->cpuinfo, 0, 6); /* sockets */

		sql = 
			"WITH "
			"  ic (vendor_id, model_name, cpu_mhz, processors, threads_per_core, cores_per_socket, sockets) "
			"    AS (VALUES ($2::text, $3::text, $4::real, $5::integer, $6::integer, $7::integer, $8::integer)), "
			"  r1 AS ("
			"    SELECT ic.vendor_id, ic.model_name, ic.processors, ic.sockets FROM ic ),"
			"  r2 AS ("
			"    SELECT rc.vendor_id, rc.model_name, rc.processors, rc.sockets FROM statsrepo.cpuinfo rc"
			"    WHERE instid = $1"
			"      AND timestamp = (SELECT pg_catalog.max(timestamp) FROM statsrepo.cpuinfo WHERE instid = $1) ) "
			"INSERT INTO statsrepo.cpuinfo"
			"  (instid, timestamp, vendor_id, model_name, cpu_mhz, "
			"   processors, threads_per_core, cores_per_socket, sockets) "
			"SELECT $1, pg_catalog.transaction_timestamp(), t.vendor_id, t.model_name,"
			"       ic.cpu_mhz, t.processors, ic.threads_per_core, ic.cores_per_socket, t.sockets "
			"FROM (SELECT * FROM r1 EXCEPT SELECT * FROM r2) t, ic";

		res = pgut_command(conn, sql, 8, params);
		if (res != PGRES_COMMAND_OK)
			goto error;
	}

	/* memory information */
	if (hwinfo->cpuinfo)
	{
		Assert( PQnfields(hwinfo->meminfo) == 1 );

		params[0] = instid;
		params[1] = PQgetvalue(hwinfo->meminfo, 0, 0); /* mem_total */

		sql = 
			"WITH"
			"  r1 (mem_total) AS (VALUES($2::bigint)),"
			"  r2 AS ("
			"    SELECT rm.mem_total FROM statsrepo.meminfo rm"
			"    WHERE instid = $1"
			"      AND timestamp = (SELECT pg_catalog.max(timestamp) FROM statsrepo.meminfo WHERE instid = $1) ) "
			"INSERT INTO statsrepo.meminfo (instid, timestamp, mem_total) "
			"SELECT $1, pg_catalog.transaction_timestamp(), t.mem_total "
			"FROM (SELECT * FROM r1 EXCEPT SELECT * FROM r2) t";

		res = pgut_command(conn, sql, 2, params);
		if (res != PGRES_COMMAND_OK)
			goto error;
	}
	
	if (!pgut_commit(conn))
		goto error;

	return true;

error:
	pgut_rollback(conn);
	return false;
}
/*
 * set server encoding
 */
static void
get_server_encoding(void)
{
	PGconn		*conn;
	int			 retry;
	const char	*encode;

	for (retry = 0;
		 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
		 delay(), retry++)
	{
		/* connect postgres database */
		if ((conn = collector_connect(NULL)) == NULL)
			continue;

		/* 
		 * if we could not find the encodig-string, it's ok.
		 * because PG_SQL_ASCII was already set.
		 */ 
		encode = PQparameterStatus(conn, "server_encoding");
		if (encode != NULL)
			pgut_putenv("PGCLIENTENCODING", encode);
		elog(DEBUG2, "collector set client_encoding : %s", encode);
		break;	/* ok */
	}
}

PGconn *
collector_connect(const char *db)
{
	char		 dbname[NAMEDATALEN];
	char		 info[1024];
	const char	*schema;

	if (db == NULL)
	{
		if (!extract_dbname(target_server, dbname, sizeof(dbname)))
				strncpy(dbname, "postgres", sizeof(dbname));	/* default database */
		schema = "statsinfo";
	}
	else
	{
		strncpy(dbname, db, sizeof(dbname));
		/* no schema required */
		schema = NULL;
	}

	/* disconnect if need to connect another database */
	if (collector_conn)
	{
		char	*pgdb;

		pgdb = PQdb(collector_conn);
		if (pgdb == NULL || strcmp(pgdb, dbname) != 0)
			collector_disconnect();
	}
	else
	{
		ControlFileData	ctrl;

		readControlFile(&ctrl, data_directory);

		/* avoid connection fails during recovery and warm-standby */
		switch (ctrl.state)
		{
			case DB_IN_PRODUCTION:
//(!)
			case DB_IN_ARCHIVE_RECOVERY:	/* hot-standby accepts connections */
//(!)
				break;			/* ok, do connect */
			default:
				delay();
				return NULL;	/* server is not ready for accepting connections */
		}
	}

#ifdef DEBUG_MODE
	snprintf(info, lengthof(info), "port=%s %s dbname=%s",
		postmaster_port, target_server, dbname);
#else
	snprintf(info, lengthof(info),
		"port=%s %s dbname=%s options='-c log_statement=none'",
		postmaster_port, target_server, dbname);
#endif
	return do_connect(&collector_conn, info, schema);
}

static void
collector_disconnect(void)
{
	pgut_disconnect(collector_conn);
	collector_conn = NULL;
}

bool
extract_dbname(const char *conninfo, char *dbname, size_t size)
{
	PQconninfoOption	*options;
	PQconninfoOption	*option;

	if ((options = PQconninfoParse(conninfo, NULL)) == NULL)
		return false;

	for (option = options; option->keyword != NULL; option++)
	{
		if (strcmp(option->keyword, "dbname") == 0)
		{
			if (option->val != NULL && option->val[0] != '\0')
			{
				strncpy(dbname, option->val, size);
				PQconninfoFree(options);
				return true;
			}
		}
	}

	PQconninfoFree(options);
	return false;
}

static void
get_postmaster_start_time(void)
{
	PGconn		*conn;
	PGresult	*res;
	int			 retry;

	for (retry = 0;
		 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
		 delay(), retry++)
	{
		/* connect postgres database */
		if ((conn = collector_connect(NULL)) == NULL)
			continue;

		res =  pgut_execute(conn, "SELECT pg_postmaster_start_time()", 0, NULL);
		if (PQresultStatus(res) == PGRES_TUPLES_OK)
		{
			postmaster_start_time = pgut_strdup(PQgetvalue(res, 0, 0));
			PQclear(res);
			break;	/* ok */
		}
		PQclear(res);
	}
}
