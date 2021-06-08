----
-- Regression test to Global Temporary Table implementation
--
-- Test for GTT with index, the temporary table must have the
-- index too.
--
----

-- Import the library
LOAD 'pgtt';

-- Create a GTT like table
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

SET pgtt.enabled TO off;
CREATE INDEX ON pgtt_schema.t_glob_temptable1 (id);
CREATE INDEX ON pgtt_schema.t_glob_temptable1 (lbl);

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

-- With indexes still defined
SELECT c2.relname, i.indisprimary, i.indisunique, pg_catalog.pg_get_indexdef(i.indexrelid, 0, true),
  pg_catalog.pg_get_constraintdef(con.oid, true), contype
FROM pg_catalog.pg_class c, pg_catalog.pg_class c2, pg_catalog.pg_index i
  LEFT JOIN pg_catalog.pg_constraint con ON (conrelid = i.indrelid AND conindid = i.indexrelid AND contype IN ('p','u','x'))
WHERE c.oid = (
        SELECT c.oid FROM pg_catalog.pg_class c LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace WHERE c.relname = 't_glob_temptable1' AND n.nspname = 'pgtt_schema'
        ) AND c.oid = i.indrelid AND i.indexrelid = c2.oid
ORDER BY i.indisprimary DESC, c2.relname;

SET pgtt.enabled TO on;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');

-- Look if we have two tables now
SELECT regexp_replace(n.nspname, '\d+', 'x', 'g'), c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

-- and that the temporary table has the indexes too
SELECT tablename, indexname FROM pg_indexes WHERE tablename = 't_glob_temptable1' and schemaname LIKE 'pg_temp_%' ORDER BY tablename, indexname;

INSERT INTO t_glob_temptable1 VALUES (2, 'two');

-- Verify that the index is used
SET enable_bitmapscan TO off;
EXPLAIN (COSTS OFF) SELECT * FROM t_glob_temptable1 WHERE id = 2;

-- Reconnect and drop it
\c - -
LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

-- Create a GTT like table
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable2 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

CREATE INDEX ON t_glob_temptable2 (id);
SELECT * FROM t_glob_temptable2;
-- Must complain that the GTT is in use
CREATE INDEX ON t_glob_temptable2 (lbl);

SELECT pg_catalog.pg_get_indexdef(i.indexrelid, 0, true)
FROM pg_catalog.pg_class c, pg_catalog.pg_class c2, pg_catalog.pg_index i
LEFT JOIN pg_catalog.pg_constraint con ON (conrelid = i.indrelid AND conindid = i.indexrelid AND contype IN ('p','u','x'))
WHERE c.oid = 'pgtt_schema.t_glob_temptable2'::regclass::oid AND c.oid = i.indrelid AND i.indexrelid = c2.oid
ORDER BY i.indisprimary DESC, c2.relname;

\c - -
LOAD 'pgtt';

DROP TABLE t_glob_temptable2;
