-- percona_pg_telemetry--1.0.sql

CREATE FUNCTION percona_pg_telemetry_version()
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION percona_pg_telemetry_status(
    OUT latest_output_filename  text,
    OUT pt_enabled              boolean)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;
