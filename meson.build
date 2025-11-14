percona_pg_telemetry_sources = files(
  'percona_pg_telemetry.c',
)

if host_system == 'windows'
  percona_pg_telemetry_sources += rc_lib_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'percona_pg_telemetry',
    '--FILEDESC', 'percona_pg_telemetry - extension for Percona telemetry data collection',])
endif

percona_pg_telemetry = shared_module('percona_pg_telemetry',
  percona_pg_telemetry_sources,
  kwargs: contrib_mod_args + {
    'dependencies': contrib_mod_args['dependencies'],
  },
)
contrib_targets += percona_pg_telemetry

install_data(
  'percona_pg_telemetry.control',
  'percona_pg_telemetry--1.0.sql',
  'percona_pg_telemetry--1.0--1.1.sql',
  'percona_pg_telemetry--1.1--1.2.sql',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'percona_pg_telemetry',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'regress': {
    'sql': [
      'basic',
    ],
    'regress_args': ['--temp-config', files('percona_pg_telemetry.conf')],

# Disabled because these regression tests require the lib to be added to
# shared preload libraries, which typical installcheck users do not have.

    # Disabled because these regression tests require the lib to be
    # added to shared preload libraries, which typical installcheck
    # users do not have.
    'runningcheck': false,
  },
}
