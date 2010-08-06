AC_DEFUN([AC_CHECK_IEEE_FLOAT_ROUND],
[
AC_MSG_CHECKING([FPU rounding behavior])
AC_RUN_IFELSE(AC_LANG_PROGRAM([[double a = 3.0, b = 7.0;]],
		              [[volatile double c = a / b;] 
		               [return (c == a/b) ? 0 : 1;]]),
	      AC_MSG_RESULT([correct IEEE behavior]),
	      AC_MSG_ERROR([Bad FPU rounding behavior.
The rounding behavior of your FPU does not follow IEEE standards.
There are two ways (among others) to enforce the expected behavior:
  - Use SSE2 instruction set if your CPU supports it
          (If gcc is used, add -msse2 and -mfpmath=sse to compiler flags)
  - Do not store floating point variables in registers
          (If gcc is used, add -ffloat-store to compiler flags)
]))
])
