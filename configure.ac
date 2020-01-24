# - If the library source code has changed at all since the last update,
# then increment revision.
# - If any interfaces have been added, removed, or changed since the last
# update, increment current, and set revision to 0.
# - If any interfaces have been added since the last public release, then
# increment age.
# - If any interfaces have been removed since the last public release, then
# set age to 0.

m4_define([lib_current],4)
m4_define([lib_revision],0)
m4_define([lib_age],4)

# Setup autoconf
AC_INIT(xdffileio,[1.0],[nicolas.bourdaud@gmail.com])
AC_CONFIG_SRCDIR([src/xdffile.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config/config.h])
AC_REQUIRE_AUX_FILE([tap-driver.sh])

# Setup automake
AM_INIT_AUTOMAKE([subdir-objects no-dist-gzip dist-xz
                  color-tests parallel-tests foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])


AC_CANONICAL_HOST
AC_CANONICAL_BUILD

# hack to only consider python3 interpreter (NOT python == python3)
m4_define_default([_AM_PYTHON_INTERPRETER_LIST], [python3 ])
AM_PATH_PYTHON([3])
PKG_CHECK_MODULES_EXT(PYTHONDEV, python3)
MM_PYTHON_MODULE([numpy], required)


AC_PROG_CC_C99
AM_PROG_CC_C_O

LT_INIT([win32-dll disable-static])
AC_SUBST(LIBTOOL_DEPS)

AC_SET_HOSTSYSTEM
AC_DEF_API_EXPORT_ATTRS
AC_C_BIGENDIAN

# Test IEEE floating point calculation behavior 
AC_CHECK_IEEE_FLOAT_ROUND

# Test missing functions
AC_SEARCH_LIBS([pthread_create], [pthread posix4], 
               [], AC_MSG_ERROR([The pthread library has not been found]))
AC_CHECK_FUNC(setrlimit, [run_error_test=true], [run_error_test=false])
AC_CHECK_FUNCS([pthread_sigmask])

AM_CONDITIONAL(RUN_ERROR_TEST, [test "x$run_error_test" = "xtrue"])

AC_SUBST([CURRENT],[lib_current])
AC_SUBST([REVISION],[lib_revision])
AC_SUBST([AGE],[lib_age])

# Optional build of 'check' based tests
PKG_CHECK_MODULES_EXT(CHECK, [check >= 0.9.12],
                      [has_check=yes; tap_in_check=yes],
                      [PKG_CHECK_MODULES_EXT(CHECK, [check], [has_check=yes], [has_check=no])])
MM_CHECK_LIB([mmthr_create], [mmlib], MMLIB, [], [AC_MSG_ERROR([mmlib library required])])
AC_ARG_WITH([check], AC_HELP_STRING([--with-check], [Build and run check based tests. @<:@default=detect@:>@]),
            [], [with_check=detect])
AS_CASE([$with_check],
        [yes], [AS_IF([test "$has_check" != no],
            [build_check_tests=yes],
            [AC_MSG_FAILURE([check required but not found])])
        ],
        [no], [build_check_tests=no],
        [detect], [build_check_tests=$has_check],
        [AC_MSG_FAILURE([invalid argument for '--with-check' option])]
)
AM_CONDITIONAL(BUILD_CHECK_TESTS, [test "$build_check_tests" = yes])
AM_CONDITIONAL(TAP_SUPPORT_IN_CHECK, [test "x$tap_in_check" = xyes])


AC_CONFIG_FILES([Makefile src/Makefile src/xdffileio.pc doc/Makefile tests/Makefile pyxdf/Makefile])
AC_OUTPUT

if test "$HAVE_IEEE_FLOAT_ROUND" = -1 
then
echo "WARNING!!!
Because of cross-compiling, the configure script has been unable to  
determine the rounding behavior of your FPU. Make sure that is follows the 
IEEE floating point standard. A non standard behavior might happen in
particular on x86 architecture (not on x86-64 by default)
There are two ways (among others) to enforce the expected behavior:
  - Use SSE2 instruction set if your CPU supports it
          (If gcc is used, add -msse2 and -mfpmath=sse to compiler flags)
  - Do not store floating point variables in registers
          (If gcc is used, add -ffloat-store to compiler flags)"
fi

