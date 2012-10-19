/*
 * writer.c:
 *
 * Copyright (c) 2010-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#ifndef WIN32
#include <sys/utsname.h>
#endif
#include <time.h>

#define WRITER_CONN_KEEP_SECS			60	/* secs */

static pthread_mutex_t	writer_queue_lock;
static List			   *writer_queue = NIL;
static time_t			writer_reload_time = 0;

static PGconn		   *writer_conn = NULL;
static time_t			writer_conn_last_used;
static bool				superuser_connect = false;

/*---- GUC variables ----*/
static char	   *my_repository_server = NULL;
/*-----------------------*/


static void reload_params(void);
static int recv_writer_queue(void);
static PGconn *writer_connect(bool superuser);
static char *get_instid(PGconn *conn);
static const char *get_nodename(void);
static void destroy_writer_queue(QueueItem *item);
static void set_connect_privileges(void);


void
writer_init(void)
{
	pthread_mutex_init(&writer_queue_lock, NULL);
	writer_queue = NIL;
	writer_reload_time = 0;	/* any values ok as far as before now */
}

/*
 * writer_main
 */
void *
writer_main(void *arg)
{
	int		items;

	reload_params();
	set_connect_privileges();

	while (shutdown_state < COLLECTOR_SHUTDOWN)
	{
		/* update settings if reloaded */
		if (writer_reload_time < collector_reload_time)
		{
			writer_reload_time = collector_reload_time;
			pthread_mutex_lock(&reload_lock);
			reload_params();
			pthread_mutex_unlock(&reload_lock);
		}

		/* send queued items into the repository server */
		if (recv_writer_queue() == 0)
		{
			/* disconnect if there are no works for a long time */
			if (writer_conn != NULL &&
				writer_conn_last_used + WRITER_CONN_KEEP_SECS < time(NULL))
			{
				pgut_disconnect(writer_conn);
				writer_conn = NULL;
				elog(DEBUG2, "disconnect unused writer connection");
			}
		}

		usleep(200 * 1000);	/* 200ms */
	}

	/* flush remaining items */
	if ((items = recv_writer_queue()) > 0)
		elog(WARNING, "writer discards %d items", items);

	pgut_disconnect(writer_conn);
	writer_conn = NULL;
	shutdown_progress(WRITER_SHUTDOWN);

	return NULL;
}

/*
 * writer_send
 *
 * The argument item should be a malloc'ed object. The ownership will be
 * granted to this module.
 */
void
writer_send(QueueItem *item)
{
	AssertArg(item != NULL);

	item->retry = 0;

	pthread_mutex_lock(&writer_queue_lock);
	writer_queue = lappend(writer_queue, item);
	pthread_mutex_unlock(&writer_queue_lock);
}

/*
 * check whether contains the specified type on the writer queue
 */
bool
writer_has_queue(WriterQueueType type)
{
	ListCell	*cell;

	pthread_mutex_lock(&writer_queue_lock);
	foreach(cell, writer_queue)
	{
		if (((QueueItem *) lfirst(cell))->type == type)
		{
			pthread_mutex_unlock(&writer_queue_lock);
			return true;
		}
	}
	pthread_mutex_unlock(&writer_queue_lock);

	return false;
}



/*
 * load guc variables
 */
static void
reload_params(void)
{
	pgut_disconnect(writer_conn);
	writer_conn = NULL;

	if (my_repository_server != NULL &&
		strcmp(my_repository_server, repository_server) != 0)
	{
		free(my_repository_server);
		my_repository_server = pgut_strdup(repository_server);
		set_connect_privileges();
	}

	free(my_repository_server);
	my_repository_server = pgut_strdup(repository_server);
}

/*
 * recv_writer_queue - return the number of queued items
 */
static int
recv_writer_queue(void)
{
	PGconn	   *conn;
	List	   *queue;
	int			ret;
	char	   *instid = NULL;
	bool		connection_used = false;

	pthread_mutex_lock(&writer_queue_lock);
	queue = writer_queue;
	writer_queue = NIL;
	pthread_mutex_unlock(&writer_queue_lock);

	if (list_length(queue) == 0)
		return 0;

	/* install writer schema */
	if ((conn = writer_connect(superuser_connect)) == NULL)
	{
		elog(ERROR, "could not connect to repository");

		/* discard current queue if can't connect to repository */
		if (list_length(queue) > 0)
		{
			elog(WARNING, "writer discards %d items", list_length(queue));
			list_destroy(queue, destroy_writer_queue);
		}
		return 0;
	}

	/* do the writer queue process */
	if ((instid = get_instid(conn)) != NULL)
	{
		connection_used = true;

		while (list_length(queue) > 0)
		{
			QueueItem  *item = (QueueItem *) linitial(queue);

			if (!item->exec(item, conn, instid))
			{
				if (++item->retry < DB_MAX_RETRY)
					break;	/* retry the item */

				/*
				 * discard if the retry count is exceeded to avoid infinite
				 * loops at one bad item.
				 */
				elog(WARNING, "writer discard an item");
			}

			item->free(item);
			queue = list_delete_first(queue);
		}
	}
	free(instid);

	/* delay on error */
	if (list_length(queue) > 0)
		delay();

	/*
	 * When we have failed items, we concatenate to the head of writer queue
	 * and retry them in the next cycle.
	 */
	pthread_mutex_lock(&writer_queue_lock);
	writer_queue = list_concat(queue, writer_queue);
	ret = list_length(writer_queue);
	pthread_mutex_unlock(&writer_queue_lock);

	/* update last used time of the connection. */
	if (connection_used)
		writer_conn_last_used = time(NULL);

	return ret;
}

/*
 * connect to the repository server.
 */
static PGconn *
writer_connect(bool superuser)
{
	char	info[1024];
	int		retry = 0;

	if (superuser)
#ifdef DEBUG
		snprintf(info, lengthof(info), "%s", my_repository_server);
#else
		snprintf(info, lengthof(info),
			"%s options='-c log_statement=none'", my_repository_server);
#endif
	else
		snprintf(info, lengthof(info), "%s", my_repository_server);

	do
	{
		if (do_connect(&writer_conn, info, "statsrepo"))
			return writer_conn;
		delay();
	} while(shutdown_state < SHUTDOWN_REQUESTED && ++retry < DB_MAX_RETRY);

	return NULL;
}

static char *
get_instid(PGconn *conn)
{
	PGresult	   *res = NULL;
	const char	   *params[4];
	char		   *instid;

	if (pgut_command(conn, "BEGIN TRANSACTION READ WRITE", 0, NULL) != PGRES_COMMAND_OK)
		goto error;

	params[0] = instance_id;
	params[1] = get_nodename();
	params[2] = postmaster_port;

	res = pgut_execute(conn,
			"SELECT instid, pg_version FROM statsrepo.instance"
			" WHERE name = $1 AND hostname = $2 AND port = $3",
			3, params);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		goto error;

	if (PQntuples(res) > 0)
	{
		/* already registered instance */
		bool	modified;

		instid = pgut_strdup(PQgetvalue(res, 0, 0));
		modified = (strcmp(server_version_string, PQgetvalue(res, 0, 1)) != 0);
		PQclear(res);

		if (modified)
		{
			params[0] = server_version_string;
			params[1] = instid;
			pgut_command(conn,
				"UPDATE statsrepo.instance SET pg_version = $1"
				" WHERE instid = $2",
				2, params);
		}
	}
	else
	{
		/* register as a new instance */
		PQclear(res);

		params[3] = server_version_string;
		res = pgut_execute(conn,
			"INSERT INTO statsrepo.instance (name, hostname, port, pg_version)"
			" VALUES ($1, $2, $3, $4) RETURNING instid",
			4, params);
		if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1)
			goto error;

		instid = pgut_strdup(PQgetvalue(res, 0, 0));
		PQclear(res);
		res = NULL;
	}

	if (!pgut_commit(conn))
		goto error;

	return instid;

error:
	PQclear(res);
	pgut_rollback(conn);
	return NULL;
}

static const char *
get_nodename(void)
{
#ifndef WIN32
	static struct utsname	name;

	if (!name.nodename[0])
	{
		if (uname(&name) < 0)
			strlcpy(name.nodename, "unknown", lengthof(name.nodename));
	}

	return name.nodename;
#else
	static char nodename[MAX_PATH];

	if (!nodename[0])
	{
		DWORD bufsize = lengthof(nodename);
		if (!GetComputerNameA(nodename, &bufsize))
			strlcpy(nodename, "unknown", lengthof(nodename));
	}

	return nodename;
#endif
}

static void
destroy_writer_queue(QueueItem *item)
{
	item->free(item);
}

static void
set_connect_privileges(void)
{
	PGresult	*res;

	/* connect to repository */
	if (writer_connect(false) == NULL)
	{
		elog(ERROR, "could not connect to repository");
		return;
	}

	res = pgut_execute(writer_conn,
		"SELECT rolsuper FROM pg_roles WHERE rolname = current_user", 0, NULL);

	if (PQresultStatus(res) == PGRES_TUPLES_OK &&
		PQntuples(res) > 0 &&
		strcmp(PQgetvalue(res, 0, 0), "t") == 0)
		superuser_connect = true;
	else
		superuser_connect = false;

	PQclear(res);
	pgut_disconnect(writer_conn);
	writer_conn = NULL;
}
