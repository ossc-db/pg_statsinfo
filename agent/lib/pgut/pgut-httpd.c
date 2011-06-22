/*-------------------------------------------------------------------------
 *
 * pgut-httpd.c
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "pgut-httpd.h"
#include "getaddrinfo.h"

static int httpd_open(int port, const char *listen_addresses);
static int httpd_accept(int server);
static void shutdown_socket(bool fatal, void *userdata);
static void httpd_send(int sock, StringInfo buf);
static int httpd_recv(int sock, StringInfo buf);
static void httpd_parse(char *message, http_request *request);

#define BUFSIZE				8192
#define MAX_RECV_SIZE		(1024 * 1024)	/* 1MB */
#define RECV_TIMEOUT_SECS	10

static const char *
httpd_read(char *message,
		   httpd_handler handler,
		   StringInfo body)
{
	struct stat		st;
	const char	   *type;
	http_request	request;
	char			path[MAXPGPATH];

	httpd_parse(message, &request);
	if (request.url == NULL || strstr(request.url, ".."))
		return "*400";

	/* TODO: fix memory leak */
	request.url = pgut_decode(request.url, strlen(request.url));

	strlcpy(path, request.url + strspn(request.url, "/"), MAXPGPATH);
	if (!path[0])
		strcpy(path, ".");

retry:
	canonicalize_path(path);
	make_native_path(path);

	if (stat(path, &st) == 0)
	{
		int		fd;

		if (S_ISDIR(st.st_mode))
		{
			strlcat(path, "/index.html", MAXPGPATH);
			goto retry;
		}
		else if (path[0] == '.')
		{
			/* hides dot files */
			return "*403";
		}
		else if ((fd = open(path, O_RDONLY | PG_BINARY, 0)) != -1)
		{
			enlargeStringInfo(body, st.st_size + 1);
			appendStringInfoFd(body, fd);
			close(fd);
			return mimetype_from_path(path);
		}
	}

	if (errno != ENOENT)
		return "*403";

	type = handler(body, &request);
	if (type == NULL)
		return "*404";
	else
		return type;
}

void
pgut_httpd(int port, const char *listen_addresses, httpd_handler handler)
{
	int				server;
	StringInfoData	message;
	StringInfoData	header;
	StringInfoData	body;

	server = httpd_open(port, listen_addresses);
	pgut_atexit_push(shutdown_socket, &server);

	initStringInfo(&message);
	initStringInfo(&header);
	initStringInfo(&body);

	while (!interrupted)
	{
		int			client;

		if ((client = httpd_accept(server)) == 0)
			continue;

		resetStringInfo(&message);
		resetStringInfo(&header);
		resetStringInfo(&body);

		if (httpd_recv(client, &message) == 0)
		{
			const char *type;

			type = httpd_read(message.data, handler, &body);

			if (type[0] != '*')
			{
				appendStringInfo(&header,
					"HTTP/1.0 200 OK\r\n"
				/*	"Date: Fri, 19 Jun 1998 20:38:48 GMT\r\n"*/
					"Server: %s\r\n"
				/*	"Last-modified: Wed, 04 Mar 1998 06:40:21 GMT\r\n"*/
					"Content-Length: %lu\r\n"
					"Connection: close\r\n"
					"Content-Type: %s\r\n"
					"\r\n", PROGRAM_NAME, (unsigned long) body.len, type);

				httpd_send(client, &header);
				httpd_send(client, &body);
			}
			else
			{
				appendStringInfo(&body,
					"<html><body>ERROR: %s</body></html>",
					type + 1);
				appendStringInfo(&header,
					"HTTP/1.0 %s NG\r\n"
					"Server: %s\r\n"
					"Content-Length: %lu\r\n"
					"Connection: close\r\n"
					"\r\n", type + 1, PROGRAM_NAME, (unsigned long) body.len);
				httpd_send(client, &header);
				httpd_send(client, &body);
			}
		}

		shutdown(client, 2);
	}

	shutdown_socket(false, &server);
	pgut_atexit_pop(shutdown_socket, &server);

	termStringInfo(&message);
	termStringInfo(&header);
	termStringInfo(&body);
}

static int
httpd_open(int port, const char *listen_addresses)
{
	int					sock;

	if (port <= 0 || 65535 < port)
		elog(ERROR, "invalid port number: %d", port);

#ifdef WIN32
	{
		WSADATA		wsaData;
		int			err;

		/* Prepare Winsock */
		err = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (err != 0)
			elog(ERROR, "WSAStartup failed: %d", err);
	}
#endif   /* WIN32 */

	if (listen_addresses && strcmp(listen_addresses, "*") == 0)
	{
		struct sockaddr_in	addr;

		MemSet(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		sock = socket(PF_INET, SOCK_STREAM, 0);
		if (sock == -1)
			elog(ERROR, "socket: %s", strerror(errno));
		if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
			elog(ERROR, "bind(*): %s", strerror(errno));
	}
	else
	{
		char				servname[32];
		struct addrinfo    *addrs, *addr;
		struct addrinfo		hint;

		snprintf(servname, lengthof(servname), "%d", port);
		MemSet(&hint, 0, sizeof(hint));
		hint.ai_family = AF_UNSPEC;
		hint.ai_flags = AI_PASSIVE;
		hint.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(listen_addresses, servname, &hint, &addrs))
			elog(ERROR, "getaddrinfo: %s", strerror(errno));

		sock = -1;
		for (addr = addrs; addr != NULL; addr = addr->ai_next)
		{
			sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			if (sock == -1)
				continue;

			if (bind(sock, addr->ai_addr, addr->ai_addrlen) == 0)
				break;

			close(sock);
		}

		if (addr == NULL)
			elog(ERROR, "socket or bind: %s", strerror(errno));

		freeaddrinfo(addrs);
	}

	if (listen(sock, SOMAXCONN) == -1)
		elog(ERROR, "listen: %s", strerror(errno));

	return sock;
}

static int
httpd_accept(int server)
{
	int					client;
	socklen_t			len;
	union
	{
		struct sockaddr			addr;
		struct sockaddr_in		addr_in;
		struct sockaddr_storage	addr_storage;
	} addr;

	wait_for_socket(server, NULL);

	len = sizeof(addr);
	client = accept(server, &addr.addr, &len);
	if (client == -1)
	{
		elog(WARNING, "accept: %s", strerror(errno));
		return 0;
	}

	return client;
}

static void
shutdown_socket(bool fatal, void *userdata)
{
	int *sock = (int *) userdata;
	if (sock && *sock != -1)
	{
		shutdown(*sock, 2);
		*sock = -1;
	}
}

static void
httpd_send(int sock, StringInfo buf)
{
	size_t	done = 0;

	while (done < buf->len)
	{
		int	rc = send(sock, buf->data + done, buf->len - done, 0);
		if (rc > 0)
			done += rc;
		else if (errno != EINTR)
			elog(WARNING, "send: %s", strerror(errno));
	}
}

static int
httpd_recv(int sock, StringInfo buf)
{
	const char *content;
	size_t		start = buf->len;
	size_t		length;
	size_t		offset;

	enlargeStringInfo(buf, BUFSIZE);
	for (;;)
	{
		int		rc;

		if (buf->len - start > MAX_RECV_SIZE)
			return errno = E2BIG;

		rc = recv(sock, buf->data + buf->len, buf->maxlen - buf->len - 1, 0);
		if (rc > 0)
		{
			buf->len += rc;
			buf->data[buf->len] = '\0';
			break;
		}
		else if (errno != EINTR)
			return errno;
	}

	/* seek to Content-Length */
	if ((content = strstr(buf->data, "Content-Length:")) == NULL)
		return 0;	/* no content */

	length = atoi(content + strlen("Content-Length:"));
	if (length < 0 || MAX_RECV_SIZE < length)
		return errno = E2BIG;	/* too large content length */

	/* seek to content */
	if ((content = strstr(content, "\r\n\r\n")) == NULL)
		return errno = E2BIG;	/* too large header */

	offset = content - buf->data + 4;
	enlargeStringInfo(buf, offset + length - buf->len + 1);
	while (buf->len < offset + length)
	{
		int				rc;
		struct timeval	timeout;

		timeout.tv_sec = RECV_TIMEOUT_SECS;
		timeout.tv_usec = 0;

		if (wait_for_socket(sock, &timeout) <= 0)
			return EAGAIN;	/* timeout */

		rc = recv(sock, buf->data + buf->len, buf->maxlen - buf->len, 0);
		if (rc > 0)
		{
			buf->len += rc;
			buf->data[buf->len] = '\0';
		}
		else if (errno != EINTR)
			return errno;
	}

	return 0;
}

#define IsURL(c)	(isprint((unsigned char)(c)) && !IsSpace(c))

static void
httpd_parse(char *message, http_request *request)
{
	while (IsSpace(*message)) { message++; }
	if (strncmp(message, "GET", 3) == 0 && !IsAlpha(message[3]))
	{
		message += 3;
		while (IsSpace(*message)) { message++; }
		request->url = message;
		while (IsURL(*message)) { message++; }
		*message = '\0';
	}
	else if (strncmp(message, "POST", 4) == 0 && !IsAlpha(message[4]))
	{
		char   *contents;

		message += 4;
		while (IsSpace(*message)) { message++; }
		request->url = message;
		while (IsURL(*message)) { message++; }

		if ((contents = strstr(message, "\r\n\r\n")) != NULL)
		{
			size_t	len;

			*message = '\0';
			*message = (strchr(request->url, '?') ? '&' : '?');
			contents += 4;
			len = strlen(contents);
			memmove(message + 1, contents, len);
			message[len + 1] = '\0';
		}
	}
	else
	{
		elog(WARNING, "unknown request: %s", message);
		memset(&request, 0, sizeof(request));
		return;
	}

	if ((request->params = strchr(request->url, '?')) != NULL)
	{
		*request->params = '\0';
		request->params++;
	}
}

static const char *MIMETYPES[][2] =
{
	{ "htm"		, "text/html" },
	{ "html"	, "text/html" },
	{ "xhtml"	, "application/xhtml+xml" },
	{ "css"		, "text/css" },
	{ "txt"		, "text/plain" },
	{ "js"		, "application/x-javascript" },
	{ "xml"		, "application/xml" },
	{ "xsl"		, "text/xsl" },
	{ "png"		, "image/png" },
	{ "jpeg"	, "image/jpeg" },
	{ "jpg"		, "image/jpeg" },
	{ "gif"		, "image/gif" },
	{ NULL }
};

const char *
mimetype_from_path(const char *path)
{
	const char *ext;
	int			i;

	if (path == NULL)
		return "application/octet-stream";

	ext = strrchr(path, '.');

	if (ext == NULL)
		return "application/octet-stream";

	for (i = 0; i < lengthof(MIMETYPES); i++)
		if (pg_strcasecmp(ext + 1, MIMETYPES[i][0]) == 0)
			return MIMETYPES[i][1];

	return "application/octet-stream";
}

/* decode URL encoded string */
char *
pgut_decode(const char *str, int length)
{
	const char	   *s;
	char		   *d;
	char		   *ret;

	ret = pgut_malloc(length + 1);
	for (s = str, d = ret; s < str + length;)
	{
		if (*s == '%' && s[1] && s[2])
		{
			char	wk[3];
			wk[0] = s[1];
			wk[1] = s[2];
			wk[2] = '\0';
			*d++ = (char) strtol(wk, NULL, 16);;
			s += 3;
		}
		else
			*d++ = *s++;
	}
	*d = '\0';

	return ret;
}
