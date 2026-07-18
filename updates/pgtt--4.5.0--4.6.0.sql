-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pgtt UPDATE" to load this file. \quit

----
-- SECURITY FIX: tighten default privileges on pg_global_temp_tables.
--
-- Every released version (2.0.0 through 4.5.0) granted ALL privileges
-- on this table to PUBLIC. Because this table is the single shared
-- bookkeeping catalog for every Global Temporary Table in the database
-- (owner, structure "code", ON COMMIT behavior, ...), this let any
-- authenticated role directly INSERT/UPDATE/DELETE/TRUNCATE rows that
-- belong to GTTs created by other users, with no ownership check
-- whatsoever -- entirely bypassing the ownership checks the extension
-- performs when it intercepts CREATE/DROP/RENAME TABLE.
--
-- SELECT is intentionally left available to PUBLIC: pgtt loads this
-- entire table into every backend's local cache on first use
-- regardless of who created which row (this is how GTTs remain usable
-- database-wide, matching Oracle-style GTT semantics), and the table
-- is documented as an introspectable catalog.
--
-- Roles that create/rename/drop GTTs (i.e. any role the DBA has
-- granted CREATE on the pgtt schema to, per the README) must now also
-- be granted explicit write access on this table, for example:
--   GRANT SELECT, INSERT, UPDATE, DELETE
--     ON @extschema@.pg_global_temp_tables TO <role>;
----
REVOKE ALL ON TABLE @extschema@.pg_global_temp_tables FROM PUBLIC;
GRANT SELECT ON TABLE @extschema@.pg_global_temp_tables TO PUBLIC;
