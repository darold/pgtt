----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT with TABLE ... (LIKE ...) clause.
--
----

-- Import the library
LOAD 'pgtt';

-- Create a GTT like table to test ON COMMIT PRESERVE ROWS
CREATE GLOBAL TEMPORARY TABLE t_glob_temptable1 (
	LIKE source
	INCLUDING ALL
) ON COMMIT PRESERVE ROWS;

-- Look at table description
\d+ t_glob_temptable1

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

BEGIN;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');

-- Look at temp table description
\d+ t_glob_temptable1

-- Look if we have two tables now
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

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
LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

