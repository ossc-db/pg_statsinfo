/*
 * maintenance.c:
 *
 * Copyright (c) 2011-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "pg_statsinfod.h"

#include <sys/types.h>
#include <sys/wait.h>

#define SQL_MAINTENANCE_PARTITION	"SELECT statsrepo.del_snapshot2(CAST($1 AS TIMESTAMPTZ))"
#define SQL_MAINTENANCE				"SELECT statsrepo.del_snapshot(CAST($1 AS TIMESTAMPTZ))"

typedef struct Maintenance
{
	QueueItem	base;

	time_t		repository_keep_period;
} Maintenance;

static bool Maintenance_exec(Maintenance *maintenance, PGconn *conn, const char *instid);
static void Maintenance_free(Maintenance *maintenance);
static pid_t forkexec(const char *command, int *fd_err);

/*
 * maintenance of the snapshot
 */
void
maintenance_snapshot(time_t repository_keep_period)
{
	Maintenance	*maintenance;

	maintenance = pgut_malloc(sizeof(Maintenance));
	maintenance->base.exec = (QueueItemExec) Maintenance_exec;
	maintenance->base.free = (QueueItemFree) Maintenance_free;
	maintenance->repository_keep_period = repository_keep_period;

	writer_send((QueueItem *) maintenance);
}

/*
 * maintenance of the log
 */
pid_t
maintenance_log(const char *command, int *fd_err)
{
	char		 logMaintenanceCmd[MAXPGPATH];
	char		*dp;
	char		*endp;
	const char	*sp;

	/* construct the log maintenance command */
	dp = logMaintenanceCmd;
	endp = logMaintenanceCmd + MAXPGPATH - 1;
	*endp = '\0';

	for (sp = log_maintenance_command; *sp; sp++)
	{
		if (*sp == '%')
		{
			switch (sp[1])
			{
				case 'l':
					/* %l: log directory */
					sp++;
					if (is_absolute_path(log_directory))
						StrNCpy(dp, log_directory, endp - dp);
					else
						join_path_components(dp, data_directory, log_directory);
					dp += strlen(dp);
					break;
				case '%':
					/* convert %% to a single % */
					sp++;
					if (dp < endp)
						*dp++ = *sp;
					break;
				default:
					/* otherwise treat the % as not special */
					if (dp < endp)
						*dp++ = *sp;
					break;
			}
		}
		else
		{
			if (dp < endp)
				*dp++ = *sp;
		}
	}
	*dp = '\0';

	/* run the log maintenance command in background */
	return forkexec(logMaintenanceCmd, fd_err);
}

#define	ERROR_MESSAGE_MAXSIZE	256

/*
 * check the status of log maintenance command running in background
 */
bool
check_maintenance_log(pid_t log_maintenance_pid, int fd_err)
{
	int	status;

	switch (waitpid(log_maintenance_pid, &status, WNOHANG))
	{
		case -1:	/* error */
			elog(ERROR,
				"failed to wait of the log maintenance command: %s", strerror(errno));
			close(fd_err);
			return true;
		case 0:		/* running */
			elog(DEBUG2, "log maintenance command is running");
			return false;
		default:	/* completed */
			if (status != 0)
			{
				/* command exit value is abnormally code */
				ssize_t read_size;
				char    errmsg[ERROR_MESSAGE_MAXSIZE];

				if((read_size = read(fd_err, errmsg, sizeof(errmsg) - 1)) >= 0)
					errmsg[read_size] = '\0';
				else
				{
					elog(ERROR, "read() on self-pipe failed: %s", strerror(errno));
					errmsg[0] = '\0';
				}

				if (WIFEXITED(status))
					elog(ERROR,
						"log maintenance command failed with exit code %d: %s",
						WEXITSTATUS(status), errmsg);
				else if (WIFSIGNALED(status))
					elog(ERROR,
						"log maintenance command was terminated by signal %d: %s",
						WTERMSIG(status), errmsg);
				else
					elog(ERROR,
						"log maintenance command exited with unrecognized status %d: %s",
						status, errmsg);
			}
			close(fd_err);
			return true;
	}
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
		status = pgut_command(conn, SQL_MAINTENANCE_PARTITION, 1, params);
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

#define R	(0)
#define W	(1)

/*
 * execute a shell command asynchronously
 */
static pid_t
forkexec(const char *command, int *fd_err)
{
	pid_t	cpid;
	int		pipe_fd_err[2];

	/* create pipes */
	if (pipe(pipe_fd_err) < 0)
	{
		elog(ERROR, "could not create pipe: %s", strerror(errno));
		return -1;
	}

	/* invoke processs */
	if ((cpid = fork()) < 0)
	{
		close(pipe_fd_err[R]);
		close(pipe_fd_err[W]);
		elog(ERROR, "fork failed: %s", strerror(errno));
		return -1;
	}

	if (cpid == 0)
	{
		/* in child process */
		close(pipe_fd_err[R]);
		dup2(pipe_fd_err[W], STDERR_FILENO);
		close(pipe_fd_err[W]);

		execlp("/bin/sh", "sh", "-c", command, NULL);
		_exit(127);
	}

	close(pipe_fd_err[W]);

	*fd_err = pipe_fd_err[R];
	return cpid;
}
