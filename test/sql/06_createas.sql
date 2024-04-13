----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT with TABLE ... AS clause.
--
----

-- Import the library
-- LOAD 'pgtt';

-- Create a GTT like table to test ON COMMIT PRESERVE ROWS
CREATE GLOBAL TEMPORARY TABLE t_glob_temptable1 AS SELECT * FROM source WITH DATA;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists as well as
-- the temporary table as we have used WITH DATA
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Look at content of the template for Global Temporary Table, must be empty
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Look at the temporary table itself, it must have the rows
SELECT * FROM t_glob_temptable1 ORDER BY id;

BEGIN;

-- With a new row
INSERT INTO t_glob_temptable1 VALUES (4, 'four');

-- Look at content of the template for Global Temporary Table, must be empty
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Look at content of the Global Temporary Table
SELECT * FROM t_glob_temptable1 ORDER BY id;

COMMIT;

SELECT * FROM t_glob_temptable1 ORDER BY id;

-- Reconnect and drop it
\c - -
-- LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

