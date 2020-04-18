----
-- Regression test to Global Temporary Table implementation
--
-- Test for transaction manamgement on GTT.
--
-- Test that the creation a GTT in rollbacked transaction
-- will not preserve it.
--
----

-- Import the library
LOAD 'pgtt';

BEGIN;

-- Register the Global temporary table in a transaction
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
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

ROLLBACK;

-- The GTT must not exists
SELECT * FROM t_glob_temptable1;

-- Return nothing
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Register the Global temporary table outside the transaction
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

BEGIN;

-- Drop the GTT
DROP TABLE t_glob_temptable1;

ROLLBACK;

-- Insert some value will create the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');
INSERT INTO t_glob_temptable1 VALUES (2, 'Two');

-- The GTT must not exists
SELECT * FROM t_glob_temptable1;

-- Both tables muste exists
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Reconnect and drop it
\c - -
LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

