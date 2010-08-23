AM_CFLAGS = -I$(top_srcdir)/src
EXTRA_DIST = ref128-13-97-50-11-7-1.bdf \
             ref128-13-97-50-11-7-1.edf \
             ref128-13-97-50-11-7-1.gdf 

check_PROGRAMS = testbdf testedf testgdf readcheck
TESTS = testbdf testedf testgdf readcheck

if RUN_ERROR_TEST
check_PROGRAMS += errorcheck
TESTS += errorcheck
endif


testbdf_SOURCES = testbdf.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h
testbdf_LDADD = $(top_builddir)/src/libxdffileio.la

testedf_SOURCES = testedf.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h
testedf_LDADD = $(top_builddir)/src/libxdffileio.la

testgdf_SOURCES = testgdf.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h
testgdf_LDADD = $(top_builddir)/src/libxdffileio.la

errorcheck_SOURCES = errorcheck.c filecmp.c filecmp.h
errorcheck_LDADD = $(top_builddir)/src/libxdffileio.la

readcheck_SOURCES = readcheck.c
readcheck_LDADD = $(top_builddir)/src/libxdffileio.la


