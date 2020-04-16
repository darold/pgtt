/*
 * Executed before every regression, to clean up specifics.
 */
DROP DATABASE gtt_testdb;
CREATE DATABASE gtt_testdb;

-- ALTER DATABASE gtt_testdb SET search_path TO pgtt_schema, public;

\c gtt_testdb

-- Create the PostgreSQL extension
CREATE EXTENSION pgtt;

