-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgtt" to load this file. \quit

----
-- Fix privileges on schema dedicated to the global temporary table
----
REVOKE ALL ON SCHEMA @extschema@ FROM PUBLIC;
GRANT USAGE ON SCHEMA @extschema@ TO PUBLIC;

----
-- Table used to store information about Global Temporary Tables.
-- Content will be loaded in memory by the pgtt extension.
----
CREATE TABLE @extschema@.pg_global_temp_tables (
	relid integer NOT NULL,
	nspname name NOT NULL,
	relname name NOT NULL,
	preserved boolean,
	code text,
	UNIQUE (nspname, relname)
);

----
-- SECURITY (fix for public write access to the catalog table):
-- Every session that uses pgtt needs to be able to *read* this table
-- (gtt_load_global_temporary_tables() scans it on first use in every
-- backend, and it is documented as an introspectable catalog), so
-- SELECT is kept available to PUBLIC. INSERT/UPDATE/DELETE/TRUNCATE
-- are intentionally NOT granted to PUBLIC any more: previously ALL
-- privileges were granted here, which let any authenticated role
-- directly tamper with (or delete) any other role's GTT registration
-- with no ownership check at all, entirely bypassing the ownership
-- checks the extension's CREATE/DROP/RENAME TABLE interception
-- performs. Roles that need to create/rename/drop GTTs (i.e. roles
-- the DBA has granted CREATE on @extschema@ to, per the README) must
-- now also be granted explicit write access on this table, e.g.:
--   GRANT SELECT, INSERT, UPDATE, DELETE
--     ON @extschema@.pg_global_temp_tables TO <role>;
----
REVOKE ALL ON TABLE @extschema@.pg_global_temp_tables FROM PUBLIC;
GRANT SELECT ON TABLE @extschema@.pg_global_temp_tables TO PUBLIC;

-- Include tables into pg_dump
SELECT pg_catalog.pg_extension_config_dump('pg_global_temp_tables', '');
