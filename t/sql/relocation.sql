----
-- Regression test to Global Temporary Table implementation
--
-- Test for extension relocation and use of schema qualifier.
-- For this test 'schema' and 'relocatable' must be commented
-- in the extension's control file.
----

DROP EXTENSION pgtt CASCADE;
DROP SCHEMA "SESSION" CASCADE;
CREATE SCHEMA "SESSION";
CREATE EXTENSION pgtt SCHEMA "SESSION";

-- Import the library
LOAD 'pgtt';

SHOW search_path;

-- Create a GTT like table with ON COMMIT PRESERVE ROWS
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

SET pgtt.enabled TO off;
CREATE INDEX ON "SESSION".t_glob_temptable1 (id);
CREATE INDEX ON "SESSION".t_glob_temptable1 (lbl);

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM "SESSION".pg_global_temp_tables;

-- A "template" unlogged table should exists
\d "SESSION".t_glob_temptable1;

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
SELECT * FROM "SESSION".t_glob_temptable1;

-- Setting new search_path might force addition of the extension schema too
CREATE SCHEMA newnsp;
SET search_path TO newnsp,public;

SELECT * FROM t_glob_temptable1 WHERE id = 2;
SELECT * FROM "SESSION".t_glob_temptable1;

