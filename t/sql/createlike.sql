----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT with TABLE ... (LIKE ...) clause.
--
----

-- Create a regular table with some rows
CREATE TABLE source (id integer, lbl varchar);

-- Import the library
LOAD 'pgtt';

-- Create a GTT like table to test ON COMMIT PRESERVE ROWS
CREATE GLOBAL TEMPORARY TABLE t_glob_temptable1 (LIKE source) ON COMMIT PRESERVE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

BEGIN;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');

-- Look if we have two tables now
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Look at content of the template for Global Temporary Table, must be empty
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Look at content of the Global Temporary Table
SELECT * FROM t_glob_temptable1;
COMMIT;

SELECT * FROM t_glob_temptable1;

