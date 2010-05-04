#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "xdferror.h"
#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"
#include "xdfformatops.h"

#define TYPE_INT		0
#define TYPE_STRING		1
#define TYPE_DATATYPE		2
#define TYPE_DOUBLE		3

struct opt_detail
{
	int field;
	unsigned int type;
};

/******************************************************
 *                                                    *
 ******************************************************/
const struct opt_detail opts_ch_table[] = {
	{XDF_CHFIELD_ARRAY_INDEX, TYPE_INT},
	{XDF_CHFIELD_ARRAY_OFFSET, TYPE_INT},
	{XDF_CHFIELD_ARRAY_TYPE, TYPE_DATATYPE},
	{XDF_CHFIELD_STORED_TYPE, TYPE_DATATYPE},
	{XDF_CHFIELD_LABEL, TYPE_STRING},
	{XDF_CHFIELD_PHYSICAL_MIN, TYPE_DOUBLE},
	{XDF_CHFIELD_PHYSICAL_MAX, TYPE_DOUBLE}, 
	{XDF_CHFIELD_DIGITAL_MIN, TYPE_DOUBLE},	
	{XDF_CHFIELD_DIGITAL_MAX, TYPE_DOUBLE},
};
#define num_opts_ch	(sizeof(opts_ch_table)/sizeof(opts_ch_table[0]))

static int get_option_type(const struct opt_detail table[], unsigned int nmax, int field)
{
	int i = nmax-1;
	while (--i) {
		if (table[i].field == field)
			break;
	}
	return i;
}
#define get_ch_opt_type(field)  (get_option_type(opts_ch_table, num_opts_ch, (field)))


static void init_xdf_struct(struct xdffile* xdf, int fd, enum xdffiletype type, int mode)
{
	xdf->ready = 0;
	xdf->error = 0;
	xdf->mode = mode;
	xdf->ftype = type;
	xdf->fd = fd;
	xdf->buff = xdf->backbuff = NULL;
	xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;
	xdf->channels = NULL;
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->array_stride = NULL;
	xdf->array_pos = NULL;
}

static struct xdffile* init_read_xdf(int fd, enum xdffiletype type)
{
	unsigned char magickey[8];
	enum xdffiletype gtype;
	struct xdffile* xdf = NULL;
	int errnum = 0;

	// Guess file type
	if ( (read(fd, magickey, sizeof(magickey)) == -1)
	    || (lseek(fd, 0, SEEK_SET) == -1) ) {
	    	errnum = errno;
		goto error;
	}
	gtype = guess_file_type(magickey);
	errnum = EILSEQ;
	if ((gtype == XDF_ANY) || ((type != XDF_ANY)&&(type != gtype))) 
		goto error;
	
	// Allocate structure
	errnum = ENOMEM;
	if (!(xdf = alloc_xdffile(gtype)))
		goto error;
	
	// Initialize by reading the file
	init_xdf_struct(xdf, fd, gtype, XDF_READ);
	if (xdf->ops->read_header(xdf) == 0)
		return xdf;
	
	// Caught an error if we reach here
	errnum = xdf_get_error(xdf);
	xdf->ops->close_file(xdf);

error:
	set_xdf_error(NULL, errnum);
	return NULL;
}


static struct xdffile* init_write_xdf(int fd, enum xdffiletype type)
{
	struct xdffile* xdf = NULL;
	int errnum = 0;

	if (!(xdf = alloc_xdffile(type))) {
		set_xdf_error(NULL, errnum);
		return NULL;
	}
	
	init_xdf_struct(xdf, fd, type, XDF_WRITE);
	return xdf;
}


struct xdffile* xdf_open(const char* filename, int mode, enum xdffiletype type)
{
	struct xdffile* xdf = NULL;
	int oflags, fd = -1;

	// Argument validation
	if (((mode != XDF_WRITE)&&(mode != XDF_READ)) || !filename) {
		set_xdf_error(NULL, EINVAL);
		return NULL;
	}
	oflags = (mode == XDF_WRITE) ? O_WRONLY : O_RDONLY; 


	if ((fd = open(filename, oflags)) == -1) {
		set_xdf_error(NULL, errno);
		return NULL;
	}

	if (mode == XDF_READ)
		xdf = init_read_xdf(fd, type);
	else
		xdf = init_write_xdf(fd, type);

	return xdf;
}

struct xdf_channel* xdf_get_channel(struct xdffile* xdf, unsigned int index)
{
	struct xdf_channel* ch = xdf->channels;
	unsigned int ich = 0;

	while (ch && (ich<index)) {
		ich++;
		ch = ch->next;
	}

	return ch;
}

static int setconf_channel_double(struct xdf_channel* ch, enum xdfchfield field, double dval)
{
	int retval = 0;
	
	if (field == XDF_CHFIELD_DIGITAL_MIN)
		ch->digital_mm[0] = dval;
	else if (field == XDF_CHFIELD_DIGITAL_MAX)
		ch->digital_mm[1] = dval;
	else if (field == XDF_CHFIELD_PHYSICAL_MIN)
		ch->digital_mm[0] = dval;
	else if (field == XDF_CHFIELD_PHYSICAL_MAX)
		ch->digital_mm[1] = dval;
	else
		retval = ch->ops->set_channel(ch, field, dval);
	
	return retval;
}


static int setconf_channel_int(struct xdf_channel* ch, enum xdfchfield field, int ival)
{
	int retval = 0;
	if (field == XDF_CHFIELD_ARRAY_INDEX)
		ch->iarray = ival;
	else if (field == XDF_CHFIELD_ARRAY_OFFSET)
		ch->offset = ival;
	else if (field == XDF_CHFIELD_ARRAY_TYPE)
		ch->inmemtype = ival;
	else if (field == XDF_CHFIELD_STORED_TYPE)
		ch->infiletype = ival;
	else
		retval = ch->ops->set_channel(ch, field, ival);
	
	return retval;
}

int xdf_setconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int type, retval = 0, out = 0;


	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (!out && (field != XDF_CHFIELD_NONE)) {
		type = get_ch_opt_type(field);

		switch (field) {
		case TYPE_INT:	
		case TYPE_DATATYPE:	
			retval = setconf_channel_int(ch, field, va_arg(ap, int));
			break;

		case TYPE_STRING:
			retval = ch->ops->set_channel(ch, field, va_arg(ap, const char*));
			break;

		case TYPE_DOUBLE:
			retval = setconf_channel_double(ch, field, va_arg(ap, double));
			break;
			
		default:
			out = 1;
			retval = EINVAL;
			break;
		}
		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}

static int getconf_channel_double(struct xdf_channel* ch, enum xdfchfield field, double* dval)
{
	int retval = 0;
	
	if (field == XDF_CHFIELD_DIGITAL_MIN)
		*dval = ch->digital_mm[0];
	else if (field == XDF_CHFIELD_DIGITAL_MAX)
		*dval = ch->digital_mm[1];
	else if (field == XDF_CHFIELD_PHYSICAL_MIN)
		*dval = ch->digital_mm[0];
	else if (field == XDF_CHFIELD_PHYSICAL_MAX)
		*dval = ch->digital_mm[1];
	else
		retval = ch->ops->get_channel(ch, field, dval);
	
	return retval;
}


static int getconf_channel_int(struct xdf_channel* ch, enum xdfchfield field, int* ival)
{
	int retval = 0;

	if (field == XDF_CHFIELD_ARRAY_INDEX)
		*ival = ch->iarray;
	else if (field == XDF_CHFIELD_ARRAY_OFFSET)
		*ival = ch->offset;
	else if (field == XDF_CHFIELD_ARRAY_TYPE)
		*ival = ch->inmemtype;
	else if (field == XDF_CHFIELD_STORED_TYPE)
		*ival = ch->infiletype;
	else
		retval = ch->ops->get_channel(ch, field, ival);
	
	return retval;
}


int xdf_getconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int type, retval = 0, out = 0;

	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (!out && (field != XDF_CHFIELD_NONE)) {
		type = get_ch_opt_type(field);

		switch (field) {
		case TYPE_INT:	
		case TYPE_DATATYPE:	
			retval = getconf_channel_int(ch, field, va_arg(ap, int*));
			break;

		case TYPE_STRING:
			retval = ch->ops->set_channel(ch, field, va_arg(ap, const char**));
			break;

		case TYPE_DOUBLE:
			retval = getconf_channel_double(ch, field, va_arg(ap, double*));
			break;
			
		default:
			retval = EINVAL;
			out = 1;
			break;
		}
		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}


int xdf_copy_channel(struct xdf_channel* dst, struct xdf_channel* src)
{
	
	if (!dst || !src)
		return set_xdf_error(NULL, EINVAL);

	return dst->ops->copy_channel(dst, src);
}


struct xdf_channel* xdf_add_channel(struct xdffile* xdf)
{
	struct xdf_channel** curr = &(xdf->channels);
	struct xdf_channel* ch;

	// go to the end of the list of channel of the xdffile
	while (*curr)
		curr = &((*curr)->next);

	// Allocate new channel
	ch = xdf->ops->alloc_channel();
	if (!ch)
		return NULL;

	// Init the new channel with the previous one
	if (*curr) {
		xdf_copy_channel(ch, *curr);
		ch->offset += get_data_size(ch->inmemtype);
	}

	// Link the channel to the end
	ch->next = NULL;
	*curr = ch;

	return ch;
}


