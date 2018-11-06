# - If the library source code has changed at all since the last update,
# then increment revision.
# - If any interfaces have been added, removed, or changed since the last
# update, increment current, and set revision to 0.
# - If any interfaces have been added since the last public release, then
# increment age.
# - If any interfaces have been removed since the last public release, then
# set age to 0.

m4_define([lib_current],0)
m4_define([lib_revision],3)
m4_define([lib_age],0)

# Setup autoconf
AC_INIT(xdffileio,[lib_current.lib_revision],[nicolas.bourdaud@gmail.com])
AC_CONFIG_SRCDIR([src/xdffile.c])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config/config.h])

# Setup automake
AM_INIT_AUTOMAKE([subdir-objects no-dist-gzip dist-xz
                  color-tests parallel-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])


AC_CANONICAL_HOST
AC_CANONICAL_BUILD


AC_PROG_CC_C99
gl_EARLY
AM_PROG_CC_C_O

LT_INIT([win32-dll disable-static])
AC_SUBST(LIBTOOL_DEPS)
gl_INIT

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

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile src/xdffileio.pc doc/Makefile tests/Makefile])
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

