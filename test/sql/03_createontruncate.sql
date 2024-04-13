----
-- Regression test to Global Temporary Table implementation
--
-- Test for TRUNCATE on GTT with temporary table first access in a transaction.
-- The temporary table must not persist after the transaction rollback.
--
----

-- Import the library
-- LOAD 'pgtt';

-- Create a GTT like table
CREATE GLOBAL TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

BEGIN;

-- Truncate first will not create the temporary table, it will
-- be operated on the "template" table which will do nothing.
TRUNCATE t_glob_temptable1;

-- Look if we have two tables now
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Insert some value will create the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');
INSERT INTO t_glob_temptable1 VALUES (2, 'Two');

-- Look at content of the template for Global Temporary Table, must be empty
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Look at content of the Global Temporary Table
SELECT * FROM t_glob_temptable1;

-- Now truncate the temporary table
TRUNCATE t_glob_temptable1;

-- Verify that there is no mo rows
SELECT * FROM t_glob_temptable1;
ROLLBACK;

-- The "template" unlogged table exists, but the
-- temporary table not because of the rollback
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Reconnect and drop it
\c - -
-- LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

-- Reconnect to test locking, see #41
\c - -
-- LOAD 'pgtt';

-- Create a GTT like table
CREATE /*GLOBAL*/ TEMPORARY TABLE test_gtt (id int, lbl text);

SELECT * FROM pgtt_schema.test_gtt;  
SELECT * FROM pgtt_schema.test_gtt;  -- success

\c - -
-- LOAD 'pgtt';
-- Cleanup
DROP TABLE test_gtt;

