--
-- GUC sanity
--

CREATE EXTENSION percona_telemetry;

SELECT application_name, backend_type, (backend_start IS NOT NULL) AS backend_started
FROM pg_stat_activity
WHERE application_name = 'percona_telemetry';

\x
SELECT name, setting, unit, short_desc, min_val, max_val, boot_val, reset_val
FROM pg_settings
WHERE name LIKE 'percona_telemetry.%'
ORDER BY name;
\x

ALTER SYSTEM SET percona_telemetry.enabled = false;
SELECT pg_reload_conf();

SELECT pg_sleep(2);
SELECT application_name, backend_type, (backend_start IS NOT NULL) AS backend_started
FROM pg_stat_activity
WHERE application_name = 'percona_telemetry';

ALTER SYSTEM RESET percona_telemetry.enabled;
SELECT pg_reload_conf();

DROP EXTENSION percona_telemetry;
