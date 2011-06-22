/*-------------------------------------------------------------------------
 *
 * pgut-httpd.h
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_HTTPD_H
#define PGUT_HTTPD_H

#include "pgut.h"

typedef struct http_request
{
	char	   *url;
	char	   *params;
} http_request;

typedef const char * (*httpd_handler)(StringInfo out, const http_request *request);

extern void pgut_httpd(int port,
					   const char *listen_addresses,
					   httpd_handler handler);
extern char *pgut_decode(const char *str, int length);
extern const char *mimetype_from_path(const char *path);

#endif   /* PGUT_HTTPD_H */
