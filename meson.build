project(
  'remote-build-queue',
  'cpp',
  version: '0.1.0',
  license: 'LGPL-2.1-only',
  default_options: [ 'cpp_std=c++17' ],
)

nix_main = dependency('nix-main', required: true)
nix_store = dependency('nix-store', required: true)
libpq = dependency('libpq', required: true)
boost = dependency('boost', required: true)
nlohmann_json = dependency('nlohmann_json', required: true)

cpp_args = [ '-Werror', '-Wmissing-field-initializers', ]

lib_srcs = [
  'src/lib/concat-strings.cc',
  'src/lib/dequeue.cc',
  'src/lib/event.cc',
  'src/lib/job.cc',
  'src/lib/postgres.cc',
]

enqueue_srcs = [
  'src/enqueue/build-requirements.cc',
  'src/enqueue/main.cc',
  'src/enqueue/postgres.cc',
]

queue_srcs = [
  'src/remote-build-queue/machines.cc',
  'src/remote-build-queue/main.cc',
  'src/remote-build-queue/postgres.cc',
  'src/remote-build-queue/worker.cc',
]

libremote_srcs = lib_srcs + enqueue_srcs + queue_srcs

install_subdir('sql', install_dir : 'libexec',)

libremote_include = include_directories('include')

libremote_build = library('remote_build',
  libremote_srcs,
  include_directories: libremote_include,
  dependencies: [ libpq, nix_store ],
  install: true,
  cpp_args: cpp_args,
)

libremote_build_objects = libremote_build.extract_objects(
  libremote_srcs,
)

install_headers([
  'include/concat-strings.hh',
  'include/event.hh',
  'include/postgres.hh',
  'include/uuid.hh',
  'include/dequeue.hh',
])

install_headers(
  [
    'include/enqueue/build-requirements.hh',
    'include/enqueue/main.hh',
    'include/enqueue/postgres.hh',
  ],
  subdir: 'enqueue'
)

install_headers(
  [
    'include/remote-build-queue/machines.hh',
    'include/remote-build-queue/main.hh',
    'include/remote-build-queue/postgres.hh',
    'include/remote-build-queue/worker.hh',
  ],
  subdir: 'remote-build-queue'
)

executable('enqueue', [ 'src/enqueue/enqueue.cc', ],
  dependencies: [ boost, libpq, nix_main, nix_store, nlohmann_json ],
  include_directories: libremote_include,
  install: true,
  install_dir: 'libexec',
  cpp_args: cpp_args,
  objects: libremote_build_objects,
)
 
executable('remote-build-queue', [ 'src/remote-build-queue/remote-build-queue.cc', ],
  include_directories: libremote_include,
  dependencies: [ boost, libpq, nix_main, nix_store, nlohmann_json ],
  install: true,
  cpp_args: cpp_args,
  objects: libremote_build_objects,
)
