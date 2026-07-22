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

SET search_path = public;

-- Test repeated SET search_path inside PL/pgSQL with memory pressure.
-- Verifies that search_path modification does not corrupt cached plans.
CREATE OR REPLACE FUNCTION test_searchpath_repeated() RETURNS void AS $$
BEGIN
    FOR i IN 1..100 LOOP
        SET SEARCH_PATH = public;
        SET Search_Path = public;
        PERFORM decode(repeat('00', 1024), 'hex');
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT test_searchpath_repeated();

-- Verify search_path still contains pgtt_schema after repeated SET
SHOW search_path;

-- Cleanup
DROP FUNCTION test_searchpath_repeated();
RESET search_path;
