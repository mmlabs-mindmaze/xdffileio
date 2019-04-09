lib_LTLIBRARIES = libxdffileio.la
include_HEADERS = xdfio.h 
bin_PROGRAMS = gdf-repair

defexecdir = $(bindir)
defexec_DATA =
DISTCLEANFILES = $(defexec_DATA)

libxdffileio_la_SOURCES = xdffile.h xdffile.c xdfconfig.c	\
			  xdftypes.c xdftypes.h 		\
			  xdfevent.c xdfevent.h 		\
			  xdfio.h formatdecl.c			\
			  streamops.h streamops.c		\
			  ebdffile.c ebdf.h			\
			  gdf1.c gdf1.h				\
			  gdf2.c gdf2.h

AM_CPPFLAGS=-I../lib -I$(top_srcdir)/lib
libxdffileio_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined \
			  -version-info $(CURRENT):$(REVISION):$(AGE) 
libxdffileio_la_LIBADD = ../lib/libgnu.la

gdf_repair_SOURCES = gdf_repair.c
gdf_repair_LDADD = libxdffileio.la ../lib/libgnu.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xdffileio.pc

if HAVE_LD_OUTPUT_DEF
libxdffileio_la_LDFLAGS += -Wl,--output-def,libxdffileio-0.def
defexec_DATA += libxdffileio-0.def
endif
