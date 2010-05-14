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


struct opt_detail
{
	int field;
	unsigned int type;
};

/******************************************************
 *                                                    *
 ******************************************************/
static const struct opt_detail opts_ch_table[] = {
	{XDF_CHFIELD_ARRAY_INDEX, TYPE_INT},
	{XDF_CHFIELD_ARRAY_OFFSET, TYPE_INT},
	{XDF_CHFIELD_ARRAY_DIGITAL, TYPE_INT},
	{XDF_CHFIELD_ARRAY_TYPE, TYPE_DATATYPE},
	{XDF_CHFIELD_STORED_TYPE, TYPE_DATATYPE},
	{XDF_CHFIELD_LABEL, TYPE_STRING},
	{XDF_CHFIELD_PHYSICAL_MIN, TYPE_DOUBLE},
	{XDF_CHFIELD_PHYSICAL_MAX, TYPE_DOUBLE}, 
	{XDF_CHFIELD_DIGITAL_MIN, TYPE_DOUBLE},	
	{XDF_CHFIELD_DIGITAL_MAX, TYPE_DOUBLE},
	{XDF_CHFIELD_UNIT, TYPE_STRING},
	{XDF_CHFIELD_TRANSDUCTER, TYPE_STRING},
	{XDF_CHFIELD_PREFILTERING, TYPE_STRING},
	{XDF_CHFIELD_RESERVED, TYPE_STRING},
};
#define num_opts_ch	(sizeof(opts_ch_table)/sizeof(opts_ch_table[0]))

static const struct opt_detail opts_info_table[] = {
	{XDF_FIELD_RECORD_DURATION, TYPE_DOUBLE},
	{XDF_FIELD_NSAMPLE_PER_RECORD, TYPE_INT},
	{XDF_FIELD_SUBJ_DESC, TYPE_STRING},
	{XDF_FIELD_REC_DESC, TYPE_STRING},
};
#define num_opts_info	(sizeof(opts_info_table)/sizeof(opts_info_table[0]))

static int get_option_type(const struct opt_detail table[], unsigned int nmax, int field)
{
	int i = nmax-1;
	do {
		if (table[i].field == field)
			return table[i].type;
	} while (i--);
	return -1;
}
#define get_ch_opt_type(field)  (get_option_type(opts_ch_table, num_opts_ch, (field)))
#define get_conf_opt_type(field)  (get_option_type(opts_info_table, num_opts_info, (field)))


static void init_xdf_struct(struct xdf* xdf, int fd, enum xdffiletype type, int mode)
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

static struct xdf* init_read_xdf(enum xdffiletype type, const char* filename)
{
	unsigned char magickey[8];
	enum xdffiletype gtype;
	struct xdf* xdf = NULL;
	int errnum = 0;
	int fd;

	// Open the file
	if ((fd = open(filename, O_RDONLY)) == -1) {
		set_xdf_error(NULL, errno);
		return NULL;
	}

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
	
	// We have caught an error if we reach here
	errnum = xdf_get_error(xdf);
	xdf->ops->close_file(xdf);

error:
	set_xdf_error(NULL, errnum);
	return NULL;
}


static struct xdf* init_write_xdf(enum xdffiletype type, const char* filename)
{
	struct xdf* xdf = NULL;
	int fd;
	mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;

	// Create the file
	if ((fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, mode)) == -1) {
		set_xdf_error(NULL, errno);
		return NULL;
	}

	// Allocates the xdffile structure
	if (!(xdf = alloc_xdffile(type))) {
		set_xdf_error(NULL, ENOMEM);
		return NULL;
	}
	
	init_xdf_struct(xdf, fd, type, XDF_WRITE);
	return xdf;
}


struct xdf* xdf_open(const char* filename, int mode, enum xdffiletype type)
{
	struct xdf* xdf = NULL;

	// Argument validation
	if (((mode != XDF_WRITE)&&(mode != XDF_READ)) || !filename) {
		set_xdf_error(NULL, EINVAL);
		return NULL;
	}

	// Structure creation 
	if (mode == XDF_READ)
		xdf = init_read_xdf(type, filename);
	else
		xdf = init_write_xdf(type, filename);

	return xdf;
}

struct xdfch* xdf_get_channel(const struct xdf* xdf, unsigned int index)
{
	struct xdfch* ch = xdf->channels;
	unsigned int ich = 0;

	while (ch && (ich<index)) {
		ich++;
		ch = ch->next;
	}

	return ch;
}


static int default_set_chconf(struct xdfch* ch, enum xdfchfield
field, union optval val)
{
	int retval = 0;

	if (field == XDF_CHFIELD_DIGITAL_MIN)
		ch->digital_mm[0] = val.d;
	else if (field == XDF_CHFIELD_DIGITAL_MAX)
		ch->digital_mm[1] = val.d;
	else if (field == XDF_CHFIELD_PHYSICAL_MIN)
		ch->physical_mm[0] = val.d;
	else if (field == XDF_CHFIELD_PHYSICAL_MAX)
		ch->physical_mm[1] = val.d;
	else if (field == XDF_CHFIELD_ARRAY_INDEX)
		ch->iarray = val.i;
	else if (field == XDF_CHFIELD_ARRAY_OFFSET)
		ch->offset = val.i;
	else if (field == XDF_CHFIELD_ARRAY_TYPE)
		ch->inmemtype = val.i;
	else if (field == XDF_CHFIELD_ARRAY_DIGITAL)
		ch->digital_inmem = val.i;
	else if (field == XDF_CHFIELD_STORED_TYPE)
		ch->infiletype = val.i;
	else
		retval = -1;
	
	return retval;
}

int xdf_set_chconf(struct xdfch* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int r1, r2, argtype, retval = 0;
	union optval val;

	if (ch == NULL)
		return set_xdf_error(NULL, EFAULT);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		argtype = get_ch_opt_type(field);
		if (argtype == TYPE_INT)
			val.i = va_arg(ap, int);
		else if (argtype == TYPE_DATATYPE)
			val.type = va_arg(ap, enum xdftype);
		else if (argtype == TYPE_STRING)
			val.str = va_arg(ap, const char*);
		else if (argtype == TYPE_DOUBLE)
			val.d = va_arg(ap, double);
		else {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}
		
		r1 = default_set_chconf(ch, field, val);
		r2 = ch->ops->set_channel(ch, field, val);
		if (r1 && r2) {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}


static int default_get_chconf(const struct xdfch* ch, enum xdfchfield
field, union optval* val)
{
	int retval = 0;

	if (field == XDF_CHFIELD_DIGITAL_MIN)
		val->d = ch->digital_mm[0];
	else if (field == XDF_CHFIELD_DIGITAL_MAX)
		val->d = ch->digital_mm[1];
	else if (field == XDF_CHFIELD_PHYSICAL_MIN)
		val->d = ch->physical_mm[0];
	else if (field == XDF_CHFIELD_PHYSICAL_MAX)
		val->d = ch->physical_mm[1];
	else if (field == XDF_CHFIELD_ARRAY_INDEX)
		val->i = ch->iarray;
	else if (field == XDF_CHFIELD_ARRAY_OFFSET)
		val->i = ch->offset;
	else if (field == XDF_CHFIELD_ARRAY_DIGITAL)
		val->i = ch->digital_inmem;
	else if (field == XDF_CHFIELD_ARRAY_TYPE)
		val->type = ch->inmemtype;
	else if (field == XDF_CHFIELD_STORED_TYPE)
		val->type = ch->infiletype;
	else
		retval = -1;
	
	return retval;
}


int xdf_get_chconf(const struct xdfch* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int r1, r2, argtype, retval = 0;
	union optval val;

	if (ch == NULL)
		return set_xdf_error(NULL, EFAULT);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		r1 = default_get_chconf(ch, field, &val);
		r2 = ch->ops->get_channel(ch, field, &val);
		if (r1 && r2) {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		argtype = get_ch_opt_type(field);
		if (argtype == TYPE_INT)
			*(va_arg(ap, int*)) = val.i;
		else if (argtype == TYPE_DATATYPE)
			*(va_arg(ap, enum xdftype*)) = val.type;
		else if (argtype == TYPE_STRING)
			*(va_arg(ap, const char**)) = val.str;
		else if (argtype == TYPE_DOUBLE)
			*(va_arg(ap, double*)) = val.d;
		else {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}


int xdf_copy_chconf(struct xdfch* dst, const struct xdfch* src)
{
	
	if (!dst || !src)
		return set_xdf_error(NULL, EINVAL);

	return dst->ops->copy_chconf(dst, src);
}


struct xdfch* xdf_add_channel(struct xdf* xdf)
{
	struct xdfch** curr = &(xdf->channels);
	struct xdfch *ch, *prev = NULL;

	// go to the end of the list of channel of the xdffile
	while (*curr) {
		prev = *curr;
		curr = &((*curr)->next);
	}

	// Allocate new channel
	ch = xdf->ops->alloc_channel();
	if (!ch)
		return NULL;

	// Init the new channel with the previous one
	if (prev) {
		xdf_copy_chconf(ch, prev);
		ch->offset += get_data_size(ch->inmemtype);
	}

	// Link the channel to the end
	ch->next = NULL;
	*curr = ch;
	xdf->numch++;

	return ch;
}


static int default_set_conf(struct xdf* xdf, enum xdffield field, union optval val)
{
	int retval = 0;

	if (field == XDF_FIELD_NSAMPLE_PER_RECORD)
		xdf->ns_per_rec = val.i;
	else if (field == XDF_FIELD_RECORD_DURATION) 
		xdf->rec_duration = val.d;
	else
		retval = -1;
	
	return retval;
}


int xdf_set_conf(struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int r1, r2, argtype, retval = 0;
	union optval val;

	if (xdf == NULL)
		return set_xdf_error(NULL, EFAULT);

	va_start(ap, field);
	while (field != XDF_FIELD_NONE) {
		argtype = get_conf_opt_type(field);
		if (argtype == TYPE_INT)
			val.i = va_arg(ap, int);
		else if (argtype == TYPE_DATATYPE)
			val.type = va_arg(ap, enum xdftype);
		else if (argtype == TYPE_STRING)
			val.str = va_arg(ap, const char*);
		else if (argtype == TYPE_DOUBLE)
			val.d = va_arg(ap, double);
		else {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}
		
		r1 = default_set_conf(xdf, field, val);
		r2 = xdf->ops->set_conf(xdf, field, val);
		if (r1 && r2) {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


static int default_get_conf(const struct xdf* xdf, enum xdffield field, union optval *val)
{
	int retval = 0;

	if (field == XDF_FIELD_NSAMPLE_PER_RECORD)
		val->i = xdf->ns_per_rec;
	else if (field == XDF_FIELD_RECORD_DURATION)
		val->d = xdf->rec_duration;
	else
		retval = -1;
	
	return retval;
}

int xdf_get_conf(const struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int r1, r2, argtype, retval = 0;
	union optval val;

	if (xdf == NULL)
		return set_xdf_error(NULL, EFAULT);

	va_start(ap, field);
	while (field != XDF_FIELD_NONE) {
		r1 = default_get_conf(xdf, field, &val);
		r2 = xdf->ops->get_conf(xdf, field, &val);
		if (r1 && r2) {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		argtype = get_conf_opt_type(field);
		if (argtype == TYPE_INT)
			*(va_arg(ap, int*)) = val.i;
		else if (argtype == TYPE_DATATYPE)
			*(va_arg(ap, enum xdftype*)) = val.type;
		else if (argtype == TYPE_STRING)
			*(va_arg(ap, const char**)) = val.str;
		else if (argtype == TYPE_DOUBLE)
			*(va_arg(ap, double*)) = val.d;
		else {
			retval = set_xdf_error(NULL, EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


int xdf_copy_conf(struct xdf* dst, const struct xdf* src)
{
	if (!dst || !src)
		return set_xdf_error(NULL, EINVAL);

	return dst->ops->copy_conf(dst, src);
}

