/*
 * Must be executed before all regression test.
 */

-- Create the PostgreSQL extension
CREATE EXTENSION pgtt;

-- Create a regular table with some rows
CREATE TABLE source (id integer, lbl varchar);
INSERT INTO source VALUES (1, 'one'), (2, 'two'),(1, 'three');


