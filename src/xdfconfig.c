/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Copyright (C) 2013  Nicolas Bourdaud

    Authors:
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <binary-io.h>
#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"
#include "xdfevent.h"

/******************************************************
 *             options table definitions              *
 ******************************************************/
struct opt_detail
{
	int field;
	unsigned int type;
};

static const struct opt_detail field_table[] = {
	/* File fields */
	{XDF_F_REC_DURATION, TYPE_DOUBLE},
	{XDF_F_REC_NSAMPLE, TYPE_INT},
	{XDF_F_SAMPLING_FREQ, TYPE_INT},
	{XDF_F_NCHANNEL, TYPE_INT},
	{XDF_F_FILEFMT, TYPE_INT},
	{XDF_F_NEVTTYPE, TYPE_INT},
	{XDF_F_NEVENT, TYPE_INT},
	{XDF_F_NREC, TYPE_INT},
	{XDF_F_SUBJ_DESC, TYPE_STRING},
	{XDF_F_SESS_DESC, TYPE_STRING},
	{XDF_F_RECTIME, TYPE_DOUBLE},
	{XDF_F_BIRTHDAY, TYPE_DOUBLE},
	{XDF_F_ADDICTION, TYPE_UINT},
	{XDF_F_HEIGHT, TYPE_DOUBLE},
	{XDF_F_WEIGHT, TYPE_DOUBLE},
	{XDF_F_GENDER, TYPE_UINT},
	{XDF_F_HANDNESS, TYPE_UINT},
	{XDF_F_VISUAL_IMP, TYPE_UINT},
	{XDF_F_LOCATION, TYPE_3DPOS},
	{XDF_F_ICD_CLASS, TYPE_ICD},
	{XDF_F_HEADSIZE, TYPE_3DPOS},
	{XDF_F_REF_POS, TYPE_3DPOS},
	{XDF_F_GND_POS, TYPE_3DPOS},
	/* Channel field */
	{XDF_CF_ARRINDEX, TYPE_INT},
	{XDF_CF_ARROFFSET, TYPE_INT},
	{XDF_CF_ARRDIGITAL, TYPE_INT},
	{XDF_CF_ARRTYPE, TYPE_DATATYPE},
	{XDF_CF_STOTYPE, TYPE_DATATYPE},
	{XDF_CF_LABEL, TYPE_STRING},
	{XDF_CF_PMIN, TYPE_DOUBLE},
	{XDF_CF_PMAX, TYPE_DOUBLE}, 
	{XDF_CF_DMIN, TYPE_DOUBLE},	
	{XDF_CF_DMAX, TYPE_DOUBLE},
	{XDF_CF_UNIT, TYPE_STRING},
	{XDF_CF_TRANSDUCTER, TYPE_STRING},
	{XDF_CF_PREFILTERING, TYPE_STRING},
	{XDF_CF_RESERVED, TYPE_STRING},
	{XDF_CF_ELECPOS, TYPE_3DPOS},
	{XDF_CF_IMPEDANCE, TYPE_DOUBLE}
};
#define num_opts	(sizeof(field_table)/sizeof(field_table[0]))

static int get_field_type(int field)
{
	int i = num_opts-1;
	do {
		if (field_table[i].field == field)
			return field_table[i].type;
	} while (i--);
	return -1;
}


static int set_arg_to_val(int field, va_list* ap, union optval* val)
{
	int argtype = get_field_type(field);

	if (argtype == TYPE_INT)
		val->i = va_arg(*ap, int);
	else if (argtype == TYPE_DATATYPE)
		val->type = va_arg(*ap, enum xdftype);
	else if (argtype == TYPE_STRING)
		val->str = va_arg(*ap, const char*);
	else if (argtype == TYPE_DOUBLE)
		val->d = va_arg(*ap, double);
	else if (argtype == TYPE_UINT)
		val->ui = va_arg(*ap, unsigned int);
	else if (argtype == TYPE_3DPOS) 
		memcpy(val->pos, va_arg(*ap, double*), sizeof(val->pos));
	else if (argtype == TYPE_ICD) 
		memcpy(val->icd, va_arg(*ap, char*), sizeof(val->icd));
	else
		return -1;

	return 0;
}


static int set_val_to_arg(int field, union optval val, va_list* ap)
{
	int argtype = get_field_type(field);

	if (argtype == TYPE_INT)
		*(va_arg(*ap, int*)) = val.i;
	else if (argtype == TYPE_DATATYPE)
		*(va_arg(*ap, enum xdftype*)) = val.type;
	else if (argtype == TYPE_STRING)
		*(va_arg(*ap, const char**)) = val.str;
	else if (argtype == TYPE_DOUBLE)
		*(va_arg(*ap, double*)) = val.d;
	else if (argtype == TYPE_UINT)
		*(va_arg(*ap, unsigned int*)) = val.ui;
	else if (argtype == TYPE_3DPOS) 
		memcpy(va_arg(*ap, double*), val.pos, sizeof(val.pos));
	else if (argtype == TYPE_ICD) 
		memcpy(va_arg(*ap, char*), val.icd, sizeof(val.icd));
	else
		return -1;

	return 0;
}

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
static void init_xdf_struct(struct xdf* xdf, int fd, int mode)
{
	struct xdfch* ch = xdf->defaultch;
	const double* lim;

	xdf->ready = 0;
	xdf->reportval = 0;
	xdf->mode = mode;
	xdf->fd = fd;
	xdf->buff = xdf->backbuff = NULL;
	xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;
	xdf->channels = NULL;
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->array_stride = NULL;
	xdf->closefd_ondestroy = 0;

	// Set default values for the default channel 
	ch->inmemtype = ch->infiletype;
	lim = xdf_datinfo(ch->infiletype)->lim;
	memcpy(ch->digital_mm, lim, sizeof(ch->digital_mm));
	memcpy(ch->physical_mm, lim, sizeof(ch->physical_mm));
	ch->digital_inmem = 0;
	ch->iarray = 0;
	ch->offset = 0;
}


/* \param xdf	pointer to an structure xdf initialized for reading
 * \param fd	file descriptor of the opened file for reading
 *
 * Initialize the metadata, i.e. it initializes the xdf structure, read the
 * file header and it sets the initial values of transfer to reasonable
 * defaults: no scaling + iarray to 0
 *
 * Return 0 in case of success, -1 otherwise
 */
static int setup_read_xdf(struct xdf* xdf, int fd)
{
	struct xdfch* ch;
	int offset = 0;

	init_xdf_struct(xdf, fd, XDF_READ);
	if (xdf->ops->read_header(xdf))
		return -1;

	// Set channel default values
	for (ch = xdf->channels; ch != NULL; ch = ch->next) {
		ch->inmemtype = ch->infiletype;
		ch->digital_inmem = 1;
		ch->iarray = 0;
		ch->offset = offset;
		offset += xdf_get_datasize(ch->inmemtype);
	}

	return 0;
}


/* \param type		expected type for the file to be opened
 * \param fd		File descriptor of the storage
 *
 * Create a xdf structure of a xDF file for reading. if type is not XDF_ANY
 * and file is not of the same type, the function will fail.
 */
static
struct xdf* create_read_xdf(enum xdffiletype type, int fd)
{
	unsigned char magickey[8];
	enum xdffiletype gtype;
	struct xdf* xdf = NULL;
	int errnum = 0;

	// Guess file type
	if ( (read(fd, magickey, sizeof(magickey)) == -1)
	    || (lseek(fd, 0, SEEK_SET) == -1) )
		return NULL;

	gtype = xdf_guess_filetype(magickey);
	if ((gtype == XDF_ANY) || ((type != XDF_ANY)&&(type != gtype))) {
		errno = EILSEQ;
		return NULL;
	}
	
	// Allocate structure
	if (!(xdf = xdf_alloc_file(gtype)))
		return NULL;
	
	// Initialize by reading the file
	if (setup_read_xdf(xdf, fd) == 0)
		return xdf;
	
	// We have caught an error if we reach here
	errnum = errno;
	xdf_close(xdf);
	errno = errnum;
	return NULL;
}


/* \param type		requested type for the file to be created
 * \param fd		File descriptor of the storage
 *
 * Create a xdf structure of a xDF file for writing. If type is XDF_ANY,
 * the function will fail
 */
static
struct xdf* create_write_xdf(enum xdffiletype type, int fd)
{
	struct xdf* xdf = NULL;

	xdf = xdf_alloc_file(type);
	if (xdf == NULL)
		return NULL;

	init_xdf_struct(xdf, fd, XDF_WRITE);
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
API_EXPORTED
struct xdf* xdf_open(const char* filename, int mode, enum xdffiletype type)
{
	int fd, oflag;
	struct xdf* xdf = NULL;
	mode_t perm = S_IRUSR|S_IWUSR;

	// Argument validation
	if (((mode != XDF_WRITE)&&(mode != XDF_READ)) || !filename) {
		errno = EINVAL;
		return NULL;
	}

	// Create the file
	oflag = (mode == XDF_READ) ? O_RDONLY : (O_WRONLY|O_CREAT|O_EXCL);
	oflag |= O_BINARY | O_CLOEXEC;
	fd = open(filename, oflag, perm);
	if (fd == -1)
		return NULL;

	// Structure creation 
	if (mode == XDF_READ)
		xdf = create_read_xdf(type, fd);
	else
		xdf = create_write_xdf(type, fd);

	if (!xdf)
		close(fd);
	xdf->closefd_ondestroy = 1;
	return xdf;
}


/* \param fd		file descriptor of the storage
 * \param mode		read or write
 * \param type		expected/requested type
 *
 * API FUNCTION
 * Create a xdf structure of a xDF file for writing or reading depending on
 * the mode. See the manpage for details
 */
API_EXPORTED
struct xdf* xdf_fdopen(int fd, int mode, enum xdffiletype type)
{
	struct xdf* xdf = NULL;
	int oflags, invalmode, closefd;

	closefd = mode & XDF_CLOSEFD;
	mode &= ~XDF_CLOSEFD;

	// Argument validation
	if (((mode != XDF_WRITE) && (mode != XDF_READ))) {
		errno = EINVAL;
		return NULL;
	}

#ifdef F_GETFL
	// validation of the file descriptor
	oflags = fcntl(fd, F_GETFL);
	invalmode = (mode == XDF_READ) ? O_WRONLY : O_RDONLY;
	if (oflags == -1 || (oflags & O_ACCMODE) == invalmode) {
		errno = EBADF;
		return NULL;
	}
#endif

	// Structure creation
	if (mode == XDF_READ)
		xdf = create_read_xdf(type, fd);
	else
		xdf = create_write_xdf(type, fd);

	if (xdf)
		xdf->closefd_ondestroy = closefd;
	return xdf;
}


/******************************************************
 *         Channel configuration functions            *
 ******************************************************/
/* \param xdf	pointer to a valid xdf structure
 * 
 * Allocate a channel, initialize it with default values and link it to
 * the end of channel list
 *
 * Return the pointer a new channel or NULL in case of failure
 */
LOCAL_FN struct xdfch* xdf_alloc_channel(struct xdf* xdf)
{
	const struct format_operations* ops = xdf->ops;
	struct xdfch *ch, **plastch;
	char* data;

	if ((data = malloc(ops->chlen)) == NULL)
		return NULL;
	
	// Initialize the channel
	memcpy(data, (char*)xdf->defaultch - ops->choff, ops->chlen);
	ch = (struct xdfch*)(data + ops->choff);
	ch->owner = xdf;

	// Link the channel to the end of the list
	ch->next = NULL;
	plastch = &(xdf->channels);
	while (*plastch)
		plastch = &((*plastch)->next);
	*plastch = ch;

	xdf->defaultch->offset += xdf_get_datasize(ch->inmemtype);

	return ch;
}

/* \param xdf	pointer to a valid xdf structure
 * \param index	index of the requested channel
 *
 * API FUNCTION
 * Returns a pointer to the index-th channel of the xdf file.
 * Returns NULL in case of failure.
 */
API_EXPORTED struct xdfch* xdf_get_channel(const struct xdf* xdf, unsigned int index)
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
 * \param label	string holding the label of the channel (can be NULL)
 *
 * API FUNCTION
 * Add a channel to xdf file. It is initialized with the last added channel
 * but its offset will correspond to neighbour of the last channel
 * Returns NULL in case of failure.
 */
API_EXPORTED struct xdfch* xdf_add_channel(struct xdf* xdf, const char* label)
{
	struct xdfch *ch;

	if ((xdf == NULL) || (xdf->mode != XDF_WRITE)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return NULL;
	}

	ch = xdf_alloc_channel(xdf);
	if (!ch)
		return NULL;
	xdf->numch++;

	if (label)
		xdf_set_chconf(ch, XDF_CF_LABEL, label, XDF_NOF);

	return ch;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be changed
 * \param val	union containing the value
 *
 * Set channel configuration handling function. First run the default
 * handler, then call the file format method.
 * Returns 1 if the type is not handled in that function, -1 in case of
 * error, 0 otherwise
 */
static int proceed_set_chconf(struct xdfch* ch, enum xdffield field,
                              union optval val)
{
	int retval = 0;

	// Default handler
	if (field == XDF_CF_DMIN) {
		if (xdf_datinfo(ch->infiletype)->lim[0] > val.d)
			retval = xdf_set_error(EDOM);
		else
			ch->digital_mm[0] = val.d;
	}
	else if (field == XDF_CF_DMAX) {
		if (xdf_datinfo(ch->infiletype)->lim[1] < val.d)
			retval = xdf_set_error(EDOM);
		else
			ch->digital_mm[1] = val.d;
	} 
	else if (field == XDF_CF_PMIN) {
		if (!ch->digital_inmem && 
		    (xdf_datinfo(ch->inmemtype)->lim[0] > val.d))
			retval = xdf_set_error(EDOM);
		else
			ch->physical_mm[0] = val.d;
	}
	else if (field == XDF_CF_PMAX) {
		if (!ch->digital_inmem && 
		    (xdf_datinfo(ch->inmemtype)->lim[1] < val.d))
			retval = xdf_set_error(EDOM);
		else
			ch->physical_mm[1] = val.d;
	}
	else if (field == XDF_CF_ARRINDEX) {
		if ((val.i < 0) && (ch->owner->mode == XDF_WRITE)) 
			retval = xdf_set_error(EPERM);
		else
			ch->iarray = val.i;
	}
	else if (field == XDF_CF_ARROFFSET)
		ch->offset = val.i;
	else if (field == XDF_CF_ARRTYPE)
		ch->inmemtype = val.i;
	else if (field == XDF_CF_ARRDIGITAL)
		ch->digital_inmem = val.i;
	else if (field == XDF_CF_STOTYPE) {
		if (ch->owner->ops->supported_type[val.i]) {
			ch->infiletype = val.i;
			memcpy(ch->digital_mm, xdf_datinfo(val.i)->lim,
					sizeof(ch->digital_mm));
		} else
			retval = xdf_set_error(EPERM);
	}
	else
		retval = 1;

	// File format specific handler
	retval = ch->owner->ops->set_channel(ch, field, val, retval);
	if (retval > 0) {
		errno = EINVAL;
	    	retval = -1;
	}
	
	return retval;
}

/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be set
 * \param other	list of couple (field val) terminated by XDF_NOF
 *
 * API FUNCTION
 * Set the configuration of a channel according to a list of couple of
 * (enum xdffield, value pointer) that should be terminated by
 * XDF_NOF.
 *
 * Example:
 *    xdf_set_chconf(ch, XDF_CF_DMIN, min,
 *                       XDF_CF_DMAX, max,
 *                       XDF_NOF);
 */
API_EXPORTED int xdf_set_chconf(struct xdfch* ch, enum xdffield field, ...)
{
	va_list ap;
	int retval = 0;
	union optval val;

	if (ch == NULL)
		return xdf_set_error(EINVAL);

	va_start(ap, field);
	while (field != XDF_NOF) {
		if (field < XDF_CF_FIRST) {
			retval = xdf_set_error(EINVAL);
			break;
		}

		// Assign the correct value type given the field
		if (set_arg_to_val(field, &ap, &val)) {
			retval = xdf_set_error(EINVAL);
			break;
		}
		
		// Set the field value
		retval = proceed_set_chconf(ch, field, val);
		if (retval)
			break;

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be get
 * \param val	pointer to an union containing the value
 *
 * Get channel configuration handling function. First run the default
 * handler, then call the file format method.
 * Returns 1 if the type is not handled in that function, -1 if an error
 * occured, 0 otherwise
 * 
 */
static int proceed_get_chconf(const struct xdfch* ch, enum xdffield
field, union optval* val)
{
	int retval = 0;

	// Default handler
	if (field == XDF_CF_DMIN)
		val->d = ch->digital_mm[0];
	else if (field == XDF_CF_DMAX)
		val->d = ch->digital_mm[1];
	else if (field == XDF_CF_PMIN)
		val->d = ch->physical_mm[0];
	else if (field == XDF_CF_PMAX)
		val->d = ch->physical_mm[1];
	else if (field == XDF_CF_ARRINDEX)
		val->i = ch->iarray;
	else if (field == XDF_CF_ARROFFSET)
		val->i = ch->offset;
	else if (field == XDF_CF_ARRDIGITAL)
		val->i = ch->digital_inmem;
	else if (field == XDF_CF_ARRTYPE)
		val->type = ch->inmemtype;
	else if (field == XDF_CF_STOTYPE)
		val->type = ch->infiletype;
	else
		retval = 1;
	
	// File format specific handler
	retval = ch->owner->ops->get_channel(ch, field, val, retval);
	if (retval > 0) {
		errno = EINVAL;
	    	retval = -1;
	}

	return retval;
}


/* \param ch	pointer to a channel of a xdf file
 * \param field	identifier of the field to be get
 * \param other	list of couple (field val) terminated by XDF_NOF
 *
 * API FUNCTION
 * Get the configuration of a channel according to a list of couple of
 * (enum xdffield, value pointer) that should be terminated by
 * XDF_NOF.
 *
 * Example:
 *    xdf_get_chconf(ch, XDF_CF_DMIN, &min,
 *                       XDF_CF_DMAX, &max,
 *                       XDF_NOF);
 */
API_EXPORTED
int xdf_get_chconf(const struct xdfch* ch, enum xdffield field, ...)
{
	va_list ap;
	int retval = 0;
	union optval val;

	if (ch == NULL)
		return xdf_set_error(EFAULT);

	va_start(ap, field);
	while (field != XDF_NOF) {
		// Get the field value
		if (field >= XDF_CF_FIRST) 
			retval = proceed_get_chconf(ch, field, &val);
		else
			retval = xdf_set_error(EINVAL);
		if (retval) 
			break;

		// Assign to correct value type to the provided pointer
		if (set_val_to_arg(field, val, &ap)) {
			retval = xdf_set_error(EINVAL);
			break;
		}

		field  = va_arg(ap, enum xdffield);
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
API_EXPORTED
int xdf_copy_chconf(struct xdfch* dst, const struct xdfch* src)
{
	if (!dst || !src)
		return xdf_set_error(EINVAL);
	
	const struct format_operations* ops = dst->owner->ops;
	const enum xdffield* req;
	union optval val;
	int errnum;

	// Use fast copy if channel come from the same type
	if (src->owner->ops->type == dst->owner->ops->type) {
		struct xdfch* next = dst->next;
		struct xdf* owner = dst->owner;
		memcpy(((char*)dst) - ops->choff, 
		       ((const char*)src) - ops->choff,
		       ops->chlen);
		dst->owner = owner;
		dst->next = next;

		return 0;
	}

	errnum = errno; // copy_chconf is not allowed to fail
	for (req = ops->chfields; *req != XDF_NOF; req++) {
		if (proceed_get_chconf(src, *req, &val))
			continue;

		if (*req == XDF_CF_STOTYPE)
			val.i = get_closest_type(val.i, ops->supported_type);

		proceed_set_chconf(dst, *req, val);
	}
	errno = errnum;

	return 0;
}


/******************************************************
 *         xDF general configuration functions        *
 ******************************************************/

/* \param xdf	pointer to xdf file
 * \param field	identifier of the field to be changed
 * \param val	union containing the value
 *
 * Set general configuration handling function. First run the default
 * handler, then call the file format method.
 * Returns 1 if the type is not handled in that function, -1 in case of
 * error and 0 otherwise.
 */
static int proceed_set_conf(struct xdf* xdf, enum xdffield field, union optval val)
{
	int retval = 0;

	if (xdf->mode != XDF_WRITE)
		return xdf_set_error(EPERM);

	// Default handler
	if (field == XDF_F_REC_NSAMPLE)
		xdf->ns_per_rec = val.i;
	else if (field == XDF_F_SAMPLING_FREQ) 
		xdf->ns_per_rec = xdf->rec_duration*(double)(val.i);
	else if (field == XDF_F_REC_DURATION) 
		xdf->rec_duration = val.d;
	else
		retval = 1;
	
	// File format specific handler
	retval = xdf->ops->set_conf(xdf, field, val, retval);
	if (retval > 0) {
		errno = EINVAL;
		retval = -1;
	}
	
	return retval;
}


/* \param xdf	pointer to a xdf file
 * \param field	identifier of the field to be set
 * \param other	list of couple (field val) terminated by XDF_NOF
 *
 * API_FUNCTION
 * set the configuration of a xDF file according to a list of couple of
 * (enum xdffield, value pointer) that should be terminated by
 * XDF_NOF.
 *
 * Example:
 *    xdf_set_conf(xdf, XDF_F_REC_NSAMPLE, ns,
 *                      XDF_F_REC_DURATION, time,
 *                      XDF_NOF);
 */
API_EXPORTED
int xdf_set_conf(struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int retval = 0;
	union optval val;
	struct xdfch* defch;

	if (xdf == NULL)
		return xdf_set_error(EINVAL);
	defch = xdf->defaultch;

	va_start(ap, field);
	while (field != XDF_NOF) {
		// Assign the correct value type given the field
		if (set_arg_to_val(field, &ap, &val)) {
			retval = xdf_set_error(EINVAL);
			break;
		}
		
		// Set the field value
		if (field < XDF_CF_FIRST)
			retval = proceed_set_conf(xdf, field, val);
		else
			retval = proceed_set_chconf(defch, field, val);
		if (retval) 
			break;

		field  = va_arg(ap, enum xdffield);
	}
	va_end(ap);
	
	return retval;
}


/* \param xdf	pointer to xdf file
 * \param field	identifier of the field to be get
 * \param val	union containing the value
 *
 * Get general configuration handling function. First run the default
 * handler, then call the file format method.
 * Returns 1 if the type is not handled in that function, -1 if an error
 * occured, 0 otherwise
 */
static int proceed_get_conf(const struct xdf* xdf, enum xdffield field, union optval *val)
{
	int retval = 0;

	// Default handler
	if (field == XDF_F_REC_NSAMPLE)
		val->i = xdf->ns_per_rec;
	else if (field == XDF_F_SAMPLING_FREQ) 
		val->i = ((double)(xdf->ns_per_rec))/xdf->rec_duration;
	else if (field == XDF_F_REC_DURATION)
		val->d = xdf->rec_duration;
	else if (field == XDF_F_NCHANNEL)
		val->i = xdf->numch;
	else if (field == XDF_F_FILEFMT)
		val->i = xdf->ops->type;
	else if (field == XDF_F_NEVTTYPE)
		val->i = (xdf->table != NULL) ? xdf->table->nentry : 0;
	else if (field == XDF_F_NEVENT)
		val->i = (xdf->table != NULL) ? xdf->table->nevent : 0;
	else if (field == XDF_F_NREC)
		val->i = xdf->nrecord;
	else
		retval = 1;

	// File format specific handler
	retval = xdf->ops->get_conf(xdf, field, val, retval);
	if (retval > 0) {
		errno = EINVAL;
		retval = -1;
	}
	
	return retval;
}


/* \param xdf	pointer to a xdf file
 * \param field	identifier of the field to be get
 * \param other	list of couple (field val) terminated by XDF_NOF
 *
 * API_FUNCTION
 * Get the configuration of a xDF file according to a list of couple of
 * (enum xdffield, value pointer) that should be terminated by
 * XDF_NOF.
 *
 * Example:
 *    xdf_get_conf(xdf, XDF_F_REC_NSAMPLE, &ns,
 *                      XDF_F_REC_DURATION, &time,
 *                      XDF_NOF);
 */
API_EXPORTED
int xdf_get_conf(const struct xdf* xdf, enum xdffield field, ...)
{
	va_list ap;
	int retval = 0;
	union optval val;
	struct xdfch* defch;

	if (xdf == NULL)
		return xdf_set_error(EFAULT);
	defch = xdf->defaultch;

	va_start(ap, field);
	while (field != XDF_NOF) {
		// Get the field value
		if (field < XDF_CF_FIRST)
			retval = proceed_get_conf(xdf, field, &val);
		else
			retval = proceed_get_chconf(defch, field, &val);
			
		if (retval) 
			break;

		// Assign to correct value type to the provided pointer
		if (set_val_to_arg(field, val, &ap)) {
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
API_EXPORTED
int xdf_copy_conf(struct xdf* dst, const struct xdf* src)
{
	if (!dst || !src)
		return xdf_set_error(EINVAL);

	const struct format_operations* ops = dst->ops;
	const enum xdffield* req;
	union optval val;
	int errnum;

	errnum = errno; // copy_chconf is not allowed to fail
	for (req = ops->filefields; *req != XDF_NOF; req++) {
		if (proceed_get_conf(src, *req, &val))
			continue;

		if (*req == XDF_CF_STOTYPE)
			val.i = get_closest_type(val.i, ops->supported_type);

		proceed_set_conf(dst, *req, val);
	}
	errno = errnum;

	return 0;
}


/* \param xdf	pointer to a xdf structure
 * \param type	target data type
 *
 * API function
 * Returns the data type supported by xdf the closest to type or -1 
 */
API_EXPORTED
int xdf_closest_type(const struct xdf* xdf, enum xdftype type)
{
	if ((xdf == NULL) || (type >= XDF_NUM_DATA_TYPES)) {
		errno = EINVAL;
		return -1;
	}

	return get_closest_type(type, xdf->ops->supported_type);
}


API_EXPORTED 
int xdf_add_evttype(struct xdf* xdf, int code, const char* desc)
{
	int evttype;

	if ((xdf == NULL) || (xdf->table == NULL)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	evttype = add_event_entry(xdf->table, code, desc);
	if (evttype < 0)
		errno = ENOMEM;
	return evttype;
}


API_EXPORTED 
int xdf_get_evttype(struct xdf* xdf, unsigned int evttype,
                    int *code, const char** desc)
{
	struct evententry* entry;

	if ((xdf == NULL) || (code == NULL) || (desc == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if ((xdf->table == NULL) || (evttype >= xdf->table->nentry)) {
		errno = (xdf->table == NULL) ? EPERM : ERANGE;
		return -1;
	}

	entry = xdf->table->entry + evttype;
	*code = entry->code;
	*desc = strlen(entry->label) ? entry->label : NULL;

	return 0;
}


API_EXPORTED 
int xdf_add_event(struct xdf* xdf, int evttype,
                          double onset, double duration)
{
	struct xdfevent evt = {
		.onset = onset,
		.duration = duration,
		.evttype = evttype
	};

	if (xdf == NULL || evttype < 0) {
		errno = EINVAL;
		return -1;
	}

	if ((xdf->table == NULL) || (evttype >= (int)xdf->table->nentry)) {
		errno = (xdf->table == NULL) ? EPERM : EINVAL;
		return -1;
	}

	return add_event(xdf->table, &evt);
}


API_EXPORTED 
int xdf_get_event(struct xdf* xdf, unsigned int index, 
                         unsigned int *evttype, double* start, double* dur)
{
	struct xdfevent* evt;

	if ((xdf==NULL)||(evttype==NULL)||(start==NULL)||(start==NULL)) {
		errno = EINVAL;
		return -1;
	}

	if ((xdf->table == NULL) || (index >= xdf->table->nevent)) {
		errno = (xdf->table == NULL) ? EPERM : ERANGE;
		return -1;
	}

	evt = get_event(xdf->table, index);
	*evttype = evt->evttype;
	*start = evt->onset;
	*dur = evt->duration;
	return 0;
}

