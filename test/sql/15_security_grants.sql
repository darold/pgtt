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
CREATE ROLE gtt_other LOGIN;

-- A role provisioned exactly as the README's non-superuser setup
-- instructions describe, plus the additional table-level grant this
-- fix now requires (documented in the 4.5.0->4.5.1 upgrade script).
CREATE ROLE gtt_owner LOGIN;
GRANT ALL ON SCHEMA pgtt_schema TO gtt_owner;
GRANT SELECT, INSERT, UPDATE, DELETE ON pgtt_schema.pg_global_temp_tables TO gtt_owner;

-- Baseline (superuser) sanity check: the table only grants SELECT to
-- PUBLIC now, not ALL.
SELECT grantee, privilege_type
  FROM information_schema.role_table_grants
 WHERE table_schema = 'pgtt_schema'
   AND table_name = 'pg_global_temp_tables'
   AND grantee = 'public'
 ORDER BY privilege_type;

-- gtt_owner creates a GTT using its documented privileges.
\c - gtt_owner

CREATE /*GLOBAL*/ TEMPORARY TABLE t_security_grants (id integer, lbl text) ON COMMIT PRESERVE ROWS;
INSERT INTO t_security_grants VALUES (1, 'one');
SELECT * FROM t_security_grants;

-- gtt_other (no special grants, PUBLIC-equivalent) can still SELECT
-- the catalog and the template table directly...
\c - gtt_other
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

-- The registration and the data gtt_owner owns must be completely
-- unaffected by the rejected attempts above.
\c - gtt_owner
SELECT * FROM t_security_grants;
SELECT nspname, relname, preserved FROM pgtt_schema.pg_global_temp_tables WHERE relname = 't_security_grants';


-- Cleanup. Reconnect first, a GTT can only be dropped from a session
-- that has not already materialized it.
\c - gtt_owner
DROP TABLE t_security_grants;
