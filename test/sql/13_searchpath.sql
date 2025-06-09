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
