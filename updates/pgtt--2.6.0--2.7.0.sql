--  complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgtt" to load this file. \quit

-- check the functions bodies as creation time, enabled by default
SET LOCAL check_function_bodies = on ;

-- make sure of client encoding
SET LOCAL client_encoding = 'UTF8';

