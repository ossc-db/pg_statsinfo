/*
 * maintenance.c:
 *
 * Copyright (c) 2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfo.h"

#define SQL_MAINTENANCE_PERTITION	"SELECT statsrepo.del_snapshot2(CAST($1 AS TIMESTAMPTZ))"
#define SQL_MAINTENANCE				"SELECT statsrepo.del_snapshot(CAST($1 AS TIMESTAMPTZ))"

typedef struct Maintenance
{
	QueueItem	base;

	time_t		repository_keep_period;
} Maintenance;

static bool Maintenance_exec(Maintenance *maintenance, PGconn *conn, const char *instid);
static void Maintenance_free(Maintenance *maintenance);

/*
 * do_maintenance
 */
void
do_maintenance(time_t repository_keep_period)
{
	Maintenance	*maintenance;

	maintenance = pgut_malloc(sizeof(Maintenance));
	maintenance->base.exec = (QueueItemExec) Maintenance_exec;
	maintenance->base.free = (QueueItemFree) Maintenance_free;
	maintenance->repository_keep_period = repository_keep_period;

	writer_send((QueueItem *) maintenance);
}

static bool
Maintenance_exec(Maintenance *maintenance, PGconn *conn, const char *instid)
{
	char			 timestamp[32];
	const char		*params[1];
	ExecStatusType	 status;
	int				 server_version;

	strftime(timestamp, sizeof(timestamp),
		"%Y-%m-%d %H:%M:%S", localtime(&maintenance->repository_keep_period));
	params[0] = timestamp;

	server_version = get_server_version(conn);

	if (server_version < 0)
		return false;
	else if (server_version >= 80400)
	{
		/* exclusive control during snapshot and maintenance */
		pthread_mutex_lock(&maintenance_lock);
		status = pgut_command(conn, SQL_MAINTENANCE_PERTITION, 1, params);
		pthread_mutex_unlock(&maintenance_lock);
	}
	else
		status = pgut_command(conn, SQL_MAINTENANCE, 1, params);

	if (status != PGRES_TUPLES_OK)
		return false;
	return true;
}

static void
Maintenance_free(Maintenance *maintenance)
{
	free(maintenance);
}
