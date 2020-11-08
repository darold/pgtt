-- Import the library
LOAD 'pgtt';

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

-- Verify the registering of the temporary table
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- Second insert failure
INSERT INTO t_glob_temptable1 VALUES (2, two);

ROLLBACK;

-- Look if we have two tables now
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Insert a new row
BEGIN;

-- With the first insert some value in the temporary table
-- Should not return an error that table doesn't exists
INSERT INTO t_glob_temptable1 VALUES (2, 'Two');

-- Look if we have two tables now
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Verify the insert
SELECT * FROM t_glob_temptable1;

COMMIT;

-- Reconnect and drop the GTT
\c - -

LOAD 'pgtt';

SHOW search_path;
DROP TABLE t_glob_temptable1;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables; -- should be empty

-- The "template" unlogged table should not exists anymore
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

