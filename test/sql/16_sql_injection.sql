----
-- Regression test to Global Temporary Table implementation
--
-- Test that a GTT name containing an embedded single quote cannot
-- break out of pgtt's internally generated SPI queries and inject
-- additional SQL. Previously several of those queries interpolated
-- the table name straight into a single-quoted string literal
-- without escaping; a double-quoted identifier containing a quote
-- character could break out of the literal.
--
----

-- A canary table pgtt's internal queries have no legitimate reason to
-- touch. If the old, unescaped code path were still active, the
-- payload embedded in the GTT name below would empty this table out
-- from under us during CREATE GLOBAL TEMPORARY TABLE.
CREATE TABLE canary (id integer);
INSERT INTO canary VALUES (1);

-- The GTT name below is a valid, if unusual, double-quoted identifier
-- containing a single quote and a complete extra SQL statement. This
-- must be treated purely as a table name, not executed as SQL.
CREATE GLOBAL TEMPORARY TABLE "x'; DELETE FROM canary WHERE '1'='1" (id integer, lbl text) ON COMMIT PRESERVE ROWS;

-- The canary must be untouched.
SELECT * FROM canary;

-- The GTT must be registered under its exact, literal name.
SELECT relname FROM pgtt_schema.pg_global_temp_tables
 WHERE relname = 'x''; DELETE FROM canary WHERE ''1''=''1';

-- Renaming to another maliciously quoted name must be equally safe
-- (exercises the UPDATE ... SET relname = ... path). This must be
-- done before the GTT is materialized in this session, exactly as
-- in 04_rename.sql, since renaming an already-materialized GTT is
-- independently rejected for unrelated reasons.
ALTER TABLE "x'; DELETE FROM canary WHERE '1'='1" RENAME TO "y'; TRUNCATE canary; --";
SELECT * FROM canary;
SELECT relname FROM pgtt_schema.pg_global_temp_tables
 WHERE relname = 'y''; TRUNCATE canary; --';

-- The GTT must work completely normally despite its name.
INSERT INTO "y'; TRUNCATE canary; --" VALUES (1, 'test');
SELECT * FROM "y'; TRUNCATE canary; --";

-- The canary must still be untouched after normal use too.
SELECT * FROM canary;

-- Cleanup
\c - -
DROP TABLE "y'; TRUNCATE canary; --";
DROP TABLE canary;
