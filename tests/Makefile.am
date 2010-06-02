AM_CFLAGS = -I$(top_srcdir)/src
EXTRA_DIST = ref128-13-97-17-11-7-1.bdf

check_PROGRAMS = testbdf errorcheck

testbdf_SOURCES = testbdf.c
testbdf_LDADD = $(top_builddir)/src/libxdffileio.la

errorcheck_SOURCES = errorcheck.c
errorcheck_LDADD = $(top_builddir)/src/libxdffileio.la

TESTS = testbdf errorcheck

