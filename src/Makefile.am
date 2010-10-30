EXTRA_DIST=exports.sym

lib_LTLIBRARIES = libxdffileio.la
include_HEADERS = xdfio.h 

libxdffileio_la_SOURCES = xdffile.h xdffile.c xdfconfig.c	\
			  xdftypes.c xdftypes.h 		\
			  xdfevent.c xdfevent.h 		\
			  xdfio.h formatdecl.c			\
			  streamops.h streamops.c		\
			  ebdffile.c ebdf.h			\
			  gdf.c gdf.h

libxdffileio_la_LDFLAGS = $(AM_LDFLAGS) -export-symbols $(srcdir)/exports.sym 
libxdffileio_la_LIBADD = $(LTLIBOBJS)

