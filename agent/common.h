/*-------------------------------------------------------------------------
 *
 * common.h
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PG_STATSINFO_COMMON_H
#define PG_STATSINFO_COMMON_H

#include "catalog/pg_control.h"

/* Error level */
#define ALERT		(PANIC + 1)
#define DISABLE		(PANIC + 2)

/* guc parameter name prefix for the program */
#if PG_VERSION_NUM >= 80400
#define GUC_PREFIX			"pg_statsinfo"
#else
#define GUC_PREFIX			"statsinfo"
#endif

/* log message prefix for the program */
#define LOG_PREFIX			"pg_statsinfo: "

/* manual snapshot log message */
#define LOGMSG_SNAPSHOT		LOG_PREFIX "snapshot requested"
/* manual maintenance log message */
#define LOGMSG_MAINTENANCE	LOG_PREFIX "maintenance requested"
#define LOGMSG_RESTART		LOG_PREFIX "restart requested"

extern bool readControlFile(ControlFileData *ctrl, const char *pgdata);

#endif   /* PG_STATSINFO_COMMON_H */
