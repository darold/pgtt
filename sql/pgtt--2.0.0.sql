-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgtt" to load this file. \quit

----
-- Create schema dedicated to the global temporary table
----
CREATE SCHEMA IF NOT EXISTS @extschema@;
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
GRANT ALL ON TABLE @extschema@.pg_global_temp_tables TO PUBLIC;

-- Include tables into pg_dump
SELECT pg_catalog.pg_extension_config_dump('pg_global_temp_tables', '');

