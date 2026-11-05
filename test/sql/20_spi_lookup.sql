----
-- Regression test to Global Temporary Table implementation
--
-- Test that an unexpected result of the lookup of the newly created
-- "template" table is reported as an error instead of crashing the
-- backend. An event trigger renames the "template" table just after its
-- creation so that the lookup by name returns no row.
--
----

CREATE FUNCTION rename_new_template() RETURNS event_trigger
LANGUAGE plpgsql AS $$
DECLARE
	r record;
BEGIN
	FOR r IN SELECT * FROM pg_event_trigger_ddl_commands()
		WHERE command_tag IN ('CREATE TABLE', 'CREATE TABLE AS') AND schema_name = 'pgtt_schema'
	LOOP
		EXECUTE format('ALTER TABLE %s RENAME TO renamed_by_evt', r.object_identity);
	END LOOP;
END;
$$;

CREATE EVENT TRIGGER evt_rename_template ON ddl_command_end
	WHEN TAG IN ('CREATE TABLE') EXECUTE FUNCTION rename_new_template();

-- Must fail with an error, not crash the backend
CREATE GLOBAL TEMPORARY TABLE t_glob_spi1 (id int);

-- Same with CREATE TABLE AS, the "template" table is then created
-- with a CREATE TABLE AS statement.
DROP EVENT TRIGGER evt_rename_template;
CREATE EVENT TRIGGER evt_rename_template ON ddl_command_end
	WHEN TAG IN ('CREATE TABLE AS') EXECUTE FUNCTION rename_new_template();
CREATE GLOBAL TEMPORARY TABLE t_glob_spi2 AS SELECT 1 AS id;

-- The session must still be usable
SELECT 'session still alive' AS status;

-- Nothing must have been registered
SELECT count(*) FROM pgtt_schema.pg_global_temp_tables WHERE relname IN ('t_glob_spi1', 't_glob_spi2');

-- Cleanup
DROP EVENT TRIGGER evt_rename_template;
DROP FUNCTION rename_new_template();
