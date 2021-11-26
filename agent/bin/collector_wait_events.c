/*
 * collector_wait_events.c:
 *
 * Copyright (c) 2009-2020, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#include <time.h>

static PGconn  *collector_wait_events_conn = NULL;

static void do_sample_wait_events(void);
static void get_server_encoding(void);
static void collector_wait_events_disconnect(void);
extern bool extract_dbname(const char *conninfo, char *dbname, size_t size);

void
collector_wait_events_init(void)
{
	/* do nothing */
}

/*
 * collector_wait_events_main
 */
void *
collector_wait_events_main(void *arg)
{
	tim			now, prev;

	usleep(1000 * 1000);	/* wait for creating schema statsinfo. */
	
	prev = getlocaltime_ms();

	/* we set actual server encoding to libpq default params. */
	get_server_encoding();

	while (shutdown_state < SHUTDOWN_REQUESTED)
	{
		now = getlocaltime_ms();

		/* sample wait events */
		if (time_ms_diff(now, prev) >= sampling_wait_events_interval)
		{
			elog(DEBUG2, "collector_wait_events_main time_ms_diff %ld sampling_wait_events_interval %d", time_ms_diff(now, prev), sampling_wait_events_interval);
			do_sample_wait_events();
			prev = getlocaltime_ms();
		}

		usleep(1000);	/* 1ms */
	}

	collector_wait_events_disconnect();
	shutdown_progress(COLLECTOR_SHUTDOWN);

	return NULL;
}

static void
do_sample_wait_events(void)
{
	PGconn	   *conn;
	int			retry;

	for (retry = 0;
		 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
		 delay(), retry++)
	{
		/* connect to postgres database and ensure functions are installed */
		if ((conn = collector_wait_events_connect(NULL)) == NULL)
			continue;

		pgut_command(conn, "SELECT statsinfo.sample_wait_events()", 0, NULL);
		break;	/* ok */
	}
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
		if ((conn = collector_wait_events_connect(NULL)) == NULL)
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
collector_wait_events_connect(const char *db)
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
	if (collector_wait_events_conn)
	{
		char	*pgdb;

		pgdb = PQdb(collector_wait_events_conn);
		if (pgdb == NULL || strcmp(pgdb, dbname) != 0)
			collector_wait_events_disconnect();
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
	return do_wait_events_connect(&collector_wait_events_conn, info, schema);
}

static void
collector_wait_events_disconnect(void)
{
	pgut_disconnect(collector_wait_events_conn);
	collector_wait_events_conn = NULL;
}

