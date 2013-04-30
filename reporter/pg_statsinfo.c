/*
 * pg_statsinfo.c
 *
 * Copyright (c) 2011-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfo.h"

const char *PROGRAM_VERSION	= "2.5.0";
const char *PROGRAM_URL		= "http://pgstatsinfo.projects.postgresql.org/";
const char *PROGRAM_EMAIL	= "pgstatsinfo-general@pgfoundry.org";

static bool			 mode_list;
static bool			 mode_size;
static char			*mode_report = NULL;
static char			*mode_snapshot = NULL;
static char			*mode_delete = NULL;
static char			*instid = NULL;
static char			*beginid = NULL;
static char			*endid = NULL;
static time_t		 begindate = (time_t) -1;
static time_t		 enddate = (time_t) -1;
static char			*output;

/* options */
static struct pgut_option options[] =
{
	{ 'b', 'l', "list", &mode_list },
	{ 'b', 's', "size", &mode_size },
	{ 's', 'r', "report", &mode_report },
	{ 's', 'S', "snapshot", &mode_snapshot },
	{ 's', 'D', "delete", &mode_delete },
	{ 's', 'i', "instid", &instid },
	{ 's', 'b', "beginid", &beginid },
	{ 's', 'e', "endid", &endid },
	{ 't', 'B', "begindate", &begindate },
	{ 't', 'E', "enddate", &enddate },
	{ 's', 'o', "output", &output },
	{ 0 }
};

int
main(int argc, char *argv[])
{
	PGconn			*conn;
	StringInfoData	 conn_info;
	int				 num_options;

	num_options = pgut_getopt(argc, argv, options);

	/* command-line arguments is not necessary */
	if (num_options != argc)
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("too many argumetns")));

	/* can't specified the mode two or more */
	if ((mode_list && (mode_size || mode_report || mode_snapshot || mode_delete)) ||
		(mode_size && (mode_report || mode_snapshot || mode_delete)) ||
		(mode_report && (mode_snapshot || mode_delete)) ||
		(mode_snapshot && mode_delete))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("can't specify two or more mode")));

	/* connect to database */
	initStringInfo(&conn_info);
	if (dbname && dbname[0])
		appendStringInfo(&conn_info, "dbname=%s ", dbname);
	if (host && host[0])
		appendStringInfo(&conn_info, "host=%s ", host);
	if (port && port[0])
		appendStringInfo(&conn_info, "port=%s ", port);
	if (username && username[0])
		appendStringInfo(&conn_info, "user=%s ", username);

	conn = pgut_connect(conn_info.data, prompt_password, ERROR);
	termStringInfo(&conn_info);

	/* execute a specified operation */
	if (mode_list)
		do_list(conn, instid);
	else if (mode_size)
		do_size(conn);
	else if (mode_report)
		do_report(conn, mode_report,
			instid, beginid, endid, begindate, enddate, output);
	else if (mode_snapshot)
		do_snapshot(conn, mode_snapshot);
	else if (mode_delete)
		do_delete(conn, mode_delete);
	else
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("please specify operation option (-l, -s, -r, -S, -D)")));

	pgut_disconnect(conn);
	return 0;
}

void
pgut_help(bool details)
{
	printf("pg_statsinfo reports a PostgreSQL database.\n\n");
	printf("Usage:\n");
	printf("  pg_statsinfo -r REPORTID [-i INSTANCEID] [-b SNAPID] [-e SNAPID] [-B DATE] [-E DATE]\n");
	printf("                           [-o FILENAME] [connection-options]\n");
	printf("  pg_statsinfo -l          [-i INSTANCEID] [connection-options]\n");
	printf("  pg_statsinfo -s          [connection-options]\n");
	printf("  pg_statsinfo -S COMMENT  [connection-options]\n");
	printf("  pg_statsinfo -D SNAPID   [connection-options]\n");

	if (!details)
		return;

	printf("\nGeneral options:\n");
	printf("  -l, --list             show the snapshot list\n");
	printf("  -s, --size             show the snapshot size\n");
	printf("  -r, --report=REPORTID  generate a report that specified by REPORTID\n");
	printf("                         ---------------------------\n");
	printf("                          * Summary\n");
	printf("                          * DatabaseStatistics\n");
	printf("                          * InstanceActivity\n");
	printf("                          * OSResourceUsage\n");
	printf("                          * DiskUsage\n");
	printf("                          * LongTransactions\n");
	printf("                          * NotableTables\n");
	printf("                          * CheckpointActivity\n");
	printf("                          * AutovacuumActivity\n");
	printf("                          * QueryActivity\n");
	printf("                          * LockConflicts\n");
	printf("                          * ReplicationActivity\n");
	printf("                          * SettingParameters\n");
	printf("                          * SchemaInformation\n");
	printf("                          * Profiles\n");
	printf("                          * All\n");
	printf("                         ---------------------------\n");
	printf("                         (can prefix match. For example, \"su\" means 'Summary')\n");
	printf("  -S, --snapshot=COMMENT get a snapshot\n");
	printf("  -D, --delete=SNAPID    delete a snapshot\n");
	printf("  -i, --instid           limit to instances of specified instance ID\n");
	printf("  -b, --beginid          begin point of report scope (specify by snapshot ID)\n");
	printf("  -B, --begindate        begin point of report scope (specify by timestamp)\n");
	printf("  -e, --endid            end point of report scope (specify by snapshot ID)\n");
	printf("  -E, --enddate          end point of report scope (specify by timestamp)\n");
	printf("\nOutput options:\n");
	printf("  -o, --output=FILENAME  destination file path for report\n");
}
