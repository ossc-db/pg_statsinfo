/*
 * report.c
 *
 * Copyright (c) 2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfo.h"

#include <time.h>

#define SNAPSHOTID_MIN	"1"
#define SNAPSHOTID_MAX	"9223372036854775807"
#define TIMESTAMP_MIN	"4713-01-01 BC 00:00:00 UTC"
#define TIMESTAMP_MAX	"294276-12-31 23:59:59 UTC"

#define SQL_SELECT_SUMMARY						"SELECT * FROM statsrepo.get_summary($1, $2)"
#define SQL_SELECT_DBSTATS						"SELECT * FROM statsrepo.get_dbstats($1, $2)"
#define SQL_SELECT_XACT_TENDENCY "\
	SELECT \
		snapid, \
		datname, \
		commit_tps::numeric(1000, 3), \
		rollback_tps::numeric(1000, 3) \
	FROM \
		statsrepo.get_xact_tendency($1, $2)"

#define SQL_SELECT_DBSIZE_TENDENCY "\
	SELECT \
		snapid, \
		datname, \
		size::numeric(1000, 3) \
	FROM \
		statsrepo.get_dbsize_tendency($1, $2)"

#define SQL_SELECT_INSTANCE_PROC_RATIO			"SELECT * FROM statsrepo.get_proc_ratio($1, $2)"
#define SQL_SELECT_INSTANCE_PROC_TENDENCY "\
	SELECT \
		snapid, \
		idle::numeric(1000, 3), \
		idle_in_xact::numeric(1000, 3), \
		waiting::numeric(1000, 3), \
		running::numeric(1000, 3) \
	FROM \
		statsrepo.get_proc_tendency($1, $2)"

#define SQL_SELECT_CPU_USAGE					"SELECT * FROM statsrepo.get_cpu_usage($1, $2)"
#define SQL_SELECT_CPU_USAGE_TENDENCY			"SELECT * FROM statsrepo.get_cpu_usage_tendency($1, $2)"
#define SQL_SELECT_IO_USAGE						"SELECT * FROM statsrepo.get_io_usage($1, $2)"
#define SQL_SELECT_IO_USAGE_TENDENCY			"SELECT * FROM statsrepo.get_io_usage_tendency($1, $2)"
#define SQL_SELECT_DISK_USAGE_TABLESPACE		"SELECT * FROM statsrepo.get_disk_usage_tablespace($1, $2)"
#define SQL_SELECT_DISK_USAGE_TABLE				"SELECT * FROM statsrepo.get_disk_usage_table($1, $2) LIMIT 10"
#define SQL_SELECT_LONG_TRANSACTIONS			"SELECT * FROM statsrepo.get_long_transactions($1, $2)"
#define SQL_SELECT_HEAVILY_UPDATED_TABLES		"SELECT * FROM statsrepo.get_heavily_updated_tables($1, $2) LIMIT 20"
#define SQL_SELECT_HEAVILY_ACCESSED_TABLES		"SELECT * FROM statsrepo.get_heavily_accessed_tables($1, $2) LIMIT 20"
#define SQL_SELECT_LOW_DENSITY_TABLES			"SELECT * FROM statsrepo.get_low_density_tables($1, $2) LIMIT 10"
#define SQL_SELECT_FRAGMENTED_TABLES			"SELECT * FROM statsrepo.get_flagmented_tables($1, $2)"
#define SQL_SELECT_CHECKPOINT_ACTIVITY			"SELECT * FROM statsrepo.get_checkpoint_activity($1, $2)"
#define SQL_SELECT_AUTOVACUUM_ACTIVITY			"SELECT * FROM statsrepo.get_autovacuum_activity($1, $2)"
#define SQL_SELECT_QUERY_ACTIVITY_FUNCTIONS		"SELECT * FROM statsrepo.get_query_activity_functions($1, $2) LIMIT 20"
#define SQL_SELECT_QUERY_ACTIVITY_STATEMENTS	"SELECT * FROM statsrepo.get_query_activity_statements($1, $2) LIMIT 20"
#define SQL_SELECT_SETTING_PARAMETERS			"SELECT * FROM statsrepo.get_setting_parameters($1, $2)"
#define SQL_SELECT_SCHEMA_INFORMATION_TABLES	"SELECT * FROM statsrepo.get_schema_info_tables($1, $2)"
#define SQL_SELECT_SCHEMA_INFORMATION_INDEXES	"SELECT * FROM statsrepo.get_schema_info_indexes($1, $2)"
#define SQL_SELECT_PROFILES						"SELECT * FROM statsrepo.get_profiles($1, $2)"

#define SQL_SELECT_REPORT_SCOPE_BY_SNAPID "\
	SELECT \
		i.instid, \
		i.hostname, \
		i.port, \
		min(s.snapid), \
		max(s.snapid) \
	FROM \
		statsrepo.snapshot s \
		LEFT JOIN statsrepo.instance i ON s.instid = i.instid \
	WHERE \
		s.snapid BETWEEN $1 AND $2 \
	GROUP BY \
		i.instid, \
		i.hostname, \
		i.port \
	ORDER BY \
		i.instid"

#define SQL_SELECT_REPORT_SCOPE_BY_TIMESTAMP "\
	SELECT \
		i.instid, \
		i.hostname, \
		i.port, \
		min(s.snapid), \
		max(s.snapid) \
	FROM \
		statsrepo.snapshot s \
		LEFT JOIN statsrepo.instance i ON s.instid = i.instid \
	WHERE \
		s.time BETWEEN $1 AND $2 \
	GROUP BY \
		i.instid, \
		i.hostname, \
		i.port \
	ORDER BY \
		i.instid"

/* the report scope per instance */
typedef struct ReportScope
{
	char	*instid;	/* instance ID */
	char	*host;		/* host */
	char	*port;		/* port */
	char	*beginid;	/* begin point of report */
	char	*endid;		/* end point of report */
} ReportScope;

/* function interface of the report builder */
typedef void (*ReportBuild)(PGconn *conn, ReportScope *scope, FILE *out);

/* report builder functions */
static void report_summary(PGconn *conn, ReportScope *scope, FILE *out);
static void report_database_statistics(PGconn *conn, ReportScope *scope, FILE *out);
static void report_instance_activity(PGconn *conn, ReportScope *scope, FILE *out);
static void report_resource_usage(PGconn *conn, ReportScope *scope, FILE *out);
static void report_disk_usage(PGconn *conn, ReportScope *scope, FILE *out);
static void report_long_transactions(PGconn *conn, ReportScope *scope, FILE *out);
static void report_notable_tables(PGconn *conn, ReportScope *scope, FILE *out);
static void report_checkpoint_activity(PGconn *conn, ReportScope *scope, FILE *out);
static void report_autovacuum_activity(PGconn *conn, ReportScope *scope, FILE *out);
static void report_query_activity(PGconn *conn, ReportScope *scope, FILE *out);
static void report_setting_parameters(PGconn *conn, ReportScope *scope, FILE *out);
static void report_schema_information(PGconn *conn, ReportScope *scope, FILE *out);
static void report_profiles(PGconn *conn, ReportScope *scope, FILE *out);
static void report_all(PGconn *conn, ReportScope *scope, FILE *out);

static ReportBuild parse_reportid(const char *value);
static List *select_scope_by_snapid(PGconn *conn, const char *beginid, const char *endid);
static List *select_scope_by_timestamp(PGconn *conn, time_t begindate, time_t enddate);
static void destroy_report_scope(ReportScope *scope);

/*
 * generate a report
 */
void
do_report(PGconn *conn,
		  const char *reportid,
		  const char *instid,
		  const char *beginid,
		  const char *endid,
		  time_t begindate,
		  time_t enddate,
		  const char *filename)
{
	ReportBuild	 reporter;
	List		*scope_list;
	ListCell	*cell;
	FILE		*out = stdout;
	int64		 b_id, e_id, i_id;

	/* validate parameters */
	if (beginid && (!parse_int64(beginid, &b_id) || b_id <= 0))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("invalid snapshot ID (--beginid) : '%s'", beginid)));
	if (endid && (!parse_int64(endid, &e_id) || e_id <= 0))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("invalid snapshot ID (--endid) : '%s'", endid)));
	if (instid && (!parse_int64(instid, &i_id) || i_id <= 0))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("invalid instance ID (--instid) : '%s'", instid)));
	if ((beginid && endid) && b_id > e_id)
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("--endid must be greater than --beginid")));
	if ((beginid || endid) && (begindate != -1 || enddate != -1))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("can't specify both snapshot ID and timestamp")));

	/* parse report ID */
	reporter = parse_reportid(reportid);

	/* change the output destination */
	if (filename && (out = fopen(filename, "w")) == NULL)
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not open file : '%s'", filename)));

	/* get the report scope of each instance */
	if (beginid || endid)
		scope_list = select_scope_by_snapid(conn, beginid, endid);
	else if (begindate != -1 || enddate != -1)
		scope_list = select_scope_by_timestamp(conn, begindate, enddate);
	else
		scope_list = select_scope_by_snapid(conn, SNAPSHOTID_MIN, SNAPSHOTID_MAX);

	/* generate report */
	foreach(cell, scope_list)
	{
		ReportScope	*scope = (ReportScope *) lfirst(cell);

		/* if instance ID is specified, skip non-target */
		if (instid && strcmp(instid, scope->instid) != 0)
			continue;
		/* don't generate report from same snapshot */
		if (strcmp(scope->beginid, scope->endid) == 0)
			continue;

		/* report header */
		fprintf(out, "---------------------------------------------\n");
		fprintf(out, "STATSINFO Report (host: %s, port: %s)\n", scope->host, scope->port);
		fprintf(out, "---------------------------------------------\n\n");

		reporter(conn, scope, out);
	}

	/* cleanup */
	list_destroy(scope_list, destroy_report_scope);
	if (out != stdout)
		fclose(out);
}

/*
 * generate a report that corresponds to 'Summary'
 */
static void
report_summary(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Summary */\n");
	fprintf(out, "----------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_SUMMARY, lengthof(params), params);
	if (PQntuples(res) == 0)
		return;
	fprintf(out, "Database System ID   : %s\n", PQgetvalue(res, 0, 0));
	fprintf(out, "Host                 : %s\n", PQgetvalue(res, 0, 1));
	fprintf(out, "Port                 : %s\n", PQgetvalue(res, 0, 2));
	fprintf(out, "PostgreSQL Version   : %s\n", PQgetvalue(res, 0, 3));
	fprintf(out, "Snapshot Begin       : %s\n", PQgetvalue(res, 0, 4));
	fprintf(out, "Snapshot End         : %s\n", PQgetvalue(res, 0, 5));
	fprintf(out, "Snapshot Duration    : %s\n", PQgetvalue(res, 0, 6));
	fprintf(out, "Total Database Size  : %s\n", PQgetvalue(res, 0, 7));
	fprintf(out, "Total Commits        : %s\n", PQgetvalue(res, 0, 8));
	fprintf(out, "Total Rollbacks      : %s\n\n", PQgetvalue(res, 0, 9));
	PQclear(res);
}

/*
 * generate a report that corresponds to 'DatabaseStatistics'
 */
static void
report_database_statistics(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Database Statistics */\n");
	fprintf(out, "----------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_DBSTATS, lengthof(params), params);
	for (i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "Database Name              : %s\n", PQgetvalue(res, i, 0));
		fprintf(out, "Database Size              : %s MB\n", PQgetvalue(res, i, 1));
		fprintf(out, "Database Size Increase     : %s MB\n", PQgetvalue(res, i, 2));
		fprintf(out, "Commit/s                   : %s\n", PQgetvalue(res, i, 3));
		fprintf(out, "Rollback/s                 : %s\n", PQgetvalue(res, i, 4));
		fprintf(out, "Cache Hit Ratio            : %s %%\n", PQgetvalue(res, i, 5));
		fprintf(out, "Block Read/s (disk+cache)  : %s\n", PQgetvalue(res, i, 6));
		fprintf(out, "Block Read/s (disk)        : %s\n", PQgetvalue(res, i, 7));
		fprintf(out, "Rows Read/s                : %s\n\n", PQgetvalue(res, i, 8));
	}
	PQclear(res);

	fprintf(out, "/** Transaction Statistics **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%10s  %-16s  %12s  %12s\n",
		"SnapshotID", "Database", "Commit/s", "Rollback/s");
	fprintf(out, "-----------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_XACT_TENDENCY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%10s  %-16s  %12s  %12s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Database Size **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%10s  %-16s  %13s\n",
		"SnapshotID", "Database", "Size");
	fprintf(out, "----------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_DBSIZE_TENDENCY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%10s  %-16s  %10s MB\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'InstanceActivity'
 */
static void
report_instance_activity(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Instance Activity */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** Instance Processes Ratio **/\n");
	fprintf(out, "-----------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_INSTANCE_PROC_RATIO, lengthof(params), params);
	if (PQntuples(res) == 0)
		return;
	fprintf(out, "Back-end Idle Ratio          : %s %%\n", PQgetvalue(res, 0, 0));
	fprintf(out, "Back-end Idle In Xact Ratio  : %s %%\n", PQgetvalue(res, 0, 1));
	fprintf(out, "Back-end Waiting Ratio       : %s %%\n", PQgetvalue(res, 0, 2));
	fprintf(out, "Back-end Running Ratio       : %s %%\n\n", PQgetvalue(res, 0, 3));
	PQclear(res);

	fprintf(out, "/** Instance Processes **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%10s  %12s  %12s  %12s  %12s\n",
		"SnapshotID", "Idle", "Idle In Xact", "Waiting", "Running");
	fprintf(out, "-----------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_INSTANCE_PROC_TENDENCY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%10s  %12s  %12s  %12s  %12s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'OSResourceUsage'
 */
static void
report_resource_usage(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* OS Resource Usage */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** CPU Usage **/\n");
	fprintf(out, "-----------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_CPU_USAGE, lengthof(params), params);
	if (PQntuples(res) == 0)
		return;
	fprintf(out, "User Mode Ratio    : %s %%\n", PQgetvalue(res, 0, 0));
	fprintf(out, "System Mode Ratio  : %s %%\n", PQgetvalue(res, 0, 1));
	fprintf(out, "Idle Mode Ratio    : %s %%\n", PQgetvalue(res, 0, 2));
	fprintf(out, "IOwait Mode Ratio  : %s %%\n\n", PQgetvalue(res, 0, 3));
	PQclear(res);

	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%10s  %10s  %10s  %10s  %10s\n",
		"SnapshotID", "User", "System", "Idle", "IOwait");
	fprintf(out, "-------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_CPU_USAGE_TENDENCY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%10s  %8s %%  %8s %%  %8s %%  %8s %%\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** IO Usage **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-12s  %-24s  %11s  %11s  %17s  %17s  %16s  %15s\n",
		"Device", "Including TabelSpaces", "Total Read", "Total Write",
		"Total Read Time", "Total Write Time", "Current IO Queue", "Total IO Time");
	fprintf(out, "--------------------------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_IO_USAGE, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-12s  %-24s  %8s MB  %8s MB  %14s ms  %14s ms  %16s  %12s ms\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%10s  %-12s  %15s  %15s  %15s  %15s\n",
		"SnapshotID", "Device", "Read Size/s", "Write Size/s", "Read Time/s", "Write Time/s");
	fprintf(out, "-----------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_IO_USAGE_TENDENCY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%10s  %-12s  %12s KB  %12s KB  %12s ms  %12s ms\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'DiskUsage'
 */
static void
report_disk_usage(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Disk Usage */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** Disk Usage per Tablespace **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-32s  %-12s  %11s  %11s  %10s\n",
		"Tablespace", "Location", "Device", "Used", "Avail", "Remain");
	fprintf(out, "---------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_DISK_USAGE_TABLESPACE, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-32s  %-12s  %8s MB  %8s MB  %8s %%\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Disk Usage per Table **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %11s  %12s  %12s  %12s\n",
		"Database", "Schema", "Table", "Size", "Table Reads", "Index Reads", "Toast Reads");
	fprintf(out, "--------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_DISK_USAGE_TABLE, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %8s MB  %12s  %12s  %12s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'LongTransactions'
 */
static void
report_long_transactions(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Long Transactions */\n");
	fprintf(out, "----------------------------------------\n");
	fprintf(out, "%-8s  %-15s  %20s  %10s  %-32s\n",
		"PID", "Client Address", "When To Start", "Duration", "Query");
	fprintf(out, "-----------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_LONG_TRANSACTIONS, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-8s  %-15s  %20s  %8s s  %-32s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'NotableTables'
 */
static void
report_notable_tables(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Notable Tables */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** Heavily Updated Tables **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %12s  %12s  %12s  %12s  %10s\n",
		"Database", "Schema", "Table", "INSERT Rows", "UPDATE Rows", "DELETE Rows",
		"Total Rows", "HOT Ratio");
	fprintf(out, "---------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_HEAVILY_UPDATED_TABLES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %12s  %12s  %12s  %12s  %8s %%\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Heavily Accessed Tables **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %12s  %12s  %14s  %17s\n",
		"Database", "Schema", "Table", "Seq Scans", "Read Rows", "Read Rows/Scan",
		"Cache Hit Ratio");
	fprintf(out, "----------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_HEAVILY_ACCESSED_TABLES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %12s  %12s  %14s  %15s %%\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Low Density Tables **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %12s  %14s  %14s  %20s\n",
		"Database", "Schema", "Table", "Live Tuples", "Logical Pages", "Physical Pages",
		"Logical Page Ratio");
	fprintf(out, "---------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_LOW_DENSITY_TABLES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %12s  %14s  %14s  %18s %%\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Fragmented Tables **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %-16s  %12s\n",
		"Database", "Schema", "Table", "Column", "Correlation");
	fprintf(out, "---------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_FRAGMENTED_TABLES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %-16s  %12s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'CheckpointActivity'
 */
static void
report_checkpoint_activity(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Checkpoint Activity */\n");
	fprintf(out, "----------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_CHECKPOINT_ACTIVITY, lengthof(params), params);
	if (PQntuples(res) == 0)
		return;
	fprintf(out, "Total Checkpoints        : %s\n", PQgetvalue(res, 0, 0));
	fprintf(out, "Checkpoints By Time      : %s\n", PQgetvalue(res, 0, 1));
	fprintf(out, "Checkpoints By XLOG      : %s\n", PQgetvalue(res, 0, 2));
	fprintf(out, "Written Buffers Average  : %s\n", PQgetvalue(res, 0, 3));
	fprintf(out, "Written Buffers Maximum  : %s\n", PQgetvalue(res, 0, 4));
	fprintf(out, "Write Duration Average   : %s sec\n", PQgetvalue(res, 0, 5));
	fprintf(out, "Write Duration Maximum   : %s sec\n\n", PQgetvalue(res, 0, 6));
	PQclear(res);
}

/*
 * generate a report that corresponds to 'AutovacuumActivity'
 */
static void
report_autovacuum_activity(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Autovacuum Activity */\n");
	fprintf(out, "----------------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %8s  %16s  %17s  %16s  %15s  %15s\n",
		"Database", "Schema", "Table", "Count", "Index Scans(Avg)",
		"Removed Rows(Avg)", "Remain Rows(Avg)", "Duration(Avg)", "Duration(Max)");
	fprintf(out, "----------------------------------------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_AUTOVACUUM_ACTIVITY, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %8s  %16s  %17s  %16s  %13s s  %13s s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7),
			PQgetvalue(res, i, 8));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'QueryActivity'
 */
static void
report_query_activity(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Query Activity */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** Functions **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-8s  %-16s  %-16s  %-16s  %8s  %13s  %12s  %12s\n",
		"OID", "Database", "Schema", "Function", "Calls", "Total Time",
		"Self Time", "Time/Call");
	fprintf(out, "----------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_QUERY_ACTIVITY_FUNCTIONS, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-8s  %-16s  %-16s  %-16s  %8s  %10s ms  %9s ms  %9s ms\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Statements **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %8s  %13s  %12s  %-s\n",
		"User", "Database", "Calls", "Total Time", "Time/Call", "Query");
	fprintf(out, "--------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_QUERY_ACTIVITY_STATEMENTS, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %8s  %10s ms  %9s ms  %-s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 2));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'SettingParameters'
 */
static void
report_setting_parameters(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Setting Parameters */\n");
	fprintf(out, "----------------------------------------\n");
	fprintf(out, "%-32s  %-32s  %-s\n",
		"Name", "Setting", "Source");
	fprintf(out, "-----------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_SETTING_PARAMETERS, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-32s  %-32s  %-s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'SchemaInformation'
 */
static void
report_schema_information(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Schema Information */\n");
	fprintf(out, "----------------------------------------\n\n");

	fprintf(out, "/** Tables **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %8s  %14s  %9s  %12s  %11s  %11s\n",
		"Database", "Schema", "Table", "Columns", "Row Width", "Size",
		"Size Incr", "Table Scans", "Index Scans");
	fprintf(out, "------------------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_SCHEMA_INFORMATION_TABLES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %8s  %9s byte  %6s MB  %9s MB  %11s  %11s\n",
			PQgetvalue(res, i, 0),	
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7),
			PQgetvalue(res, i, 8));
	}
	fprintf(out, "\n");
	PQclear(res);

	fprintf(out, "/** Indexes **/\n");
	fprintf(out, "-----------------------------------\n");
	fprintf(out, "%-16s  %-16s  %-16s  %-16s  %9s  %12s  %11s  %9s  %10s  %11s  %-s\n",
		"Database", "Schema", "Index", "Table", "Size", "Size Incr",
		"Index Scans", "Rows/Scan", "Disk Reads", "Cache Reads", "Index Key");
	fprintf(out, "--------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_SCHEMA_INFORMATION_INDEXES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-16s  %-16s  %-16s  %-16s  %6s MB  %9s MB  %11s  %9s  %10s  %11s  %-s\n",
			PQgetvalue(res, i, 0),	
			PQgetvalue(res, i, 1),
			PQgetvalue(res, i, 2),
			PQgetvalue(res, i, 3),
			PQgetvalue(res, i, 4),
			PQgetvalue(res, i, 5),
			PQgetvalue(res, i, 6),
			PQgetvalue(res, i, 7),
			PQgetvalue(res, i, 8),
			PQgetvalue(res, i, 9),
			PQgetvalue(res, i, 10));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'Profiles'
 */
static void
report_profiles(PGconn *conn, ReportScope *scope, FILE *out)
{
	PGresult	*res;
	const char	*params[] = { scope->beginid, scope->endid };
	int			 i;

	fprintf(out, "----------------------------------------\n");
	fprintf(out, "/* Profiles */\n");
	fprintf(out, "----------------------------------------\n");
	fprintf(out, "%-32s  %8s\n",
		"Processing", "Executes");
	fprintf(out, "---------------------------------------------\n");

	res = pgut_execute(conn, SQL_SELECT_PROFILES, lengthof(params), params);
	for(i = 0; i < PQntuples(res); i++)
	{
		fprintf(out, "%-32s  %8s\n",
			PQgetvalue(res, i, 0),
			PQgetvalue(res, i, 1));
	}
	fprintf(out, "\n");
	PQclear(res);
}

/*
 * generate a report that corresponds to 'All'
 */
static void
report_all(PGconn *conn, ReportScope *scope, FILE *out)
{
	report_summary(conn, scope, out);
	report_database_statistics(conn, scope, out);
	report_instance_activity(conn, scope, out);
	report_resource_usage(conn, scope, out);
	report_disk_usage(conn, scope, out);
	report_long_transactions(conn, scope, out);
	report_notable_tables(conn, scope, out);
	report_checkpoint_activity(conn, scope, out);
	report_autovacuum_activity(conn, scope, out);
	report_query_activity(conn, scope, out);
	report_setting_parameters(conn, scope, out);
	report_schema_information(conn, scope, out);
	report_profiles(conn, scope, out);
}

/*
 * parse the report ID and determine a report builder
 */
static ReportBuild
parse_reportid(const char *value)
{
	const char *v = value;
	size_t		len;

	/* null input is 'All'. */
	if (v == NULL)
		return (ReportBuild) report_all;

	/* skip blank */
	while (IsSpace(*v)) { v++; }
	len = strlen(v);

	/* Do a prefix match. For example, "su" means 'Summary' */
	if (pg_strncasecmp(REPORTID_SUMMARY, v, len) == 0)
		return (ReportBuild) report_summary;
	else if (pg_strncasecmp(REPORTID_DATABASE_STATISTICS, v, len) == 0)
		return (ReportBuild) report_database_statistics;
	else if (pg_strncasecmp(REPORTID_INSTANCE_ACTIVITY, v, len) == 0)
		return (ReportBuild) report_instance_activity;
	else if (pg_strncasecmp(REPORTID_OS_RESOURCE_USAGE, v, len) == 0)
		return (ReportBuild) report_resource_usage;
	else if (pg_strncasecmp(REPORTID_DISK_USAGE, v, len) == 0)
		return (ReportBuild) report_disk_usage;
	else if (pg_strncasecmp(REPORTID_LONG_TRANSACTIONS, v, len) == 0)
		return (ReportBuild) report_long_transactions;
	else if (pg_strncasecmp(REPORTID_NOTABLE_TABLES, v, len) == 0)
		return (ReportBuild) report_notable_tables;
	else if (pg_strncasecmp(REPORTID_CHECKPOINT_ACTIVITY, v, len) == 0)
		return (ReportBuild) report_checkpoint_activity;
	else if (pg_strncasecmp(REPORTID_AUTOVACUUM_ACTIVITY, v, len) == 0)
		return (ReportBuild) report_autovacuum_activity;
	else if (pg_strncasecmp(REPORTID_QUERY_ACTIVITY, v, len) == 0)
		return (ReportBuild) report_query_activity;
	else if (pg_strncasecmp(REPORTID_SETTING_PARAMETERS, v, len) == 0)
		return (ReportBuild) report_setting_parameters;
	else if (pg_strncasecmp(REPORTID_SCHEMA_INFORMATION, v, len) == 0)
		return (ReportBuild) report_schema_information;
	else if (pg_strncasecmp(REPORTID_PROFILES, v, len) == 0)
		return (ReportBuild) report_profiles;
	else if (pg_strncasecmp(REPORTID_ALL, v, len) == 0)
		return (ReportBuild) report_all;

	ereport(ERROR,
		(errcode(EINVAL),
		 errmsg("invalid report ID: '%s'", value)));
	return NULL;
}

/*
 * examine the report scope for each instance from the range
 * specified by snapshot ID
 */
static List *
select_scope_by_snapid(PGconn *conn, const char *beginid, const char *endid)
{
	List		*scope_list = NIL;
	PGresult	*res;
	char		 b_id[64];
	char		 e_id[64];
	const char	*params[2] = { b_id, e_id };
	int			 i;

	if (beginid)
		strncpy(b_id, beginid, sizeof(b_id));
	else
		/* set the oldest snapshot to begin point */
		strncpy(b_id, SNAPSHOTID_MIN, sizeof(b_id));
	if (endid)
		strncpy(e_id, endid, sizeof(e_id));
	else
		/* set the lastest snapshot to end point */
		strncpy(e_id, SNAPSHOTID_MAX, sizeof(e_id));

	res = pgut_execute(conn, SQL_SELECT_REPORT_SCOPE_BY_SNAPID, lengthof(params), params);
	for (i = 0; i < PQntuples(res); i++)
	{
		ReportScope	*scope;

		scope = pgut_malloc(sizeof(ReportScope));
		scope->instid = pgut_strdup(PQgetvalue(res, i, 0));		/* instance ID */
		scope->host = pgut_strdup(PQgetvalue(res, i, 1));		/* host */
		scope->port = pgut_strdup(PQgetvalue(res, i, 2));		/* port */
		scope->beginid = pgut_strdup(PQgetvalue(res, i, 3));	/* begin point of report */
		scope->endid = pgut_strdup(PQgetvalue(res, i, 4));		/* end point of report */
		scope_list = lappend(scope_list, scope);
	}
	PQclear(res);
	return scope_list;
}

/*
 * examine the report scope for each instance from the range
 * specified by timestamp
 */
static List *
select_scope_by_timestamp(PGconn *conn, time_t begindate, time_t enddate)
{
	List		*scope_list = NIL;
	PGresult	*res;
	char		 b_date[64];
	char		 e_date[64];
	const char	*params[2] = { b_date, e_date };
	int			 i;

	if (begindate != -1)
		strftime(b_date, sizeof(b_date),
			"%Y-%m-%d %H:%M:%S", localtime(&begindate));
	else
		/* set the oldest snapshot to begin point */
		strncpy(b_date, TIMESTAMP_MIN, sizeof(b_date));
	if (enddate != -1)
		strftime(e_date, sizeof(e_date),
			"%Y-%m-%d %H:%M:%S", localtime(&enddate));
	else
		/* set the lastest snapshot to end point */
		strncpy(e_date, TIMESTAMP_MAX, sizeof(e_date));

	res = pgut_execute(conn, SQL_SELECT_REPORT_SCOPE_BY_TIMESTAMP, lengthof(params), params);
	for (i = 0; i < PQntuples(res); i++)
	{
		ReportScope	*scope;

		scope = pgut_malloc(sizeof(ReportScope));
		scope->instid = pgut_strdup(PQgetvalue(res, i, 0));		/* instance ID */
		scope->host = pgut_strdup(PQgetvalue(res, i, 1));		/* host */
		scope->port = pgut_strdup(PQgetvalue(res, i, 2));		/* port */
		scope->beginid = pgut_strdup(PQgetvalue(res, i, 3));	/* begin point of report */
		scope->endid = pgut_strdup(PQgetvalue(res, i, 4));		/* end point of report */
		scope_list = lappend(scope_list, scope);
	}
	PQclear(res);
	return scope_list;
}

static void
destroy_report_scope(ReportScope *scope)
{
	free(scope->instid);
	free(scope->host);
	free(scope->port);
	free(scope->beginid);
	free(scope->endid);
	free(scope);
}
