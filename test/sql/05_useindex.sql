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
\d pgtt_schema.t_glob_temptable1;

SET pgtt.enabled TO on;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');

-- Look if we have two tables now
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

SET pgtt.enabled TO off;
\d t_glob_temptable1;
SET pgtt.enabled TO on;

INSERT INTO t_glob_temptable1 VALUES (2, 'two');

SELECT * FROM t_glob_temptable1 WHERE id = 2;

-- Reconnect and drop it
\c - -
LOAD 'pgtt';

-- Cleanup
DROP TABLE t_glob_temptable1;

