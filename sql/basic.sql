--
-- basic sanity
--

CREATE  EXTENSION percona_telemetry;

-- Sleep so that waiting on agent becomes true
SELECT  pg_sleep(3);

SELECT  name
FROM    pg_settings
WHERE   name LIKE 'percona_telemetry.%';

SELECT  percona_telemetry_version();

SELECT  regexp_replace(regexp_replace(latest_output_filename, '\d{11,}', '<INSTANCE ID>', 'g'), '\d{6,}', '<TIMESTAMP>', 'g') AS latest_output_filename
        , pt_enabled
FROM    percona_telemetry_status();

DROP    EXTENSION percona_telemetry;
