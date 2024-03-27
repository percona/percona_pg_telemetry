percona_telemetry_sources = files(
  'pt_json.c',
  'percona_telemetry.c',
)

if host_system == 'windows'
  percona_telemetry_sources += rc_lib_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'percona_telemetry',
    '--FILEDESC', 'percona_telemetry - extension for Percona telemetry data collection',])
endif

percona_telemetry = shared_module('percona_telemetry',
  percona_telemetry_sources,
  kwargs: contrib_mod_args + {
    'dependencies': contrib_mod_args['dependencies'],
  },
)
contrib_targets += percona_telemetry

install_data(
  'percona_telemetry.control',
  'percona_telemetry--1.0.sql',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'percona_telemetry',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'regress': {
    'sql': [
      'basic',
      'debug_json',
      'gucs',
    ],
    'regress_args': ['--temp-config', files('percona_telemetry.conf')],

# Disabled because these regression tests require the lib to be added to
# shared preload libraries, which typical installcheck users do not have.

    # Disabled because these regression tests require the lib to be
    # added to shared preload libraries, which typical installcheck
    # users do not have.
    'runningcheck': false,
  },
}
