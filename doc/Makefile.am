dist_man_MANS = xdf_add_channel.3 xdf_get_channel.3		\
		xdf_set_chconf.3 xdf_get_chconf.3 		\
		xdf_copy_chconf.3				\
		xdf_open.3 xdf_close.3				\
		xdf_set_conf.3 xdf_get_conf.3 xdf_copy_conf.3	\
		xdf_define_arrays.3 xdf_prepare_transfer.3	\
		xdf_write.3 xdf_read.3 xdf_seek.3		\
		xdf_closest_type.3				\
		xdf_add_event.3 xdf_get_event.3			\
		xdf_add_evttype.3 xdf_get_evttype.3
		
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
