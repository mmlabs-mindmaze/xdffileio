eol=

AM_CFLAGS = -I$(top_srcdir)/src \
	-DSRCDIR=\"$(abs_srcdir)\" \
	-DBUILDDIR=\"$(abs_builddir)\" \
	$(eol)

EXTRA_DIST = \
	ref128-13-97-50-11-7-1.bdf \
	ref128-13-97-50-11-7-1.edf \
	ref128-13-97-50-11-7-1.gdf1 \
	ref128-13-97-50-11-7-1.gdf2 \
	$(eol)

check_PROGRAMS = testbdf testedf testgdf1 testgdf2 readcheck gen-broken-gdf check-fixed-gdf
TESTS = testbdf testedf testgdf1 testgdf2 readcheck recompose-gdf.sh

if RUN_ERROR_TEST
check_PROGRAMS += errorcheck
TESTS += errorcheck
endif

TESTS_ENVIRONMENT = BUILDDIR=$(top_builddir)

testbdf_SOURCES = testbdf.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h checkseek.h checkseek.c
testbdf_LDADD = $(top_builddir)/src/libxdffileio.la

testedf_SOURCES = testedf.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h checkseek.h checkseek.c
testedf_LDADD = $(top_builddir)/src/libxdffileio.la

testgdf1_SOURCES = testgdf1.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h checkseek.h checkseek.c
testgdf1_LDADD = $(top_builddir)/src/libxdffileio.la

testgdf2_SOURCES = testgdf2.c filecmp.c filecmp.h copy_xdf.c copy_xdf.h\
		  validation.c validation.h checkseek.h checkseek.c
testgdf2_LDADD = $(top_builddir)/src/libxdffileio.la

errorcheck_SOURCES = errorcheck.c filecmp.c filecmp.h
errorcheck_LDADD = $(top_builddir)/src/libxdffileio.la

readcheck_SOURCES = readcheck.c
readcheck_LDADD = $(top_builddir)/src/libxdffileio.la

gen_broken_gdf_SOURCES = gen-broken-gdf.c
gen_broken_gdf_LDFLAGS = -lm
gen_broken_gdf_LDADD = $(top_builddir)/src/libxdffileio.la

check_fixed_gdf_SOURCES = check-fixed-gdf.c
check_fixed_gdf_LDFLAGS = -lm
check_fixed_gdf_LDADD = $(top_builddir)/src/libxdffileio.la
