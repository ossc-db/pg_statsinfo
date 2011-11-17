/*
 * snapshot.c:
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

/* XXX: should read SQLs from external files? */
#include "collector_sql.h"
#include "writer_sql.h"

/* snapshot data */
typedef struct Snap
{
	QueueItem	base;

	char	   *comment;	/* snapshot comment, or NULL */
	PGresult   *dbnames;	/* database list { dbid, dbname } */
	List	   *instance;	/* per instance snapshot */
	List	   *dbsnaps;	/* a list of per database snapshot */
	char	   *start;		/* start timestamp */
} Snap;

static const char *instance_gets[] =
{
/*	SQL_SELECT_ACTIVITY,	*/
	SQL_SELECT_TABLESPACE,
	SQL_SELECT_SETTING,
	SQL_SELECT_ROLE,
	SQL_SELECT_CPU,
	SQL_SELECT_DEVICE,
	SQL_SELECT_PROFILE,
	SQL_SELECT_LOCK,
#if PG_VERSION_NUM >= 90100
	SQL_SELECT_REPLICATION,
#endif
/*	SQL_SELECT_STATEMENT,	*/
	NULL
};

static const char *instance_puts[] =
{
	SQL_INSERT_ACTIVITY,
	SQL_INSERT_TABLESPACE,
	SQL_INSERT_SETTING,
	SQL_INSERT_ROLE,
	SQL_INSERT_CPU,
	SQL_INSERT_DEVICE,
	SQL_INSERT_PROFILE,
	SQL_INSERT_LOCK,
#if PG_VERSION_NUM >= 90100
	SQL_INSERT_REPLICATION,
#endif
	SQL_INSERT_STATEMENT,
	NULL
};

static const char *database_gets[] =
{
	SQL_SELECT_SCHEMA,
	SQL_SELECT_TABLE,
	SQL_SELECT_COLUMN,
	SQL_SELECT_INDEX,
	SQL_SELECT_INHERITS,
#if PG_VERSION_NUM >= 80400
	SQL_SELECT_FUNCTION,
#endif
	NULL
};

static const char *database_puts[] =
{
	SQL_INSERT_SCHEMA,
	SQL_INSERT_TABLE,
	SQL_INSERT_COLUMN,
	SQL_INSERT_INDEX,
	SQL_INSERT_INHERITS,
#if PG_VERSION_NUM >= 80400
	SQL_INSERT_FUNCTION,
#endif
	NULL
};

static void Snap_free(Snap *snap);
static bool Snap_exec(Snap *snap, PGconn *conn, const char *instid);
static List *do_gets(PGconn *conn, const char *sql[]);
static PGresult *do_get(PGconn *conn, const char *sql,
						int nParams, const char **params);
static bool do_puts(PGconn *conn, const char *sql[], List *src,
					const char *snapid, const char *dbid);
static bool do_put(PGconn *conn, const char *sql, PGresult *src,
				   const char *snapid, const char *dbid);
static bool has_pg_stat_statements(PGconn *conn);
static bool has_statsrepo_alert(PGconn *conn);

QueueItem *
get_snapshot(char *comment)
{
	PGconn		*conn = NULL;
	PGresult	*activity = NULL;
	Snap		*snap;
	int			 r;
	int			 rows;
	int			 retry;

	/* allocate a new Snap object */
	snap = pgut_new(Snap);
	memset(snap, 0, sizeof(*snap));

	/*
	 * collect instance statistics
	 */
	elog(DEBUG2, "snapshot (instance)");

	/*
	 * start of the snapshot information getting
	 */
	if ((snap->start = getlocaltimestamp()) == NULL)
	{
		Snap_free(snap);
		return NULL;
	}

	for (retry = 0;
		 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
		 delay(), retry++)
	{
		/* connect to postgres database and ensure functions are installed */
		if ((conn = collector_connect(NULL)) == NULL)
			continue;

		/* query activities as a separated transaction. */
		if (activity == NULL)
		{
			activity = do_get(conn, SQL_SELECT_ACTIVITY, 0, NULL);
			if (activity == NULL)
				continue;
		}

		/* enum databases */
		if (snap->dbnames == NULL)
		{
			const char *params[1];

			params[0] = excluded_dbnames;
			snap->dbnames = do_get(conn, SQL_SELECT_DATABASE, 1, params);
			if (snap->dbnames == NULL)
				continue;
		}

		/* query other instance-level statistics */
		snap->instance = do_gets(conn, instance_gets);
		if (snap->instance == NIL)
			continue;

		break;	/* ok */
	}

	if (snap->instance == NIL)
	{
		PQclear(activity);	/* activity has not been assigned yet */
		Snap_free(snap);
		return NULL;
	}

	/* prepend the activity to the instance statistics */
	snap->instance = lcons(activity, snap->instance);

	/* When pg_stat_statements is installed, we collect it */
	if (has_pg_stat_statements(conn))
	{
		PGresult *stmt;

		stmt = pgut_execute(conn, SQL_SELECT_STATEMENT, 0, NULL);
		if (PQresultStatus(stmt) == PGRES_TUPLES_OK)
			snap->instance = lappend(snap->instance, stmt);
		else
			PQclear(stmt);
	}

	/* collect database statistics */
	rows = PQntuples(snap->dbnames);
	for (r = 0; r < rows; r++)
	{
		const char *db = PQgetvalue(snap->dbnames, r, 1);
		List	   *dbsnap = NIL;

		elog(DEBUG2, "snapshot (database=%s)", db);
		for (retry = 0;
			 shutdown_state < SHUTDOWN_REQUESTED && retry < DB_MAX_RETRY;
			 delay(), retry++)
		{
			if ((conn = collector_connect(db)) == NULL ||
				(dbsnap = do_gets(conn, database_gets)) == NIL)
				continue;

			break;	/* ok */
		}

		snap->dbsnaps = lappend(snap->dbsnaps, dbsnap);
	}

	/* ok, fill other fields. */
	snap->base.type = QUEUE_SNAPSHOT;
	snap->base.free = (QueueItemFree) Snap_free;
	snap->base.exec = (QueueItemExec) Snap_exec;
	snap->comment = comment;

	return (QueueItem *) snap;
}

static void
destroy_PGresult_list(List *list)
{
	list_destroy(list, PQclear);
}

static void
Snap_free(Snap *snap)
{
	if (snap)
	{
		free(snap->comment);
		PQclear(snap->dbnames);
		destroy_PGresult_list(snap->instance);
		list_destroy(snap->dbsnaps, destroy_PGresult_list);
		if (snap->start != NULL)
			free(snap->start);
		free(snap);
	}
}

/*
 * insert statstics into the repository server
 */
static bool
Snap_exec(Snap *snap, PGconn *conn, const char *instid)
{
	PGresult   *snapid_res = NULL;
	const char *params[4];
	const char *snapid;
	ListCell   *db;
	int			rows;
	int			i;
	PGresult   *repo_size = NULL;
	PGresult   *update_res = NULL;
	char	   *end = NULL;

	elog(DEBUG2, "write (snapshot)");

	/*
	 * get statsrepo schema relation total size
	 */
	repo_size = do_get(conn, SQL_SELECT_REPOSIZE, 0, NULL);
	if (repo_size == NULL)
		goto error;

	if (pgut_command(conn, "BEGIN", 0, NULL) != PGRES_COMMAND_OK)
		goto error;

	params[0] = instid;
	params[1] = snap->comment;
	snapid_res = pgut_execute(conn, SQL_NEW_SNAPSHOT, 2, params);
	if (PQntuples(snapid_res) == 0)
		goto error;

	snapid = PQgetvalue(snapid_res, 0, 0);

	if (!do_put(conn, SQL_INSERT_DATABASE, snap->dbnames, snapid, NULL))
		goto error;

	if (!do_puts(conn, instance_puts, snap->instance, snapid, NULL))
		goto error;

	i = 0;
	foreach(db, snap->dbsnaps)
	{
		List	   *dbsnap = (List*) lfirst(db);
		const char *dbid = PQgetvalue(snap->dbnames, i ,0);

		if (!do_puts(conn, database_puts, dbsnap, snapid, dbid))
			goto error;
		i++;
	}

	/*
	 * end of the snapshot information getting
	 */
	if ((end = getlocaltimestamp()) == NULL)
		goto error;

	/*
	 * update statsrepo.snapshot
	 */
	params[0] = snapid;
	params[1] = end;
	params[2] = snap->start;
	params[3] = PQgetvalue(repo_size, 0, 0);
	update_res = pgut_execute(conn, SQL_UPDATE_SNAPSHOT, 4, params);
	if (PQresultStatus(update_res) != PGRES_COMMAND_OK)
		goto error;

	if (!pgut_commit(conn))
		goto error;

	/*
	 * call statsrepo.alert(snapid) if exists
	 */
	if (has_statsrepo_alert(conn))
	{
		PGresult *alerts;

		elog(DEBUG2, "run alert(snapid=%s)", snapid);
		alerts = pgut_execute(conn,
							  "SELECT * FROM statsrepo.alert($1)",
							  1, &snapid);
		rows = PQntuples(alerts);
		for (i = 0; i < rows; i++)
			elog(ALERT, "%s", PQgetvalue(alerts, i, 0));
		PQclear(alerts);
	}

	free(end);
	PQclear(snapid_res);
	PQclear(repo_size);
	PQclear(update_res);
	return true;

error:
	if (end != NULL)
		free(end);
	PQclear(snapid_res);
	PQclear(repo_size);
	PQclear(update_res);
	pgut_rollback(conn);
	return false;
}

/*
 * 'sql' must have a NULL sentinel at the end.
 */
static List *
do_gets(PGconn *conn, const char *sql[])
{
	int		i;
	List   *result = NIL;

	if (pgut_command(conn, "BEGIN", 0, NULL) != PGRES_COMMAND_OK)
		goto error;

	for (i = 0; sql[i]; i++)
	{
		PGresult   *res;

		if ((res = do_get(conn, sql[i], 0, NULL)) == NULL)
			goto error;
		result = lappend(result, res);
	}

	if (!pgut_commit(conn))
		goto error;

	return result;	/* ok */

error:
	list_destroy(result, PQclear);
	pgut_rollback(conn);
	return NIL;
}

static PGresult *
do_get(PGconn *conn, const char *sql, int nParams, const char **params)
{
	PGresult *res;

	res = pgut_execute(conn, sql, nParams, params);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		return NULL;
	}

	return res;
}

static bool
do_puts(PGconn *conn,
		const char *sql[],
		List *src,
		const char *snapid,
		const char *dbid)
{
	ListCell   *cell;
	int			i;

	i = 0;
	foreach(cell, src)
	{
		PGresult *res = (PGresult *) lfirst(cell);

		if (res && sql[i])
		{
			if (!do_put(conn, sql[i], res, snapid, dbid))
				return false;
		}
		i++;
	}
	return true;
}

static bool
do_put(PGconn *conn,
	   const char *sql,
	   PGresult *src,
	   const char *snapid,
	   const char *dbid)
{
	const char *params[FUNC_MAX_ARGS];
	int			rows, cols;
	int			r, c;
	int			shift;

	Assert(src);

	cols = PQnfields(src);
	shift = 0;
	params[shift++] = snapid;
	if (dbid)
		params[shift++] = dbid;

	if (shift + cols > FUNC_MAX_ARGS)
	{
		elog(WARNING, "too many columns: %d", cols);
		return false;
	}

	rows = PQntuples(src);
	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < cols; c++)
		{
			if (PQgetisnull(src, r, c))
				params[shift + c] = NULL;
			else
				params[shift + c] = PQgetvalue(src, r, c);
		}

		switch (pgut_command(conn, sql, shift + cols, params))
		{
			case PGRES_COMMAND_OK:
			case PGRES_TUPLES_OK:
				break;
			default:
				return false;
		}
	}

	return true;
}

static bool
has_pg_stat_statements(PGconn *conn)
{
	PGresult   *res;
	bool		result;

	/* check whether pg_stat_statements is installed */
	res = pgut_execute(conn,
			"SELECT relname FROM pg_class"
			" WHERE relname = 'pg_stat_statements' AND relkind = 'v'",
			0, NULL);
	result = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
	PQclear(res);

	return result;
}

static bool
has_statsrepo_alert(PGconn *conn)
{
	PGresult   *res;
	bool		result;

	/* check whether statsrepo.alert is installed */
	res = pgut_execute(conn,
					   "SELECT 1 FROM pg_proc, pg_namespace n"
					   " WHERE nspname = 'statsrepo'"
					   "   AND proname = 'alert'"
					   "   AND pronamespace = n.oid"
					   " LIMIT 1",
					   0, NULL);
	result = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
	PQclear(res);

	return result;
}
