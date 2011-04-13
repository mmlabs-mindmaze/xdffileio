AC_DEFUN([AC_USE_SSEMATH],
[
  AC_REQUIRE([AC_PROG_CC])
  AC_MSG_CHECKING([for sse based math flag])
  sm_save_CFLAGS="$CFLAGS"
  CFLAGS="-mfpmath=sse $CFLAGS"
  AC_TRY_COMPILE(
        [],
        [],
        gl_cv_cc_ssemath=yes,
        gl_cv_cc_ssemath=no)
  CFLAGS="$sm_save_CFLAGS"
  if test $gl_cv_cc_ssemath = yes; then
    CFLAGS="-mfpmath=sse $CFLAGS"
  fi
  AC_MSG_RESULT([$gl_cv_cc_ssemath])
  AC_SUBST([CFLAGS])
])

AC_DEFUN([AC_CHECK_IEEE_FLOAT_ROUND],
[
AC_REQUIRE([AC_USE_SSEMATH])
AC_MSG_CHECKING([FPU rounding behavior])
HAVE_IEEE_FLOAT_ROUND=-1
AC_RUN_IFELSE(AC_LANG_PROGRAM([[double a = 3.0, b = 7.0;]],
		              [[volatile double c = a / b;] 
		               [return (c == a/b) ? 0 : 1;]]),
	      [AC_MSG_RESULT([correct IEEE behavior])
	      HAVE_IEEE_FLOAT_ROUND=1],
	      [AC_MSG_ERROR([Bad FPU rounding behavior.
The rounding behavior of your FPU does not follow IEEE standards.
There are three ways (among others) to enforce the expected behavior (best solutions first):
  - Specify a CPU architecture providing SSE2 supported by your CPU
          (If gcc is used, add -march=pentium4 or even better -march=native to compiler flags)
  - Force use of SSE2 instruction set if your CPU supports it
          (If gcc is used, add -msse2 and -mfpmath=sse to compiler flags)
  - Do not store floating point variables in registers
          (If gcc is used, add -ffloat-store to compiler flags)])
	       HAVE_IEEE_FLOAT_ROUND=0],
	      AC_MSG_RESULT([unable to test]))
])

