----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT defined inside a PLPGSQL function.
--
----

-- Import the library
-- LOAD 'pgtt';

CREATE OR REPLACE FUNCTION test_temp_table ()
RETURNS integer
AS $$
DECLARE
    nrows integer;
BEGIN

     CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1(id int, lbl text) ON COMMIT PRESERVE ROWS;
     INSERT INTO t_glob_temptable1 (id, lbl) SELECT i, md5(i::text) FROM generate_series(1, 10) i;
     SELECT count(*) INTO nrows FROM t_glob_temptable1 ;
     RETURN nrows;
END;
$$
LANGUAGE plpgsql SECURITY DEFINER;

-- Look at Global Temporary Table definition: none
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- Call the function, must returns 10 rows
SELECT test_temp_table();

-- Look at Global Temporary Table definition: table exists
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- Look if the temporary table exists outside the function call
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- A "template" unlogged table should exists
SET pgtt.enabled TO off;
SELECT a.attname,
  pg_catalog.format_type(a.atttypid, a.atttypmod),
  (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid, true) for 128)
   FROM pg_catalog.pg_attrdef d
   WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),
  a.attnotnull,
  pg_catalog.col_description(a.attrelid, a.attnum)
FROM pg_catalog.pg_attribute a
WHERE a.attrelid = (
        SELECT c.oid FROM pg_catalog.pg_class c LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace WHERE c.relname = 't_glob_temptable1' AND n.nspname = 'pgtt_schema'
        ) AND a.attnum > 0 AND NOT a.attisdropped
ORDER BY a.attnum;

-- Get rows from the template table
SELECT * FROM pgtt_schema.t_glob_temptable1;

-- Get rows from the temporary table
SET pgtt.enabled TO on;
SELECT * FROM t_glob_temptable1;

-- Reconnect without dropping the global temporary table
\c - -
-- LOAD 'pgtt';

SET pgtt.enabled TO off;
VACUUM pg_class;
SELECT pg_sleep(1);

-- Verify that only the temporary table have been dropped
-- Only the "template" unlogged table should exists
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Look at Global Temporary Table definition, the table must be present
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

SET pgtt.enabled TO on;

-- Call the function a second time - must fail the table already exists
SELECT test_temp_table();

-- Look at temporary table content, must be empty after the reconnect and function failure
SELECT * FROM t_glob_temptable1;

-- Now the "template" unlogged table should exists as well as the temporary table
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- Reconnect and drop it
\c - -
-- LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

DROP FUNCTION test_temp_table();
