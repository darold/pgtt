----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT with ON COMMIT DELETE ROWS clause.
--
-- Test drop of GTT when in use throwing error and effective
-- when done in a separate session.
--
----

-- Create a GTT like table to test ON COMMIT DELETE ROWS
CREATE GLOBAL TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT DELETE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

BEGIN;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');

-- Look if we have two tables now
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Second insert, the temporary table exists
INSERT INTO t_glob_temptable1 VALUES (2, 'Two');

-- Look at content of the template for Global Temporary Table, must be empty
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Look at content of the Global Temporary Table
SELECT * FROM t_glob_temptable1;
COMMIT;

-- No row must perstist after the commit
SELECT * FROM t_glob_temptable1;

-- With holdabe cursor the rows must remain
BEGIN;
INSERT INTO t_glob_temptable1 VALUES (1, 'One');
INSERT INTO t_glob_temptable1 VALUES (2, 'Two');
DECLARE curs1 CURSOR WITH HOLD FOR SELECT * FROM t_glob_temptable1;
FETCH curs1;
COMMIT;

-- No row must perstist after the commit for the cursor
FETCH curs1;
-- But not for a new select
SELECT * FROM t_glob_temptable1;
CLOSE curs1;

-- Drop the global temporary table: fail because it is in use
DROP TABLE t_glob_temptable1;

-- Reconnect and drop it
\c - -

SHOW search_path;
DROP TABLE t_glob_temptable1;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables; -- should be empty

-- The "template" unlogged table should not exists anymore
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

