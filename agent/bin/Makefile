#
# pg_statsinfo: bin/Makefile
#
#    Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
SRCS = \
	pg_statsinfo.c \
	autovacuum.c \
	checkpoint.c \
	collector.c \
	logger.c \
	logger_in.c \
	logger_out.c \
	maintenance.c \
	snapshot.c \
	writer.c \
	pg_control.c \
	pgut/pgut.c \
	pgut/pgut-list.c \
	pgut/pgut-pthread.c

OBJS = $(SRCS:.c=.o)
DATA = pg_statsrepo.sql pg_statsrepo_partition.sql pg_statsrepo_alert.sql uninstall_pg_statsrepo.sql
PROGRAM = pg_statsinfo
REGRESS = pg_statsinfo

PG_CPPFLAGS = -I$(libpq_srcdir) -DFRONTEND -DPGUT_MULTI_THREADED -DPGUT_OVERRIDE_ELOG
PG_LIBS = $(libpq) -lpthread

ifndef USE_PGXS
top_builddir = ../../..
makefile_global = $(top_builddir)/src/Makefile.global
ifeq "$(wildcard $(makefile_global))" ""
USE_PGXS = 1    # use pgxs if not in contrib directory
endif
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/$(MODULE_big)
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))
