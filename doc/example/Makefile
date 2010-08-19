LIBS=-lxdffileio -lm

all: create_bdf copy_datafile

create_bdf: create_bdf.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

copy_datafile: copy_datafile.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean: 
	$(RM) create_bdf
	$(RM) copy_datafile
