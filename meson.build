project('asunder',
  'c',
  version: '2.9.3',
  default_options: [
    'c_std=gnu11',
    'warning_level=3',
  ],
  meson_version: '>=0.45',
)

i18n = import('i18n')
config = configuration_data()

###############################################################################
# Dependencies

gtk3    = dependency('gtk+-3.0',    version: '>=3.11.0')
libcddb = dependency('libcddb',     version: '>=0.9.5')

gettext_package = meson.project_name()

datadir_abs = join_paths(get_option('prefix'), get_option('datadir'))
config.set_quoted('PACKAGE',            meson.project_name())
config.set_quoted('PACKAGE_DATA_DIR',   datadir_abs)
config.set_quoted('PACKAGE_LOCALE_DIR', join_paths(datadir_abs, 'locale'))

add_project_arguments(
  '-DHAVE_CONFIG_H',
  '-DENABLE_NLS',
  language: 'c'
)

# Port to gtk3
add_project_arguments(
  '-Wdeprecated-declarations',
  '-DGDK_DISABLE_DEPRECATED',
  '-DGTK_DISABLE_DEPRECATED',
  '-DGLIB_DISABLE_DEPRECATED',
  language: 'c',
)

subdir('src')
subdir('data')
subdir('po')
