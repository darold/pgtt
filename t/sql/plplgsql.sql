----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT defined inside a PLPGSQL function.
--
----

-- Import the library
LOAD 'pgtt';

CREATE OR REPLACE FUNCTION test_temp_table ()
RETURNS boolean
AS $$
BEGIN

     CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1(id int, lbl text) ON COMMIT PRESERVE ROWS;
     INSERT INTO t_glob_temptable1 (id, lbl) SELECT i, md5(i::text) FROM generate_series(1, 10) i;
     PERFORM * FROM t_glob_temptable1 ;
     RETURN true;
END;
$$
LANGUAGE plpgsql SECURITY DEFINER;

-- Look at Global Temporary Table definition: none
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- Call the function
SELECT test_temp_table();

-- Look at Global Temporary Table definition: tablae exists
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
SET pgtt.enabled TO off;
\d pgtt_schema.t_glob_temptable1;

-- Get rows from the template table
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

-- Get rows from the temporary table
SELECT * FROM t_glob_temptable1;

-- Verify that both tables have been dropped
SET pgtt.enabled TO off;

-- The "template" unlogged table should not exists anymore as weel as the temporary table
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables; -- should be empty

SET pgtt.enabled TO on;

-- Call the function a second time
SELECT test_temp_table();

-- Look at temporary table content
SELECT * FROM t_glob_temptable1;

