project(
  'gnome-software-plugin-gearlever',
  'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  # GNOME Software only scans its system plugin directory. Installing this
  # private-ABI module below /usr/local leaves it undiscoverable on distro
  # builds whose plugins live below /usr.
  default_options: ['c_std=gnu11', 'warning_level=2', 'prefix=/usr'],
)

cc = meson.get_compiler('c')
glib = dependency('glib-2.0', version: '>= 2.74')
gobject = dependency('gobject-2.0')
gio = dependency('gio-2.0')
gmodule = dependency('gmodule-2.0')
json_glib = dependency('json-glib-1.0', version: '>= 1.6')

plugin_api = get_option('plugin_api')
gs_libdir = get_option('gnome_software_libdir')
if gs_libdir == ''
  gs_libdir = join_paths(get_option('prefix'), get_option('libdir'), 'gnome-software')
endif

gs_search_dirs = [
  gs_libdir,
  '/usr/lib/' + host_machine.cpu_family() + '-linux-gnu/gnome-software',
  '/usr/lib64/gnome-software',
  '/usr/lib/gnome-software',
]

# Ubuntu installs GNOME Software's private runtime library but intentionally
# does not expose its unstable plugin headers/pkg-config file.  The small ABI
# compatibility declaration in include/ targets the selected plugin ABI.
gnome_software = cc.find_library('gnomesoftware', dirs: gs_search_dirs, required: true)

inc = include_directories('include', 'src')
common_deps = [glib, gobject, gio, json_glib]

plugin = shared_module(
  'gs_plugin_gearlever',
  ['src/gs-plugin-gearlever.c', 'src/gearlever-utils.c'],
  include_directories: inc,
  dependencies: common_deps + [gnome_software],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'gnome-software', 'plugins-' + plugin_api),
)

test_parser = executable(
  'test-parser',
  ['tests/test-parser.c', 'src/gearlever-utils.c'],
  include_directories: inc,
  dependencies: common_deps,
)
test('parser', test_parser)

test_plugin_load = executable(
  'test-plugin-load',
  'tests/test-plugin-load.c',
  include_directories: inc,
  dependencies: [glib, gobject, gio, gmodule, gnome_software],
)
test('plugin-load', test_plugin_load, args: [plugin.full_path()], depends: plugin)

test_plugin_updates = executable(
  'test-plugin-updates',
  'tests/test-plugin-updates.c',
  include_directories: inc,
  dependencies: [glib, gobject, gio, gmodule, gnome_software],
)
test('plugin-updates', test_plugin_updates, args: [plugin.full_path()], depends: plugin)
