/*
 * autovacuum.c : parse auto-vacuum and auto-analyze messages
 *
 * Copyright (c) 2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#define SQL_INSERT_AUTOVACUUM "\
INSERT INTO statsrepo.autovacuum VALUES \
($1, $2::timestamptz - interval '1sec' * $10, \
 $3, $4, $5, $6, $7, $8, $9, $10, $11)"

#define SQL_INSERT_AUTOANALYZE "\
INSERT INTO statsrepo.autoanalyze VALUES \
($1, $2::timestamptz - interval '1sec' * $6, $3, $4, $5, $6)"

/* pg_rusage (rusage is not localized) */
#define MSG_RUSAGE \
	"CPU %fs/%fu sec elapsed %f sec"

#define NUM_AUTOVACUUM		9
#define NUM_AUTOANALYZE		4
#define NUM_RUSAGE			3

/* autovacuum log data */
typedef struct AutovacuumLog
{
	QueueItem	base;

	char	finish[LOGTIME_LEN];
	List   *params;
} AutovacuumLog;

static void Autovacuum_free(AutovacuumLog *av);
static bool Autovacuum_exec(AutovacuumLog *av, PGconn *conn, const char *instid);
static bool Autoanalyze_exec(AutovacuumLog *av, PGconn *conn, const char *instid);

/*
 * parse_autovacuum
 */
bool
parse_autovacuum(const char *message, const char *timestamp)
{
	AutovacuumLog  *av;
	List		   *params;
	List		   *usage;
	const char	   *str_usage;
	QueueItemExec	exec;

	if ((params = capture(message, msg_autovacuum, NUM_AUTOVACUUM)) != NIL)
		exec = (QueueItemExec) Autovacuum_exec;
	else if ((params = capture(message, msg_autoanalyze, NUM_AUTOANALYZE)) != NIL)
		exec = (QueueItemExec) Autoanalyze_exec;
	else
		return false;

	/*
	 * Re-parse rusage output separatedly. Note that MSG_RUSAGE won't be
	 * localized with any lc_messages.
	 */
	str_usage = llast(params);
	if ((usage = capture(str_usage, MSG_RUSAGE, NUM_RUSAGE)) == NIL)
	{
		elog(WARNING, "cannot parse rusage: %s", str_usage);
		list_free_deep(params);
		return false;	/* should not happen */
	}

	av = pgut_new(AutovacuumLog);
	av->base.free = (QueueItemFree) Autovacuum_free;
	av->base.exec = exec;
	strlcpy(av->finish, timestamp, lengthof(av->finish));
	av->params = list_concat(params, usage);

	writer_send((QueueItem *) av);
	return true;
}

static void
Autovacuum_free(AutovacuumLog *av)
{
	if (av)
	{
		list_free_deep(av->params);
		free(av);
	}
}

static bool
Autovacuum_exec(AutovacuumLog *av, PGconn *conn, const char *instid)
{
	const char	   *params[11];

	elog(DEBUG2, "write (autovacuum)");
	Assert(list_length(av->params) == NUM_AUTOVACUUM + NUM_RUSAGE);

	params[0] = instid;
	params[1] = av->finish;					/* finish */
	params[2] = list_nth(av->params, 0);	/* database */
	params[3] = list_nth(av->params, 1);	/* schema */
	params[4] = list_nth(av->params, 2);	/* table */
	params[5] = list_nth(av->params, 3);	/* index_scans */
	params[6] = list_nth(av->params, 4);	/* page_removed */
	params[7] = list_nth(av->params, 5);	/* page_remain */
	params[8] = list_nth(av->params, 6);	/* tup_removed */
	params[9] = list_nth(av->params, 7);	/* tup_remain */
	params[10] = list_nth(av->params, NUM_AUTOVACUUM + 2);	/* duration */

	return pgut_command(conn, SQL_INSERT_AUTOVACUUM,
						lengthof(params), params) == PGRES_COMMAND_OK;
}

static bool
Autoanalyze_exec(AutovacuumLog *av, PGconn *conn, const char *instid)
{
	const char	   *params[6];

	elog(DEBUG2, "write (autoanalyze)");
	Assert(list_length(av->params) == NUM_AUTOANALYZE + NUM_RUSAGE);

	params[0] = instid;
	params[1] = av->finish;					/* finish */
	params[2] = list_nth(av->params, 0);	/* database */
	params[3] = list_nth(av->params, 1);	/* schema */
	params[4] = list_nth(av->params, 2);	/* table */
	params[5] = list_nth(av->params, NUM_AUTOANALYZE + 2);	/* duration */

	return pgut_command(conn, SQL_INSERT_AUTOANALYZE,
						lengthof(params), params) == PGRES_COMMAND_OK;
}
