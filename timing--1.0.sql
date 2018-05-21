/* contrib/timing/timing--1.0.sql */

--complain if script is sourced in psql rather than via ALTER EXTENSION
\echo Use "CRAETE EXTENSION timing_table" to load this file. \quit

CREATE TABLE timing_table(
	id				    serial,
	timetype			text,
	last_time			timestamp,
	next_time			timestamp,
	inter_value			text
);

CREATE FUNCTION insert_timing(text,text)
RETURNS text
AS 'MODULE_PATHNAME','insert_timing'
LANGUAGE C STRICT PARALLEL RESTRICTED;

CREATE FUNCTION update(integer)
RETURNS text
AS 'MODULE_PATHNAME','update'
LANGUAGE C STRICT PARALLEL RESTRICTED;