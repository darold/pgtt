----
-- Regression test to Global Temporary Table implementation
--
-- This test is not part of the regression tests run with
-- make check install because it need manual changes.
--
-- Test for extension privilege to use non-superuser role.
-- For this test the extension must have been registered as
-- a plugin to allow simple user to use LOAD of the extension.
-- See Installation instruction in documentation for details.
-- Then execute:
--
--    createdb gtt_privilege
--    LANG=C psql -d gtt_privilege -f test/privilege.sql > results/privilege.out 2>&1
--    diff results/privilege.out test/expected/privilege.out
--    dropdb gtt_privilege
--    dropuser pgtt_user1
--
----

-- As superuser
CREATE ROLE pgtt_user1 LOGIN;
CREATE EXTENSION IF NOT EXISTS pgtt;
GRANT ALL ON SCHEMA pgtt_schema TO pgtt_user1;

-- Set session_preload_libraries
DO $$                          
BEGIN
    --EXECUTE format('ALTER DATABASE %I SET session_preload_libraries = ''$libdir/plugins/pgtt''', current_database());
    EXECUTE format('ALTER DATABASE %I SET session_preload_libraries = ''pgtt''', current_database());
END
$$;

\c - pgtt_user1

-- Import the library
-- LOAD '$libdir/plugins/pgtt';

SHOW search_path;

-- Create a GTT like table with ON COMMIT PRESERVE ROWS
CREATE /*GLOBAL*/ TEMPORARY TABLE t_glob_temptable1 (id integer, lbl text) ON COMMIT PRESERVE ROWS;

-- Look at Global Temporary Table definition
SELECT nspname, relname, preserved, code FROM pgtt_schema.pg_global_temp_tables;

-- With the first insert some value in the temporary table
INSERT INTO t_glob_temptable1 VALUES (1, 'One');
INSERT INTO t_glob_temptable1 VALUES (2, 'two');

-- Look if we have two tables now
SELECT n.nspname, c.relname FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE relname = 't_glob_temptable1';

SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_glob_temptable1;
SET pgtt.enabled TO on;

SELECT * FROM t_glob_temptable1;

