----
-- Regression test to Global Temporary Table implementation
--
-- Test that PUBLIC no longer has write access to the shared
-- pg_global_temp_tables catalog table or to a GTT's "template"
-- table, only SELECT -- and that a role provisioned per the
-- documented non-superuser setup (CREATE on the pgtt schema, plus
-- the additional explicit grant this fix now requires on
-- pg_global_temp_tables) can still fully create, use and drop a GTT.
--
----

-- A role with no special pgtt privileges beyond the schema's default
-- USAGE grant to PUBLIC (i.e. representative of PUBLIC itself).
CREATE ROLE pgtt_user2 LOGIN;

-- A role provisioned exactly as the README's non-superuser setup
-- instructions describe, plus the additional table-level grant this
-- fix now requires (documented in the 4.5.0->4.5.1 upgrade script).
CREATE ROLE pgtt_user1 LOGIN;
GRANT ALL ON SCHEMA pgtt_schema TO pgtt_user1;
GRANT SELECT, INSERT, UPDATE, DELETE ON pgtt_schema.pg_global_temp_tables TO pgtt_user1;

-- Baseline (superuser) sanity check: the table only grants SELECT to
-- PUBLIC now, not ALL.
SELECT grantee, privilege_type
  FROM information_schema.role_table_grants
 WHERE table_schema = 'pgtt_schema'
   AND table_name = 'pg_global_temp_tables'
   AND grantee = 'public'
 ORDER BY privilege_type;

-- pgtt_user1 creates a GTT using its documented privileges.
\c - pgtt_user1
CREATE /*GLOBAL*/ TEMPORARY TABLE t_security_grants (id integer, lbl text) ON COMMIT PRESERVE ROWS;
INSERT INTO t_security_grants VALUES (1, 'one');
SELECT * FROM t_security_grants;

-- pgtt_user2 (no special grants, PUBLIC-equivalent) can still SELECT
-- the catalog and the template table directly...
\c - pgtt_user2
SELECT nspname, relname, preserved FROM pgtt_schema.pg_global_temp_tables WHERE relname = 't_security_grants';
SET pgtt.enabled TO off;
SELECT * FROM pgtt_schema.t_security_grants;
SET pgtt.enabled TO on;

-- ...but can no longer write to either. Every statement below must
-- fail with a permission error.
INSERT INTO pgtt_schema.pg_global_temp_tables VALUES (0, 'pgtt_schema', 'spoofed_gtt', true, 'id integer');
UPDATE pgtt_schema.pg_global_temp_tables SET code = 'hijacked' WHERE relname = 't_security_grants';
DELETE FROM pgtt_schema.pg_global_temp_tables WHERE relname = 't_security_grants';
TRUNCATE pgtt_schema.pg_global_temp_tables;

SET pgtt.enabled TO off;
INSERT INTO pgtt_schema.t_security_grants VALUES (99, 'should not be allowed');
UPDATE pgtt_schema.t_security_grants SET lbl = 'hijacked';
DELETE FROM pgtt_schema.t_security_grants;
TRUNCATE pgtt_schema.t_security_grants;
SET pgtt.enabled TO on;

-- The registration and the data pgtt_user1 owns must be completely
-- unaffected by the rejected attempts above.
\c - pgtt_user1
SELECT * FROM t_security_grants;
SELECT nspname, relname, preserved FROM pgtt_schema.pg_global_temp_tables WHERE relname = 't_security_grants';
DROP TABLE t_security_grants;

-- Cleanup
\c - -
DROP ROLE pgtt_user1;
DROP ROLE pgtt_user2;
