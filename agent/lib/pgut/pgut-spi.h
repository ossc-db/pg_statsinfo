/*-------------------------------------------------------------------------
 *
 * pgut-spi.h
 *
 * Copyright (c) 2009-2015, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_SPI_H
#define PGUT_SPI_H

#include "executor/spi.h"

/*
 * Set the format style used by gcc to check printf type functions. We really
 * want the "gnu_printf" style set, which includes what glibc uses, such
 * as %m for error strings and %lld for 64 bit long longs. But not all gcc
 * compilers are known to support it, so we just use "printf" which all
 * gcc versions alive are known to support, except on Windows where
 * using "gnu_printf" style makes a dramatic difference. Maybe someday
 * we'll have a configure test for this, if we ever discover use of more
 * variants to be necessary.
 */
#ifdef WIN32
#define PG_PRINTF_ATTRIBUTE gnu_printf
#else
#define PG_PRINTF_ATTRIBUTE printf
#endif

#if PG_VERSION_NUM < 80300

typedef void *SPIPlanPtr;

#endif

#if PG_VERSION_NUM < 80400

extern int SPI_execute_with_args(const char *src, int nargs, Oid *argtypes,
	Datum *values, const char *nulls, bool read_only, long tcount);

#endif

extern void execute(int expected, const char *sql);
extern void execute_plan(int expected, SPIPlanPtr plan, Datum *values, const char *nulls);
extern void execute_with_format(int expected, const char *format, ...)
__attribute__((format(printf, 2, 3)));
extern void execute_with_args(int expected, const char *src, int nargs, Oid argtypes[], Datum values[], const bool nulls[]);
extern void execute_with_format_args(int expected, const char *format, int nargs, Oid argtypes[], Datum values[], const bool nulls[], ...)
__attribute__((format(printf, 2, 7)));

#endif   /* PGUT_SPI_H */
