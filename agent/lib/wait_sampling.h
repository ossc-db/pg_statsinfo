/*
 * lib/wait_sampling.h
 *
 * Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

/*      
* For hash table dealloc factors. (same as pg_stat_statements)
* TODO: should share for wait-sampling-hash-table
*/    
#define STATSINFO_USAGE_INCREASE (1.0)
#define STATSINFO_USAGE_DECREASE_FACTOR	(0.99)
#define STATSINFO_USAGE_DEALLOC_PERCENT	5
#define STATSINFO_USAGE_INIT (1.0)

/* Location of permanent stats file (valid when database is shut down) */
#define STATSINFO_WS_DUMP_FILE  PGSTAT_STAT_PERMANENT_DIRECTORY "/pg_statsinfo_ws.stat"

/* Magic number identifying the stats file format */
static const uint32 STATSINFO_WS_FILE_HEADER = 0x20210930;

typedef struct
{
	Oid				userid;			/* user OID */
	Oid				dbid;			/* database OID */
	uint64			queryid;		/* query identifier */
	BackendType		backend_type;		/* Type of backends */
	uint32			wait_event_info;	/* Wait sampling information */
} wait_samplingHashKey;

/* wait sampling counters. */
typedef struct wait_samplingCounters
{
	double			  usage;			/* usage factor */
	uint64			  count;			/* number of samples */
} wait_samplingCounters;

/* wait sampling entry per database (same as pg_stat_statements) */
typedef struct wait_samplingEntry
{
	wait_samplingHashKey		key;				/* hash key of entry - MUST BE FIRST */
	wait_samplingCounters	counters;			/* statistics for this event */
	slock_t			mutex;				/* protects the counters only */
} wait_samplingEntry;

typedef struct
{
	Oid				userid;			/* user OID */
	Oid				dbid;			/* database OID */
	uint64			queryid;		/* query identifier */
} wait_samplingSubHashKey;

typedef struct wait_samplingSubEntry
{
	wait_samplingSubHashKey		key;			/* hash key of entry - MUST BE FIRST */
	double				usage;			/* usage factor */
} wait_samplingSubEntry;

/*
 * Global statistics for sample_wait_sampling
 */
typedef struct wait_samplingGlobalStats
{
	int64		dealloc;		/* # of times entries were deallocated */
	TimestampTz stats_reset;	/* timestamp with all stats reset */
} wait_samplingGlobalStats;

typedef struct wait_samplingSharedState
{
	LWLock	   *lock;			/* protects hashtable search/modification */
	slock_t		mutex;			/* protects following fields only: */
	wait_samplingGlobalStats stats;		/* global statistics for wait_sampling */
} wait_samplingSharedState;
