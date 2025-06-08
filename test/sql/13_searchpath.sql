----
-- Regression test to Global Temporary Table implementation
--
-- Test on search_path forced by the extension
--
----


SHOW search_path ;
create schema first_schema;
create schema second_schema;
SET search_path TO "$user", public, fisrt_schema, second_schema,pg_catalog;
SHOW search_path ;
CREATE EXTENSION pgtt;
SHOW search_path ;
SET search_path TO pg_catalog;
SHOW search_path ;
SET search_path TO public;
SHOW search_path ;
