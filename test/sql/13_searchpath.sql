----
-- Regression test to Global Temporary Table implementation
--
-- Test on search_path forced by the extension
--
----
DROP EXTENSION pgtt;

SHOW search_path ;
create schema first_schema;
create schema second_schema;
SET search_path TO "$user",public,pg_catalog,fisrt_schema,second_schema;
SHOW search_path ;
CREATE EXTENSION pgtt;
SHOW search_path ;
SET search_path TO pg_catalog;
SHOW search_path ;
SET search_path TO public;
SHOW search_path ;

-- Test only pg_catalog in search_path.
DROP EXTENSION pgtt;
\c - -
SHOW search_path;
SET search_path TO pg_catalog;
SHOW search_path;
CREATE EXTENSION pgtt;
SHOW search_path;

-- Test the pg_catalog at end of search_path.
DROP EXTENSION pgtt;
\c - -
SET search_path TO "$user", public, pg_catalog;
SHOW search_path;
CREATE EXTENSION pgtt;
SHOW search_path;

-- Test empty search path like set by pg_dump
SELECT pg_catalog.set_config('search_path', '', false);
SHOW search_path;

-- Repeated SET search_path in a PL/pgSQL loop. The parse tree of the SET
-- statement is cached and reused at each iteration, the extension must not
-- add its schema to the statement itself. See issue #59.
\c - -
CREATE OR REPLACE FUNCTION test_searchpath_repeated() RETURNS void AS $$
BEGIN
    FOR i IN 1..100 LOOP
        SET search_path = public;
        PERFORM decode(repeat('00', 1024), 'hex');
    END LOOP;
END;
$$ LANGUAGE plpgsql;
SELECT test_searchpath_repeated();
SHOW search_path;
DROP FUNCTION test_searchpath_repeated();

-- The pgtt schema must be looked for in the search_path using an exact
-- comparison of the schema names: a schema whose name just includes the
-- name of the pgtt schema must not prevent it to be added, and a schema
-- whose name ends with pg_catalog must not be truncated.
\c - -
CREATE SCHEMA pgtt_schema_bak;
CREATE SCHEMA foo_pg_catalog;
SELECT pg_catalog.set_config('search_path', 'public, pgtt_schema_bak', false);
SHOW search_path;
SELECT pg_catalog.set_config('search_path', 'public, foo_pg_catalog', false);
SHOW search_path;
SELECT pg_catalog.set_config('search_path', 'public, foo_pg_catalog, pg_catalog', false);
SHOW search_path;
DROP SCHEMA pgtt_schema_bak;
DROP SCHEMA foo_pg_catalog;
