#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"


/******************************************************
 *             options table definitions              *
 ******************************************************/
struct opt_detail
{
	int field;
	unsigned int type;
};

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
	{XDF_FIELD_SAMPLING_FREQ, TYPE_INT},
	{XDF_FIELD_NCHANNEL, TYPE_INT},
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


/******************************************************
 *            xDF structure initialization            *
 ******************************************************/

/* \param xdf	pointer to a valid xdf structure
 * \param fd	file descriptor to be used with the xdf file
 * \param type	type of the xdf file
 * \param mode	mode of the file
 *
 * Initialize a xdf structure the provided and default values
 */
static void init_xdf_struct(struct xdf* xdf, int fd, enum xdffiletype type, int mode)
{
	xdf->ready = 0;
	xdf->reportval = 0;
	xdf->mode = mode;
	xdf->ftype = type;
	xdf->fd = fd;
	xdf->buff = xdf->backbuff = NULL;
	xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;
	xdf->channels = NULL;
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->array_stride = NULL;
}

/* \param type		expected type for the file to be opened
 * \param filename	path of the file to be read
 *
 * Create a xdf structure of a xDF file for reading. if type is not XDF_ANY and
 * file is not of the same type, the function will fail.
 */
static struct xdf* create_read_xdf(enum xdffiletype type, const char* filename)
{
	unsigned char magickey[8];
	enum xdffiletype gtype;
	struct xdf* xdf = NULL;
	int errnum = 0;
	int fd = -1;

	// Open the file
	if ((fd = open(filename, O_RDONLY)) == -1) 
		return NULL;

	// Guess file type
	if ( (read(fd, magickey, sizeof(magickey)) == -1)
	    || (lseek(fd, 0, SEEK_SET) == -1) ) {
	    	errnum = errno;
		goto error;
	}
	gtype = xdf_guess_filetype(magickey);
	if ((gtype == XDF_ANY) || ((type != XDF_ANY)&&(type != gtype))) {
		errnum = EMEDIUMTYPE;
		goto error;
	}
	
	// Allocate structure
	if (!(xdf = xdf_alloc_file(gtype))) {
		errnum = errno;
		goto error;
	}
	
	// Initialize by reading the file
	init_xdf_struct(xdf, fd, gtype, XDF_READ);
	if (xdf->ops->read_header(xdf) == 0)
		return xdf;
	
	// We have caught an error if we reach here
	errnum = errno;
	xdf_close(xdf);
	errno = errnum;
	return NULL;

error:
	close(fd);
	errno = errnum;
	return NULL;
}


/* \param type		requested type for the file to be created
 * \param filename	path of the file to be written
 *
 * Create a xdf structure of a xDF file for writing. If type is XDF_ANY,
 * the function will fail
 */
static struct xdf* create_write_xdf(enum xdffiletype type, const char* filename)
{
	struct xdf* xdf = NULL;
	int fd = -1, errnum;
	mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;

	// Create the file
	if ( ((fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, mode)) == -1) 
	    || !(xdf = xdf_alloc_file(type)) ) {
		if (fd == -1) {
			errnum = errno;
			close(fd);
			errno = errnum;
		}
		return NULL;
	}

	init_xdf_struct(xdf, fd, type, XDF_WRITE);
	return xdf;
}


/* \param filename	path of the file to be written
 * \param mode		read or write
 * \param type		expected/requested type
 *
 * API FUNCTION
 * Create a xdf structure of a xDF file for writing or reading depending on
 * the mode. See the manpage for details
 */
struct xdf* xdf_open(const char* filename, int mode, enum xdffiletype type)
{
	struct xdf* xdf = NULL;

	// Argument validation
	if (((mode != XDF_WRITE)&&(mode != XDF_READ)) || !filename) {
		errno = EINVAL;
		return NULL;
	}

	// Structure creation 
	if (mode == XDF_READ)
		xdf = create_read_xdf(type, filename);
	else
		xdf = create_write_xdf(type, filename);

	return xdf;
}


/******************************************************
 *         Channel configuration functions            *
 ******************************************************/
/* \param xdf	pointer to a valid xdf structure
 * 
 * Allocate a channel
 * Return the pointer a new channel or NULL in case of failure
 */
struct xdfch* xdf_alloc_channel(struct xdf* owner)
{
	struct xdfch* ch;

	ch = owner->ops->alloc_channel();
	if (ch)
		ch->owner = owner;

	return ch;
}

/* \param xdf	pointer to a valid xdf structure
 * \param index	index of the requested channel
 *
 * API FUNCTION
 * Returns a pointer to the index-th channel of the xdf file.
 * Returns NULL in case of failure.
 */
struct xdfch* xdf_get_channel(const struct xdf* xdf, unsigned int index)
{
	struct xdfch* ch = xdf->channels;
	unsigned int ich = 0;

	if ((xdf == NULL) || (index >= xdf->numch)) {
		errno = EINVAL;
		return NULL;
	}

	while (ch && (ich<index)) {
		ich++;
		ch = ch->next;
	}

	return ch;
}


/* \param xdf	pointer to a valid xdf structure opened for writing
 *
 * API FUNCTION
 * Add a channel to xdf file. It is initialized with the last added channel
 * but its offset will correspond to neighbour of the last channel
 * Returns NULL in case of failure.
 */
struct xdfch* xdf_add_channel(struct xdf* xdf)
{
	if ((xdf == NULL) || (xdf->mode != XDF_WRITE)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return NULL;
	}

	struct xdfch** curr = &(xdf->channels);
	struct xdfch *ch, *prev = NULL;

	// go to the end of the list of channel of the xdffile
	while (*curr) {
		prev = *curr;
		curr = &((*curr)->next);
	}

	ch = xdf_alloc_channel(xdf);
	if (!ch)
		return NULL;

	// Init the new channel with the previous one
	if (prev) {
		xdf_copy_chconf(ch, prev);
		ch->offset += xdf_get_datasize(ch->inmemtype);
	}

	// Link the channel to the end
	ch->next = NULL;
	*curr = ch;
	xdf->numch++;

	return ch;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be changed
 * \param val	union containing the value
 *
 * Default set channel configuration handling functions.
 * Returns 1 if the type is not handled in that function, -1 in case of
 * error, 0 otherwise
 */
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
	else if (field == XDF_CHFIELD_ARRAY_INDEX) {
		if ((val.i < 0) && (ch->owner->mode == XDF_WRITE)) 
			retval = xdf_set_error(EPERM);
		else
			ch->iarray = val.i;
	}
	else if (field == XDF_CHFIELD_ARRAY_OFFSET)
		ch->offset = val.i;
	else if (field == XDF_CHFIELD_ARRAY_TYPE)
		ch->inmemtype = val.i;
	else if (field == XDF_CHFIELD_ARRAY_DIGITAL)
		ch->digital_inmem = val.i;
	else if (field == XDF_CHFIELD_STORED_TYPE)
		ch->infiletype = val.i;
	else
		retval = 1;
	
	return retval;
}

/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be set
 * \param other	list of couple (field val) terminated by XDF_CHFIELD_NONE
 *
 * API FUNCTION
 * Set the configuration of a channel according to a list of couple of
 * (enum xdfchfield, value pointer) that should be terminated by
 * XDF_CHFIELD_NONE.
 *
 * Example:
 *    xdf_set_chconf(ch, XDF_CHFIELD_DIGITAL_MIN, min,
 *                       XDF_CHFIELD_DIGITAL_MAX, max,
 *                       XDF_CHFIELD_NONE);
 */
int xdf_set_chconf(struct xdfch* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int rval, argtype, retval = 0;
	union optval val;

	if (ch == NULL)
		return xdf_set_error(EINVAL);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		argtype = get_ch_opt_type(field);

		// Assign the correct value type given the field
		if (argtype == TYPE_INT)
			val.i = va_arg(ap, int);
		else if (argtype == TYPE_DATATYPE)
			val.type = va_arg(ap, enum xdftype);
		else if (argtype == TYPE_STRING)
			val.str = va_arg(ap, const char*);
		else if (argtype == TYPE_DOUBLE)
			val.d = va_arg(ap, double);
		else {
			retval = xdf_set_error(EINVAL);
			break;
		}
		
		// Set the field value
		rval = default_set_chconf(ch, field, val);
		rval = ch->owner->ops->set_channel(ch, field, val, rval);
		if (rval) {
			if (rval > 0)
				errno = EINVAL;
		    	retval = -1;
			break;
		}

		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be get
 * \param val	pointer to an union containing the value
 *
 * Default get channel configuration handling function.
 * Returns 1 if the type is not handled in that function, -1 if an error
 * occured, 0 otherwise
 * 
 */
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
		retval = 1;
	
	return retval;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be get
 * \param other	list of couple (field val) terminated by XDF_CHFIELD_NONE
 *
 * API FUNCTION
 * Get the configuration of a channel according to a list of couple of
 * (enum xdfchfield, value pointer) that should be terminated by
 * XDF_CHFIELD_NONE.
 *
 * Example:
 *    xdf_get_chconf(ch, XDF_CHFIELD_DIGITAL_MIN, &min,
 *                       XDF_CHFIELD_DIGITAL_MAX, &max,
 *                       XDF_CHFIELD_NONE);
 */
int xdf_get_chconf(const struct xdfch* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int rval, argtype, retval = 0;
	union optval val;

	if (ch == NULL)
		return xdf_set_error(EFAULT);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		// Get the field value
		rval = default_get_chconf(ch, field, &val);
		rval = ch->owner->ops->get_channel(ch, field, &val, rval);
		if (rval) {
			if (rval > 0)
				errno = EINVAL;
			retval = -1;
			break;
		}

		// Assign to correct value type to the provided pointer
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
			retval = xdf_set_error(EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdfchfield);
	}
	va_end(ap);
	
	return retval;
}


/* \param dst	pointer to the destination xdf channel 
 * \param src	pointer to the source xdf channel
 *
 * API FUNCTION
 * Copy the configuration of a channel
 */
int xdf_copy_chconf(struct xdfch* dst, const struct xdfch* src)
{
	
	if (!dst || !src)
		return xdf_set_error(EINVAL);

	return dst->owner->ops->copy_chconf(dst, src);
}


/******************************************************
 *         xDF general configuration functions        *
 ******************************************************/

/* \param xdf	pointer to xdf file
 * \param field	identifier of the field to be changed
 * \param val	union containing the value
 *
 * Default set general configuration handling function.
 * Returns 1 if the type is not handled in that function, -1 in case of
 * error and 0 otherwise.
 */
static int default_set_conf(struct xdf* xdf, enum xdffield field, union optval val)
{
	int retval = 0;

	if (field == XDF_FIELD_NSAMPLE_PER_RECORD)
		xdf->ns_per_rec = val.i;
	else if (field == XDF_FIELD_SAMPLING_FREQ) 
		xdf->ns_per_rec = xdf->rec_duration*(double)(val.i);
	else if (field == XDF_FIELD_RECORD_DURATION) 
		xdf->rec_duration = val.d;
	else if (field == XDF_FIELD_NCHANNEL) 
		retval = xdf_set_error(EINVAL);
	else
		retval = 1;
	
	if ((retval == 0) && (xdf->mode != XDF_WRITE))
		retval = xdf_set_error(EPERM);
	
	return retval;
}


/* \param xdf	pointer to a xdf file
 * \param field	identifier of the field to be set
 * \param other	list of couple (field val) terminated by XDF_FIELD_NONE
 *
 * API_FUNCTION
 * set the configuration of a xDF file according to a list of couple of
 * (enum xdfchfield, value pointer) that should be terminated by
 * XDF_FIELD_NONE.
 *
 * Example:
 *    xdf_set_conf(xdf, XDF_FIELD_NSAMPLE_PER_RECORD, ns,
 *                      XDF_FIELD_RECORD_DURATION, time,
 *                      XDF_FIELD_NONE);
 */
int xdf_set_conf(struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int rval, argtype, retval = 0;
	union optval val;

	if (xdf == NULL)
		return xdf_set_error(EINVAL);

	va_start(ap, field);
	while (field != XDF_FIELD_NONE) {
		argtype = get_conf_opt_type(field);

		// Assign the correct value type given the field
		if (argtype == TYPE_INT)
			val.i = va_arg(ap, int);
		else if (argtype == TYPE_DATATYPE)
			val.type = va_arg(ap, enum xdftype);
		else if (argtype == TYPE_STRING)
			val.str = va_arg(ap, const char*);
		else if (argtype == TYPE_DOUBLE)
			val.d = va_arg(ap, double);
		else {
			retval = xdf_set_error(EINVAL);
			break;
		}
		
		// Set the field value
		rval = default_set_conf(xdf, field, val);
		rval = xdf->ops->set_conf(xdf, field, val, rval);
		if (rval) {
			if (rval > 0)
				errno = EINVAL;
			retval = -1;
			break;
		}

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


/* \param xdf	pointer to xdf file
 * \param field	identifier of the field to be get
 * \param val	union containing the value
 *
 * Default get general configuration handling function.
 * Returns 1 if the type is not handled in that function, -1 if an error
 * occured, 0 otherwise
 */
static int default_get_conf(const struct xdf* xdf, enum xdffield field, union optval *val)
{
	int retval = 0;

	if (field == XDF_FIELD_NSAMPLE_PER_RECORD)
		val->i = xdf->ns_per_rec;
	else if (field == XDF_FIELD_SAMPLING_FREQ) 
		val->i = ((double)(xdf->ns_per_rec))/xdf->rec_duration;
	else if (field == XDF_FIELD_RECORD_DURATION)
		val->d = xdf->rec_duration;
	else if (field == XDF_FIELD_NCHANNEL)
		val->i = xdf->numch;
	else
		retval = 1;
	
	return retval;
}


/* \param xdf	pointer to a xdf file
 * \param field	identifier of the field to be get
 * \param other	list of couple (field val) terminated by XDF_FIELD_NONE
 *
 * API_FUNCTION
 * Get the configuration of a xDF file according to a list of couple of
 * (enum xdfchfield, value pointer) that should be terminated by
 * XDF_FIELD_NONE.
 *
 * Example:
 *    xdf_get_conf(xdf, XDF_FIELD_NSAMPLE_PER_RECORD, &ns,
 *                      XDF_FIELD_RECORD_DURATION, &time,
 *                      XDF_FIELD_NONE);
 */
int xdf_get_conf(const struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int rval, argtype, retval = 0;
	union optval val;

	if (xdf == NULL)
		return xdf_set_error(EFAULT);

	va_start(ap, field);
	while (field != XDF_FIELD_NONE) {
		// Get the field value
		rval = default_get_conf(xdf, field, &val);
		rval = xdf->ops->get_conf(xdf, field, &val, rval);
		if (rval) {
			if (rval > 0)
				errno = EINVAL;
			retval = -1;
			break;
		}

		// Assign to correct value type to the provided pointer
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
			retval = xdf_set_error(EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


/* \param dst	pointer to the destination xdf file 
 * \param src	pointer to the source xdf file
 *
 * API FUNCTION
 * Copy the configuration of a xDF file
 */
int xdf_copy_conf(struct xdf* dst, const struct xdf* src)
{
	if (!dst || !src)
		return xdf_set_error(EINVAL);

	return dst->ops->copy_conf(dst, src);
}

