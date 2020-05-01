LOAD 'pgtt';
-- Must throw ERROR: attempt to create referential integrity constraint on temporary table.
CREATE /*GLOBAL*/ TEMPORARY TABLE t2 (c1 integer, FOREIGN KEY (c1) REFERENCES source (id));
BEGIN;
CREATE /*GLOBAL*/ TEMPORARY TABLE t2 (c1 integer);
-- Must throw ERROR: attempt to create referential integrity constraint on temporary table.
ALTER TABLE t2 ADD FOREIGN KEY (c1) REFERENCES source (id);

