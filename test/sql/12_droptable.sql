----
-- Regression test to Global Temporary Table implementation
--
-- Test on dropping regular table when the extension is loaded
--
----

-- Import the library
LOAD 'pgtt';

-- Create a GTT like table
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- A "template" unlogged table should exists
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

-- Create and drop a regular table
CREATE TABLE t1 (id integer);
DROP TABLE t1;

-- Cleanup
DROP TABLE t_glob_temptable1;

