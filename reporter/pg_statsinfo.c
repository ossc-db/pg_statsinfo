/*
 * pg_statsinfo.c
 *
 * Copyright (c) 2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfo.h"

const char *PROGRAM_VERSION	= "1.0.0";
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

static PGconn *connect_repository(PGconn *conn_info);

int
main(int argc, char *argv[])
{
	PGconn			*conn_info;
	PGconn			*conn_repo;
	StringInfoData	 buff;
	int				 num_options;

	num_options = pgut_getopt(argc, argv, options);

	/* command-line arguments is not necessary */
	if (num_options != argc)
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("too many argumetns")));

	/* connect to database being monitored */
	initStringInfo(&buff);
	if (dbname && dbname[0])
		appendStringInfo(&buff, "dbname=%s ", dbname);
	if (host && host[0])
		appendStringInfo(&buff, "host=%s ", host);
	if (port && port[0])
		appendStringInfo(&buff, "port=%s ", port);
	if (username && username[0])
		appendStringInfo(&buff, "user=%s ", username);

	conn_info = pgut_connect(buff.data, prompt_password, ERROR);
	termStringInfo(&buff);

	/* can't specified the mode two or more */
	if ((mode_list && (mode_size || mode_report || mode_snapshot || mode_delete)) ||
		(mode_size && (mode_report || mode_snapshot || mode_delete)) ||
		(mode_report && (mode_snapshot || mode_delete)) ||
		(mode_snapshot && mode_delete))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("can't specify two or more mode")));

	/* execute a specified operation */
	if (mode_list)
		do_list(conn_info, instid);
	else if (mode_size)
		do_size(conn_info);
	else if (mode_report)
	{
		/* connect to repository database */
		conn_repo = connect_repository(conn_info);
		do_report(conn_repo, mode_report, instid, beginid, endid, begindate, enddate, output);
		pgut_disconnect(conn_repo);
	}
	else if (mode_snapshot)
		do_snapshot(conn_info, mode_snapshot);
	else if (mode_delete)
	{
		/* connect to repository database */
		conn_repo = connect_repository(conn_info);
		do_delete(conn_repo, mode_delete);
		pgut_disconnect(conn_repo);
	}
	else
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("no operation specified")));

	pgut_disconnect(conn_info);
	return 0;
}

void
pgut_help(bool details)
{
	printf("%s reports a PostgreSQL database.\n\n", PROGRAM_NAME);
	printf("Usage:\n");
	printf("  %s [OPTIONS]\n", PROGRAM_NAME);

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
	printf("                          * SettingParameters\n");
	printf("                          * SchemaInformation\n");
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

/*
 * connect to the repository database
 */
static PGconn *
connect_repository(PGconn *conn_info)
{
	PGconn		*conn_repo;
	PGresult	*res;

	/* obtain connection information */
	res = pgut_execute(conn_info,
		"SELECT setting FROM pg_settings WHERE name = 'pg_statsinfo.repository_server'", 0, NULL);
	if (PQntuples(res) == 0)
		ereport(ERROR,
			(errmsg("pg_statsinfo is not working")));

	/* connect to repository database */
	conn_repo = pgut_connect(PQgetvalue(res, 0, 0), false, ERROR);
	PQclear(res);

	return conn_repo;
}
