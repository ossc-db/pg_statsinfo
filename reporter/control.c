/*
 * control.c
 *
 * Copyright (c) 2009-2013, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfo.h"

/*
 * start pg_statsinfo background process
 */
void
do_start(PGconn *conn)
{
	/* call a function that start pg_statsinfo background process */
	pgut_command(conn, "SELECT statsinfo.start(60)", 0, NULL);
}

/*
 * stop pg_statsinfo background process
 */
void
do_stop(PGconn *conn)
{
	/* call a function that stop pg_statsinfo background process */
	pgut_command(conn, "SELECT statsinfo.stop(60)", 0, NULL);
}
