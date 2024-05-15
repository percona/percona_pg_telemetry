# contrib/percona_pg_telemetry/Makefile

MODULE_big = percona_pg_telemetry
OBJS = \
	$(WIN32RES) \
	pt_json.o	\
	percona_pg_telemetry.o

EXTENSION = percona_pg_telemetry
DATA = percona_pg_telemetry--1.0.sql

PGFILEDESC = "percona_pg_telemetry - extension for Percona telemetry data collection."

REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/percona_pg_telemetry/percona_pg_telemetry.conf
REGRESS = basic debug_json gucs

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/percona_pg_telemetry
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
