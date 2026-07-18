----
-- Regression test to Global Temporary Table implementation
--
-- Test for unsupported foreign keys on GTT.
--
----
-- Import the library
-- LOAD 'pgtt';
-- Must throw ERROR: attempt to create referential integrity constraint on temporary table.
CREATE /*GLOBAL*/ TEMPORARY TABLE t2 (c1 integer, FOREIGN KEY (c1) REFERENCES source (id));
BEGIN;
CREATE /*GLOBAL*/ TEMPORARY TABLE t2 (c1 integer);
-- Must throw ERROR: attempt to create referential integrity constraint on temporary table.
ALTER TABLE t2 ADD FOREIGN KEY (c1) REFERENCES source (id);
ROLLBACK;
BEGIN;
-- Must be valid statement
CREATE TABLE t3 (c1 integer, FOREIGN KEY (c1) REFERENCES source (id));
ROLLBACK;

----
-- Regression test for the column-level REFERENCES form of a foreign
-- key, which a previous text-regex implementation of this check
-- (searching only for the literal substring "FOREIGN KEY") failed to
-- catch, since this syntax contains no such substring.
----
-- Must throw the same error as the table-level FOREIGN KEY (...) form above.
CREATE /*GLOBAL*/ TEMPORARY TABLE t4 (c1 integer REFERENCES source (id));
BEGIN;
CREATE /*GLOBAL*/ TEMPORARY TABLE t4 (c1 integer);
-- Column-level REFERENCES added later via ALTER TABLE must also fail,
-- exactly like the table-level form tested above.
ALTER TABLE t4 ADD FOREIGN KEY (c1) REFERENCES source (id);
ROLLBACK;
BEGIN;
-- Must be valid statement: column-level REFERENCES on a regular table.
CREATE TABLE t5 (c1 integer REFERENCES source (id));
ROLLBACK;
