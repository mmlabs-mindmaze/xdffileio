dist_man_MANS = gdf-repair.1
		
nobase_dist_doc_DATA = example/Makefile		\
                       example/create_bdf.c	\
		       example/copy_datafile.c

AM_CFLAGS=-I$(top_srcdir)/src

# Verify at least that the example compiles
check_PROGRAMS = create_bdf copy_datafile
create_bdf_SOURCES = example/create_bdf.c
create_bdf_LDADD = $(top_builddir)/src/libxdffileio.la -lm
copy_datafile_SOURCES = example/copy_datafile.c
copy_datafile_LDADD = $(top_builddir)/src/libxdffileio.la -lm
