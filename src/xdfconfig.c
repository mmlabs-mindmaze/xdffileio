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


int xdf_setconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int retval = 0;
	const char* string = NULL;
	double dval;
	unsigned int ival;
	enum xdftype type;

	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		switch (field) {
		case XDF_CHFIELD_ARRAY_INDEX:	/* unsigned int */
		case XDF_CHFIELD_ARRAY_OFFSET:	/* unsigned int */
			ival = va_arg(ap, unsigned int);
			retval = ch->ops->set_channel(ch, field, ival);
			break;

		case XDF_CHFIELD_ARRAY_TYPE:		/* enum xdftype */
		case XDF_CHFIELD_STORED_TYPE:		/* enum xdftype */
			type = va_arg(ap, enum xdftype);
			retval = ch->ops->set_channel(ch, field, type);
			break;

		case XDF_CHFIELD_STORED_LABEL:       /* const char*  */
			string = va_arg(ap, const char*);
			retval = ch->ops->set_channel(ch, field, string);
			break;

		case XDF_CHFIELD_PHYSICAL_MIN:	/* double 	*/
		case XDF_CHFIELD_PHYSICAL_MAX:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MIN:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MAX:	/* double 	*/
			dval = va_arg(ap, double);
			retval = ch->ops->set_channel(ch, field, dval);
			break;
		default:
			break;
		}
	}
	va_end(ap);
	
	return retval;
}


int xdf_getconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int retval = 0, out = 0;
	const char** string = NULL;
	double* dval;
	unsigned int* ival;
	enum xdftype* type;

	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (!out) {
		switch (field) {
		case XDF_CHFIELD_NONE:
			out = 1;
			break;

		case XDF_CHFIELD_ARRAY_INDEX:	/* unsigned int */
		case XDF_CHFIELD_ARRAY_OFFSET:	/* unsigned int */
			ival = va_arg(ap, unsigned int*);
			retval = ch->ops->get_channel(ch, field, ival);
			break;

		case XDF_CHFIELD_ARRAY_TYPE:		/* enum xdftype */
		case XDF_CHFIELD_STORED_TYPE:		/* enum xdftype */
			type = va_arg(ap, enum xdftype*);
			retval = ch->ops->get_channel(ch, field, type);
			break;

		case XDF_CHFIELD_STORED_LABEL:       /* const char*  */
			string = va_arg(ap, const char**);
			retval = ch->ops->get_channel(ch, field, string);
			break;

		case XDF_CHFIELD_PHYSICAL_MIN:	/* double 	*/
		case XDF_CHFIELD_PHYSICAL_MAX:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MIN:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MAX:	/* double 	*/
			dval = va_arg(ap, double*);
			retval = ch->ops->get_channel(ch, field, dval);
			break;
		}
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
	unsigned int offset;
	enum xdftype type;

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
		xdf_getconf_channel(ch, XDF_CHFIELD_ARRAY_OFFSET, &offset,
		                        XDF_CHFIELD_ARRAY_TYPE, &type,
					XDF_CHFIELD_NONE);
		offset += get_data_size(type);
		xdf_setconf_channel(ch, XDF_CHFIELD_ARRAY_OFFSET, offset,
					XDF_CHFIELD_NONE);
	}

	// Link the channel to the end
	ch->next = NULL;
	*curr = ch;

	return ch;
}


