EXTENSION  = pgtt
EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
	       sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

PGFILEDESC = "pgtt - Global Temporary Tables for PostgreSQL"

PG_CONFIG = pg_config

PGVEROK = $(shell $(PG_CONFIG) --version | egrep " (9.[456]|1[0123])" > /dev/null && echo yes || echo no)
PG_CPPFLAGS = -I$(libpq_srcdir)
PG_LDFLAGS = -L$(libpq_builddir) -lpq
PG_LIBDIR := $(shell $(PG_CONFIG) --libdir)

SHLIB_LINK = $(libpq)

ifeq ($(PGVEROK),yes)

DOCS = $(wildcard README*)
MODULES = pgtt

DATA = $(wildcard updates/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql

else
	$(error Minimum version of PostgreSQL required is 9.4)
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

