CREATE  EXTENSION percona_pg_telemetry;

SELECT  percona_pg_telemetry_version();

SELECT  *
FROM    percona_pg_telemetry_status();

DROP    EXTENSION percona_pg_telemetry;
