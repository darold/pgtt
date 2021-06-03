EXTENSION  = pgtt
EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
	       sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

PGFILEDESC = "pgtt - Global Temporary Tables for PostgreSQL"

PG_CONFIG = pg_config

PG_CPPFLAGS = -I$(libpq_srcdir) -Wno-uninitialized
PG_LDFLAGS = -L$(libpq_builddir) -lpq
PG_LIBDIR := $(shell $(PG_CONFIG) --libdir)

SHLIB_LINK = $(libpq)

DOCS = $(wildcard README*)
MODULES = pgtt

DATA = $(wildcard updates/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql

TESTS        = 00_init 01_oncommitdelete 02_oncommitpreserve \
	       03_createontruncate 04_rename 05_useindex \
	       06_createas 07_createlike 08_plplgsql \
	       09_transaction 10_foreignkey 11_after_error

REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

