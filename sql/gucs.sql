--
-- GUC sanity
--

CREATE EXTENSION percona_pg_telemetry;

SELECT application_name, backend_type, (backend_start IS NOT NULL) AS backend_started
FROM pg_stat_activity
WHERE application_name = 'percona_pg_telemetry';

\x
SELECT name, setting, unit, short_desc, min_val, max_val, boot_val, reset_val
FROM pg_settings
WHERE name LIKE 'percona_pg_telemetry.%'
ORDER BY name;
\x

ALTER SYSTEM SET percona_pg_telemetry.enabled = false;
SELECT pg_reload_conf();

SELECT pg_sleep(2);
SELECT application_name, backend_type, (backend_start IS NOT NULL) AS backend_started
FROM pg_stat_activity
WHERE application_name = 'percona_pg_telemetry';

ALTER SYSTEM RESET percona_pg_telemetry.enabled;
SELECT pg_reload_conf();

DROP EXTENSION percona_pg_telemetry;
