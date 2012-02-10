lib_LTLIBRARIES = libxdffileio.la
include_HEADERS = xdfio.h 

libxdffileio_la_SOURCES = xdffile.h xdffile.c xdfconfig.c	\
			  xdftypes.c xdftypes.h 		\
			  xdfevent.c xdfevent.h 		\
			  xdfio.h formatdecl.c			\
			  streamops.h streamops.c		\
			  ebdffile.c ebdf.h			\
			  gdf1.c gdf1.h				\
			  gdf2.c gdf2.h

AM_CPPFLAGS=-I../lib
libxdffileio_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined \
			  -version-info $(CURRENT):$(REVISION):$(AGE) 
libxdffileio_la_LIBADD = ../lib/libgnu.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xdffileio.pc
