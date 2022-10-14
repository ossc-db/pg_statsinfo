/*
 * autovacuum.c : parse auto-vacuum and auto-analyze messages
 *
 * Copyright (c) 2009-2022, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#define SQL_INSERT_AUTOVACUUM "\
INSERT INTO statsrepo.autovacuum VALUES \
($1, $2::timestamptz - interval '1sec' * $21, \
 $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, \
 $19, $20, $21, $22, $23, $24, $25, $26, $27, $28, $29, $30, $31, $32)" 

#define SQL_INSERT_AUTOANALYZE "\
INSERT INTO statsrepo.autoanalyze VALUES \
($1, $2::timestamptz - interval '1sec' * $11, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13)" 

#define SQL_INSERT_AUTOVACUUM_CANCEL "\
INSERT INTO statsrepo.autovacuum_cancel VALUES \
($1, $2, $3, $4, $5, $6)"

#define SQL_INSERT_AUTOANALYZE_CANCEL "\
INSERT INTO statsrepo.autoanalyze_cancel VALUES \
($1, $2, $3, $4, $5, $6)"

/* pg_rusage (rusage is not localized) */
#define MSG_RUSAGE \
	"CPU: user: %f s, system: %f s, elapsed: %f s"

/* index scan (index scan is not localized) */
#define MSG_INDEX_SCAN \
	"index scan %s: %u pages from table (%.2f%% of total) %s %lld dead item %s"

#define MSG_INDEX_SCAN_PTN_1 "not needed"
#define MSG_INDEX_SCAN_PTN_2 "needed"
#define MSG_INDEX_SCAN_PTN_3 "bypassed"
#define MSG_INDEX_SCAN_PTN_4 "bypassed by failsafe"

/* indexes (indexes is not localized) */
#define MSG_INDEXES \
	"index \"%s\": pages: %u in total, %u newly deleted, %u currently deleted, %u reusable"

/* I/O Timngs (I/O Timngs is not localized) */
#define MSG_IO_TIMING \
	"I/O timings: read: %.3f ms, write: %.3f ms"

/* autovacuum cancel request */
#define MSG_AUTOVACUUM_CANCEL_REQUEST \
	"sending cancel to blocking autovacuum PID %s"

/* autovacuum cancel */
#define MSG_AUTOVACUUM_CANCEL \
	"canceling autovacuum task"

#define NUM_AUTOVACUUM				25
#define IDX_AUTOVACUUM_RUSAGE		24
#define IDX_AUTOVACUUM_OPTIONAL		13
#define NUM_AUTOANALYZE				12
#define IDX_AUTOANALYZE_RUSAGE		11
#define IDX_AUTOANALYZE_OPTIONAL	3

#define NUM_INDEXES					5
#define NUM_INDEX_SCAN				6
#define NUM_IO_TIMING				2
#define NUM_RUSAGE					3
#define NUM_AUTOVACUUM_CANCEL		5

#define AUTOVACUUM_CANCEL_LIFETIME	300	/* sec */

/* autovacuum log data */
typedef struct AutovacuumLog
{
	QueueItem	base;

	char	finish[LOGTIME_LEN];
	List   *params;
} AutovacuumLog;

/* autovacuum cancel request */
typedef struct AVCancelRequest
{
	time_t	 time;		/* localtime that detected the cancel request */
	char	*w_pid;		/* autovacuum worker PID */
	char	*query;		/* query that caused the cancel */
} AVCancelRequest;

static void Autovacuum_free(AutovacuumLog *av);
static bool Autovacuum_exec(AutovacuumLog *av, PGconn *conn, const char *instid);
static bool Autoanalyze_exec(AutovacuumLog *av, PGconn *conn, const char *instid);
static bool AutovacuumCancel_exec(AutovacuumLog *av, PGconn *conn, const char *instid);
static void refresh_avc_request(void);
static AVCancelRequest *get_avc_request(const char *w_pid);
static void put_avc_request(AVCancelRequest *new_entry);
static void remove_avc_request(AVCancelRequest *entry);

static List		*avc_request = NIL;

/*
 * is_autovacuum
 */
bool
is_autovacuum(const char *message)
{
	/* autovacuum log */
	if (match(message, msg_autovacuum))
		return true;

	/* autoanalyze log */
	if (match(message, msg_autoanalyze))
		return true;

	return false;
}

/*
 * parse_autovacuum
 */
bool
parse_autovacuum(const char *message, const char *timestamp)
{
	AutovacuumLog  *av;
	List		   *params;
	List		   *usage;
	List		   *index_scan = NIL;
	List		   *indexes    = NIL;
	List		   *io_timing  = NIL;
	const char	   *str_usage;
	QueueItemExec	exec;
	int				idx_rusage = 0;

	if ((params = capture(message, msg_autovacuum, NUM_AUTOVACUUM)) != NIL)
	{
		char *str_optional;
		char *index_names;
		char *index_pages_total;
		char *index_pages_new_del;
		char *index_pgaes_current_del;
		char *index_pages_reusable;
		bool bfirst_index = true;

		exec = (QueueItemExec) Autovacuum_exec;
		idx_rusage = IDX_AUTOVACUUM_RUSAGE;

		str_optional = (char*)list_nth( params, IDX_AUTOVACUUM_OPTIONAL );
		if ( strlen(str_optional) == 0 )
		{
			/* Empty list of number of elements NUM_INDEX_SCAN. */
			for( int i=0; i<NUM_INDEX_SCAN; i++)
				index_scan = lappend( index_scan, NULL );

			/* Empty Index information. */
			index_names             = pgut_malloc( 3 );
			index_pages_total       = pgut_malloc( 3 );
			index_pages_new_del     = pgut_malloc( 3 );
			index_pgaes_current_del = pgut_malloc( 3 );
			index_pages_reusable    = pgut_malloc( 3 );
			strcpy(index_names,            "{}");
			strcpy(index_pages_total,      "{}");
			strcpy(index_pages_new_del,    "{}");
			strcpy(index_pgaes_current_del,"{}");
			strcpy(index_pages_reusable,   "{}");

			/* Empty I/O timing Information. */
			for( int i=0; i<NUM_IO_TIMING; i++)
				io_timing = lappend( io_timing, NULL );
		}
		else
		{
			/* Buffer size for temporarily saving the index information list */
			long len_index_buf = strlen(str_optional);

			char *tok = strtok(str_optional, "\n");
			char *str_index_scan_ptn;
			
			/* Re-parse index scan output separatedly. */
			if ((index_scan = capture( tok, MSG_INDEX_SCAN, NUM_INDEX_SCAN)) != NIL){
				str_index_scan_ptn = (char*)list_nth( index_scan, 0 );
				if ( strcmp( str_index_scan_ptn, MSG_INDEX_SCAN_PTN_1) == 0 ){
					strcpy( str_index_scan_ptn, "1" );
				}
				else if ( strcmp( str_index_scan_ptn, MSG_INDEX_SCAN_PTN_2) == 0 ){
					strcpy( str_index_scan_ptn, "2" );
				}
				else if ( strcmp( str_index_scan_ptn, MSG_INDEX_SCAN_PTN_3) == 0 ){
					strcpy( str_index_scan_ptn, "3" );
				}
				else if ( strcmp( str_index_scan_ptn, MSG_INDEX_SCAN_PTN_4) == 0 ){
					strcpy( str_index_scan_ptn, "4" );
				}
				else {
					list_free_deep(params);
					list_free_deep(index_scan);
					elog(WARNING, "cannot parse index ptn of autovacuum: %s", str_index_scan_ptn);
					return false;	/* should not happen */
				}
				/* get next token. */
				tok = strtok(NULL, "\n");
			} else {
				/* Empty list of number of elements NUM_INDEX_SCAN. */
				for( int i=0; i<NUM_INDEX_SCAN; i++)
					index_scan = lappend( index_scan, NULL );
			}

			/* Re-parse indexes output separatedly. */
			index_names             = pgut_malloc( len_index_buf );
			index_pages_total       = pgut_malloc( len_index_buf );
			index_pages_new_del     = pgut_malloc( len_index_buf );
			index_pgaes_current_del = pgut_malloc( len_index_buf );
			index_pages_reusable    = pgut_malloc( len_index_buf );
			strcpy(index_names,            "{");
			strcpy(index_pages_total,      "{");
			strcpy(index_pages_new_del,    "{");
			strcpy(index_pgaes_current_del,"{");
			strcpy(index_pages_reusable,   "{");

			while( tok )
			{
				List	*Indexes_params;
				if (strlen(tok)>0){
					if( (Indexes_params = capture( tok, MSG_INDEXES, NUM_INDEXES )) != NIL ){
						if( bfirst_index ){
							bfirst_index = false;
						} else {
							strcat( index_names,             "," );
							strcat( index_pages_total,       "," );
							strcat( index_pages_new_del,     "," );
							strcat( index_pgaes_current_del, "," );
							strcat( index_pages_reusable,    "," );
						}
						strcat( index_names,             (char*)list_nth(Indexes_params,0) );
						strcat( index_pages_total,       (char*)list_nth(Indexes_params,1) );
						strcat( index_pages_new_del,     (char*)list_nth(Indexes_params,2) );
						strcat( index_pgaes_current_del, (char*)list_nth(Indexes_params,3) );
						strcat( index_pages_reusable,    (char*)list_nth(Indexes_params,4) );
					} else {
						break;
					}
				}
				/* get next token. */
				tok = strtok(NULL, "\n");
			}
			strcat( index_names,             "}" );
			strcat( index_pages_total,       "}" );
			strcat( index_pages_new_del,     "}" );
			strcat( index_pgaes_current_del, "}" );
			strcat( index_pages_reusable,    "}" );
		
			/* Re-parse I/O timings output separatedly. */
			if ( tok && (strlen(tok) > 0) )
			{
				if ( (io_timing = capture( tok, MSG_IO_TIMING, NUM_IO_TIMING )) != NIL )
					tok = strtok(NULL, "\n");
				else
				{
					elog(WARNING, "The optional string in the autovacuum log cannot be parsed.: %s", tok);
					/* Empty I/O timing Information. */
					for( int i=0; i<NUM_IO_TIMING; i++)
						io_timing = lappend( io_timing, NULL );
				}
			}
			else
			{
				/* Empty I/O timing Information. */
				for( int i=0; i<NUM_IO_TIMING; i++)
					io_timing = lappend( io_timing, NULL );
			}

			if ( tok && (strlen(tok) > 0) )
				elog(WARNING, "Unexpected optional string in autovacuum log.: %s", tok);
		}

		/* indexes list. */
		indexes = lappend( indexes, strdup(index_names) );
		indexes = lappend( indexes, strdup(index_pages_total) );
		indexes = lappend( indexes, strdup(index_pages_new_del) );
		indexes = lappend( indexes, strdup(index_pgaes_current_del) );
		indexes = lappend( indexes, strdup(index_pages_reusable) );

		free(index_names);
		free(index_pages_total);
		free(index_pages_new_del);
		free(index_pgaes_current_del);
		free(index_pages_reusable);
	}
	else if ((params = capture(message, msg_autoanalyze, NUM_AUTOANALYZE)) != NIL)
	{
		char *str_optional;

		exec = (QueueItemExec) Autoanalyze_exec;
		idx_rusage = IDX_AUTOANALYZE_RUSAGE;

		/* Re-parse I/O timings. */
		str_optional = (char*)list_nth( params, IDX_AUTOANALYZE_OPTIONAL );
		if( strlen(str_optional) == 0)
		{
			/* Empty I/O timing Information. */
			for( int i=0; i<NUM_IO_TIMING; i++)
				io_timing = lappend( io_timing, NULL );
		}
		else
		{
			if (str_optional[strlen(str_optional) - 1] == '\n')
				str_optional[strlen(str_optional) - 1] = '\0';
			
			if ((io_timing = capture( str_optional, MSG_IO_TIMING, NUM_IO_TIMING )) == NIL )
			{
				elog(WARNING, "The optional string in the autoanalyze log cannot be parsed.: %s", str_optional);
				/* Empty I/O timing Information. */
				for( int i=0; i<NUM_IO_TIMING; i++)
					io_timing = lappend( io_timing, NULL );
			}
		}
	}
	else
		return false;

	/*
	 * Re-parse rusage output separatedly. Note that MSG_RUSAGE won't be
	 * localized with any lc_messages.
	 */
	str_usage = (char*)list_nth( params, idx_rusage);
	if ((usage = capture(str_usage, MSG_RUSAGE, NUM_RUSAGE)) == NIL)
	{
		elog(WARNING, "cannot parse rusage: %s", str_usage);
		list_free_deep(params);
		list_free_deep(index_scan);
		list_free_deep(indexes);
		return false;	/* should not happen */
	}


	av = pgut_new(AutovacuumLog);
	av->base.type = QUEUE_AUTOVACUUM;
	av->base.free = (QueueItemFree) Autovacuum_free;
	av->base.exec = exec;
	strlcpy(av->finish, timestamp, lengthof(av->finish));
	av->params = list_concat(params, usage);
	av->params = list_concat(params, index_scan);
	av->params = list_concat(params, indexes);
	av->params = list_concat(params, io_timing);

	writer_send((QueueItem *) av);
	return true;
}

/*
 * is_autovacuum_cancel
 */
bool
is_autovacuum_cancel(int elevel, const char *message)
{
	/* autovacuum cancel log */
	if (elevel == ERROR &&
		match(message, MSG_AUTOVACUUM_CANCEL))
		return true;

	return false;
}

/*
 * is_autovacuum_cancel_request
 */
bool
is_autovacuum_cancel_request(int elevel, const char *message)
{
	/* autovacuum cancel request log */
	if ((elevel == LOG || elevel == DEBUG) &&
		match(message, MSG_AUTOVACUUM_CANCEL_REQUEST))
		return true;

	return false;
}

/*
 * parse_autovacuum_cancel
 */
bool
parse_autovacuum_cancel(const Log *log)
{
	AutovacuumLog	*av;
	AVCancelRequest	*entry;
	List			*params;

	/* parse context string */
	char	*ctx;
	char	*tok;
	params = NIL;
	ctx = pgut_strdup(log->context);
	tok = strtok(ctx, "\n");
	while( tok )
	{
		if ((params = capture( tok, 
			"automatic %s of table \"%s.%s.%s\"", 4)) != NIL)
			break;
		tok = strtok(NULL, "\n");
	}
	free( ctx );
	if (params == NIL)
		return false;	/* should not happen */

	/* get the query that caused cancel */
	entry = get_avc_request(log->pid);

	av = pgut_new(AutovacuumLog);
	av->base.type = QUEUE_AUTOVACUUM;
	av->base.free = (QueueItemFree) Autovacuum_free;
	av->base.exec = (QueueItemExec) AutovacuumCancel_exec;
	strlcpy(av->finish, log->timestamp, lengthof(av->finish));
	av->params = params;
	if (entry)
	{
		av->params = lappend(av->params, pgut_strdup(entry->query));
		remove_avc_request(entry);
	}
	else
		av->params = lappend(av->params, NULL);

	writer_send((QueueItem *) av);

	return true;
}

/*
 * parse_autovacuum_cancel_request
 */
bool
parse_autovacuum_cancel_request(const Log *log)
{
	AVCancelRequest	*new_entry;
	List			*params;

	/* remove old entries */
	refresh_avc_request();

	/* add a new entry of the cancel request */
	if ((params = capture(log->hint,
		MSG_AUTOVACUUM_CANCEL_REQUEST, 1)) == NIL)
		return false;	/* should not happen */

	new_entry = pgut_malloc(sizeof(AVCancelRequest));
	new_entry->time = time(NULL);
	new_entry->w_pid = pgut_strdup((char *) list_nth(params, 0));
	new_entry->query = pgut_strdup(log->user_query);
	list_free_deep(params);

	put_avc_request(new_entry);

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
	const char	   *params[32];

	elog(DEBUG2, "write (autovacuum)");
	Assert(list_length(av->params) == NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + NUM_INDEXES + NUM_IO_TIMING);

	memset(params, 0, sizeof(params));

	/*
	 * Note: In PostgreSQL14, the output order of the automatic vacuum log
	 * has changed and it no longer matches the column order of the
	 * repository, so the values of the second argument of list_nth()
	 * are not in order.
	 */
	params[0] = instid;
	params[1] = av->finish;					/* finish */
	params[2] = list_nth(av->params, 1);	/* database */
	params[3] = list_nth(av->params, 2);	/* schema */
	params[4] = list_nth(av->params, 3);	/* table */
	params[5] = list_nth(av->params, 4);	/* index_scans */
	params[6] = list_nth(av->params, 5);	/* page_removed */
	params[7] = list_nth(av->params, 6);	/* page_remain */
	params[8] = list_nth(av->params, 8);	/* frozen_skipped_pages */
	params[9] = list_nth(av->params, 9);	/* tup_removed */
	params[10] = list_nth(av->params, 10);	/* tup_remain */
	params[11] = list_nth(av->params, 11);	/* tup_dead */
	params[12] = list_nth(av->params, 18);	/* page_hit */
	params[13] = list_nth(av->params, 19);	/* page_miss */
	params[14] = list_nth(av->params, 20);	/* page_dirty */
	params[15] = list_nth(av->params, 14);	/* read_rate */
	params[16] = list_nth(av->params, 16);	/* write_rate */
	params[17] = list_nth(av->params, 21);	/* wal_records */  
	params[18] = list_nth(av->params, 22);	/* wal_page_images */ 
	params[19] = list_nth(av->params, 23);	/* wal_bytes */
	params[20] = list_nth(av->params, NUM_AUTOVACUUM + 2);	/* duration */
	params[21] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + 0);	/* index_scan_ptn */
	params[22] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + 1);	/* tbl_scan_pages */
	params[23] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + 2);	/* tbl_scan_pages_ratio */
	params[24] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + 4);	/* dead_lp */
	params[25] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + 0);	/* index_names */
	params[26] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + 1);	/* index_pages_total */
	params[27] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + 2);	/* index_pages_new_del */
	params[28] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + 3);	/* index_pgaes_current_del */
	params[29] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + 4);	/* index_pages_reusable */
	params[30] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + NUM_INDEXES + 0);	/* io_timings_read */
	params[31] = list_nth(av->params, NUM_AUTOVACUUM + NUM_RUSAGE + NUM_INDEX_SCAN + NUM_INDEXES + 1);	/* io_timings_write */

	return pgut_command(conn, SQL_INSERT_AUTOVACUUM,
						lengthof(params), params) == PGRES_COMMAND_OK;
}

static bool
Autoanalyze_exec(AutovacuumLog *av, PGconn *conn, const char *instid)
{
	const char	   *params[13];

	elog(DEBUG2, "write (autoanalyze)");
	Assert(list_length(av->params) == NUM_AUTOANALYZE + NUM_RUSAGE + NUM_IO_TIMING);

	params[0] = instid;
	params[1] = av->finish;					/* finish */
	params[2] = list_nth(av->params, 0);	/* database */
	params[3] = list_nth(av->params, 1);	/* schema */
	params[4] = list_nth(av->params, 2);	/* table */
	params[5] = list_nth(av->params, 8);	/* page_hit */
	params[6] = list_nth(av->params, 9);	/* page_miss */ 
	params[7] = list_nth(av->params, 10);	/* page_dirty */
	params[8] = list_nth(av->params, 4);	/* read_rate */
	params[9] = list_nth(av->params, 6);	/* write_rate */
	params[10] = list_nth(av->params, NUM_AUTOANALYZE + 2);	/* duration */
	params[11] = list_nth(av->params, NUM_AUTOANALYZE + NUM_RUSAGE + 0);	/* io_timings_read */
	params[12] = list_nth(av->params, NUM_AUTOANALYZE + NUM_RUSAGE + 1);	/* io_timings_write */
	
	return pgut_command(conn, SQL_INSERT_AUTOANALYZE,
						lengthof(params), params) == PGRES_COMMAND_OK;
}

static bool
AutovacuumCancel_exec(AutovacuumLog *av, PGconn *conn, const char *instid)
{
	const char	*params[6];
	const char	*query;

	elog(DEBUG2, "write (autovacuum cancel)");
	Assert(list_length(av->params) == NUM_AUTOVACUUM_CANCEL);

	params[0] = instid;
	params[1] = av->finish;					/* finish */
	params[2] = list_nth(av->params, 1);	/* database */
	params[3] = list_nth(av->params, 2);	/* schema */
	params[4] = list_nth(av->params, 3);	/* table */
	params[5] = list_nth(av->params, 4);	/* query */

	if (strcmp(list_nth(av->params, 0), "vacuum") == 0)
		query = SQL_INSERT_AUTOVACUUM_CANCEL;
	else
		query = SQL_INSERT_AUTOANALYZE_CANCEL;

	return pgut_command(conn, query,
						lengthof(params), params) == PGRES_COMMAND_OK;
}

static void
refresh_avc_request(void)
{
	ListCell	*cell;
	time_t		 now = time(NULL);

	foreach (cell, avc_request)
	{
		AVCancelRequest	*entry = lfirst(cell);

		if ((now - entry->time) > AUTOVACUUM_CANCEL_LIFETIME)
			remove_avc_request(entry);
	}
}

static AVCancelRequest *
get_avc_request(const char *w_pid)
{
	ListCell	*cell;

	foreach (cell, avc_request)
	{
		AVCancelRequest	*entry = lfirst(cell);

		if (strcmp(entry->w_pid, w_pid) == 0)
			return entry;
	}

	return NULL;	/* not found */
}

static void
put_avc_request(AVCancelRequest *new_entry)
{
	AVCancelRequest	*old_entry;

	/* remove old entry that has same autovacuum worker PID */
	if ((old_entry = get_avc_request(new_entry->w_pid)))
		remove_avc_request(old_entry);

	avc_request = lappend(avc_request, new_entry);
}

static void
remove_avc_request(AVCancelRequest *entry)
{
	if (!entry)
		return;

	free(entry->w_pid);
	free(entry->query);
	free(entry);
	avc_request = list_delete_ptr(avc_request, entry);
}
