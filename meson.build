project('xdffileio', 'c',
        version : '0.3',
        default_options : [
            'warning_level=1',
        ],
        license : 'LGPL-3.0',
        meson_version: '>= 0.49'
)

# follow semantic versionning (https://semver.org)
# * MAJOR version when you make incompatible API changes,
# * MINOR version when you add functionality in a backwards-compatible manner
# * PATCH version when you make backwards-compatible bug fixes.
major = '0'
minor = '0'
patch = '3'
version = major + '.' + minor + '.' + patch

cc = meson.get_compiler('c')
configuration_inc = include_directories('src')

# additional (optional) warnings
flags = [
        '-Wshadow',
        '-Wstrict-prototypes',
        '-Wmissing-prototypes',
]
add_project_arguments(cc.get_supported_arguments(flags), language : 'c')

# define HAVE_CONFIG_H with compiler command line to include the generated
# config.h file (same as autotools)
add_project_arguments('-DHAVE_CONFIG_H', language : 'c')

config = configuration_data()
subdir('config/api-exports')

# Defined for compatibility with autotools
# The other such macros defined by autotools are not used and will not be added
config.set_quoted('PACKAGE_STRING', meson.project_name() + ' ' + meson.project_version())

# list of mandatory headers
check_headers = [
    'math.h',
]
foreach h : check_headers
    cc.check_header(h)
endforeach

# write config file
build_cfg = 'config.h'  # named as such to match autotools build system
configure_file(output : build_cfg, configuration : config)

headers = files(
        'src/xdfio.h',
)
sources = files(
        'src/common.h',
        'src/ebdffile.c',
        'src/ebdf.h',
        'src/formatdecl.c',
        'src/gdf1.c',
        'src/gdf1.h',
        'src/gdf2.c',
        'src/gdf2.h',
        'src/streamops.c',
        'src/streamops.h',
        'src/xdfconfig.c',
        'src/xdfevent.c',
        'src/xdfevent.h',
        'src/xdffile.c',
        'src/xdffile.h',
        'src/xdfio.h',
        'src/xdftypes.c',
        'src/xdftypes.h',
)

install_headers(headers)

libmath = cc.find_library('m', required : true)
mmlib = cc.find_library('mmlib', required : true)
mmlib_deps = [mmlib]
if host_machine.system() != 'windows'
        mmlib_deps += dependency('threads')
endif
xdffileio = shared_library('xdffileio',
        sources,
        install : true,
        version : version,
        include_directories : configuration_inc,
        dependencies : mmlib_deps,
)

pkg = import('pkgconfig')
pkg.generate(xdffileio)

gdf_repair = executable('gdf-repair',
        'src/gdf-repair.c',
        install : true,
        include_directories : include_directories('.', 'src'),
        link_with : [xdffileio],
        dependencies : mmlib,
)


#
# LIBRARY PYTHON
#

pymod = import('python')
py3 = pymod.find_installation('python3', required : true)
py_dep = dependency('python3')

py3.extension_module('_pyxdf',
        'pyxdf/pyxdf.c',
        install : true,
        subdir : 'xdf',
        link_with : xdffileio,
        include_directories : configuration_inc,
        dependencies : py_dep,
)

py3.install_sources('pyxdf/__init__.py', subdir : 'xdf')


#
# TESTS
#

if get_option('tests')

        unittest_args = [
                '-DSRCDIR="' + meson.source_root() + '/tests"',
                '-DBUILDDIR="' + meson.current_build_dir() + '"',
        ]

        testbdf_sources = files(
                'tests/checkseek.c',
                'tests/checkseek.h',
                'tests/copy_xdf.c',
                'tests/copy_xdf.h',
                'tests/filecmp.c',
                'tests/filecmp.h',
                'tests/testbdf.c',
                'tests/validation.c',
                'tests/validation.h',
        )
        testbdf = executable('testbdf',
                testbdf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies : mmlib,
        )
        test('testbdf', testbdf, is_parallel : false, suite : 'core')

        testedf_sources = files(
                'tests/checkseek.c',
                'tests/checkseek.h',
                'tests/copy_xdf.c',
                'tests/copy_xdf.h',
                'tests/filecmp.c',
                'tests/filecmp.h',
                'tests/testedf.c',
                'tests/validation.c',
                'tests/validation.h',
        )
        testedf = executable('testedf',
                testedf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies : mmlib,
        )
        test('testedf', testedf, is_parallel : false, suite : 'core')

        testgdf1_sources = files(
                'tests/checkseek.c',
                'tests/checkseek.h',
                'tests/copy_xdf.c',
                'tests/copy_xdf.h',
                'tests/filecmp.c',
                'tests/filecmp.h',
                'tests/testgdf1.c',
                'tests/validation.c',
                'tests/validation.h',
        )
        testgdf1 = executable('testgdf1',
                testedf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies : mmlib,
        )
        test('testgdf1', testgdf1, is_parallel : false, suite : 'core')

        testgdf2_sources = files(
                'tests/checkseek.c',
                'tests/checkseek.h',
                'tests/copy_xdf.c',
                'tests/copy_xdf.h',
                'tests/filecmp.c',
                'tests/filecmp.h',
                'tests/testgdf2.c',
                'tests/validation.c',
                'tests/validation.h',
        )
        testgdf2 = executable('testgdf2',
                testedf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies : mmlib,
        )
        test('testgdf2', testgdf2, is_parallel : false, suite : 'core')

        if cc.has_header_symbol('sys/resource.h', 'getrlimit')
                errorcheck_sources = files(
                        'tests/errorcheck.c',
                        'tests/filecmp.c',
                        'tests/filecmp.h',
                )
                errorcheck = executable('errorcheck',
                        errorcheck_sources,
                        c_args : unittest_args,
                        include_directories : include_directories('src', 'tests'),
                        link_with : xdffileio,
                        dependencies : mmlib,
                )
                test('errorcheck', errorcheck,
                     is_parallel : false,
                     suite : 'core'
                )
        endif  # has getrlimit()

        readcheck_sources = files(
                'tests/readcheck.c',
        )
        readcheck = executable('readcheck',
                readcheck_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies : mmlib,
        )
        test('readcheck', readcheck, is_parallel : false, suite : 'core')

        gen_broken_gdf_sources = files(
                'tests/gen-broken-gdf.c',
        )
        gen_broken_gdf = executable('gen-broken-gdf',
                gen_broken_gdf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies: [libmath, mmlib],
        )

        check_fixed_gdf_sources = files(
                'tests/check-fixed-gdf.c',
        )
        check_fixed_gdf = executable('check-fixed-gdf',
                check_fixed_gdf_sources,
                c_args : unittest_args,
                include_directories : include_directories('src', 'tests'),
                link_with : xdffileio,
                dependencies: [libmath, mmlib],
        )

        test('check_fixed_gdf',
                files ('tests/recompose-gdf.sh'),
                env : ['EXECUTABLETEST=' + meson.current_build_dir(),
                       'EXECUTABLESRC=' + meson.current_build_dir(),],
                is_parallel : false,
                suite : 'core',
        )
endif


#
# TESTS PYTHON

if get_option('tests-python')
        test('pytests',
                files ('pyxdf/tests/python-unittests.tap'),
                env : ['builddirlibs=' + meson.current_build_dir(),
                       'builddir=' + meson.current_build_dir(),
                       'srcdir=' + meson.source_root() + '/pyxdf',
                       'srcdirlibs=' + meson.current_build_dir(),
                ],
                suite : 'python',
        )
endif


#
# DOC
#

man_files = files(
        'doc/gdf-repair.1',
        'doc/xdf_add_channel.3',
        'doc/xdf_add_event.3',
        'doc/xdf_add_evttype.3',
        'doc/xdf_close.3',
        'doc/xdf_closest_type.3',
        'doc/xdf_copy_chconf.3',
        'doc/xdf_copy_conf.3',
        'doc/xdf_define_arrays.3',
        'doc/xdf_fdopen.3',
        'doc/xdf_get_channel.3',
        'doc/xdf_get_chconf.3',
        'doc/xdf_get_conf.3',
        'doc/xdf_get_event.3',
        'doc/xdf_get_evttype.3',
        'doc/xdf_open.3',
        'doc/xdf_prepare_transfer.3',
        'doc/xdf_read.3',
        'doc/xdf_seek.3',
        'doc/xdf_set_chconf.3',
        'doc/xdf_set_conf.3',
        'doc/xdf_write.3',
)
install_man(man_files)

# compile example as a test, only install the sources
example = executable('create-bdf-example',
        files('doc/example/create_bdf.c'),
        include_directories : include_directories('.', 'src'),
        link_with : xdffileio,
        dependencies : libmath,
)

example2 = executable('create-copy-datafile-example',
        files('doc/example/copy_datafile.c'),
        include_directories : include_directories('.', 'src'),
        link_with : xdffileio,
        dependencies : libmath,
)

install_subdir('doc/example',
        install_dir :  get_option('datadir') / 'doc' / 'xdffileio')

install_data(files('AUTHORS', 'COPYING', 'NEWS', 'README'),
        install_dir : get_option('datadir') / 'doc' / 'xdffileio')

#
# DEVTOOLS
#

# test public headers for most warnings and C++ compatibility
api_compat_test = run_target('api-compat-test',
        command : [
            'bash',
            files('devtools/api-compat-test.sh'),
            headers,
        ]
)


codespell = find_program('codespell', required : false)
if codespell.found()
    # check for typo in all possible sources
    run_target('spelling',
        command : [
            codespell,
            '--ignore-words', files('devtools/codespell.ignore'),
            sources + man_files,
        ],
    )
endif # codespell
