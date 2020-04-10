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
#include <mmsysio.h>

#include "streamops.h"
#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"
#include "xdfevent.h"
#include "common.h"

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
	xdf->tmp_event_fd = -1;
	xdf->tmp_code_fd = -1;
	xdf->buff = xdf->backbuff = NULL;
	xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;
	xdf->channels = NULL;
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->array_stride = NULL;
	xdf->closefd_ondestroy = 0;
	xdf->nrecord = -1;

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
	if ( (mm_read(fd, magickey, sizeof(magickey)) == -1)
	    || (mm_seek(fd, 0, SEEK_SET) == -1) )
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


static
int create_tmp_writefile_with_suffix(struct xdf* xdf, char const * suffix,
                                     int oflag)
{
	size_t filename_len;
	int fd;
	mode_t perm = S_IRUSR|S_IWUSR;

	/* <root>.<suffix>\0 */
	filename_len = strlen(xdf->filename);
	strcat(xdf->filename, suffix);

	fd = mm_open(xdf->filename, oflag, perm);

	xdf->filename[filename_len] = '\0';
	return fd;
}

/* \param type		requested type for the file to be created
 * \param fd		File descriptor of the storage
 * \param filename	filename used to create fd (NULL with xdf_fopen())
 * \param oflag         flag used when opening event file descriptor
 *
 *
 * Create a xdf structure of a xDF file for writing. If type is XDF_ANY,
 * the function will fail
 */
static
struct xdf* create_write_xdf(enum xdffiletype type, int fd,
                             const char * filename, int oflag)
{
	struct xdf* xdf = NULL;

	xdf = xdf_alloc_file(type);
	if (xdf == NULL)
		return NULL;

	init_xdf_struct(xdf, fd, XDF_WRITE);
	if (filename != NULL) {
		/* reserve more than needed to allow re-using this buffer in xdf_close()
		 * without needing to alloc() */
		xdf->filename = malloc(strlen(filename) + 8);
		if (!xdf->filename)
			goto error;
		strcpy(xdf->filename, filename);

		xdf->tmp_event_fd = create_tmp_writefile_with_suffix(xdf, ".event", oflag);
		xdf->tmp_code_fd = create_tmp_writefile_with_suffix(xdf, ".code", oflag);
		if (xdf->tmp_event_fd < 0 || xdf->tmp_code_fd < 0)
			goto error;
	}

	return xdf;

error:
	xdf_close(xdf);
	return NULL;
}


/**
 * xdf_open() - opens a XDF file for reading or writing
 * @filename:	path of the file to be written
 * @mode:       read or write
 * @type:       expected/requested type
 *
 * xdf_open() opens a XDF the file referred by the path @filename for reading
 * or writing.
 *
 * If @mode is XDF_READ, the file is opened for reading. Thus it
 * must exist and @type should be either XDF_ANY or set to the type
 * of the file referred by @type. Otherwise, the function will fail.
 *
 * If @mode is XDF_WRITE, the file is opened for writing. Thus the
 * path @filename must not referred to an existing file: the function will
 * fail if the file exist. This behavior prevents to overwrite any previous
 * recording. XDF_TRUNC flag can be added to disable this behavior and
 * overwrite existing file (this flag is ignored if combined with XDF_READ.
 * @type should be also be set to the desired type of data format
 * (XDF_ANY will result in a error).
 *
 * The possible file type values are defined in the header file <xdfio.h>.
 *
 * Return: an handle to XDF file opened in case of success.
 *         Otherwise, NULL is returned and errno is set appropriately
 *         (In addition to the errors related to calls to open() or
 *         read(), the following errors can occur when calling xdf_open():
 *         EILSEQ if the file that is being opened does not correspond to a
 *         supported file format or is not of the type specified, ENOMEM if
 *         the system is unable to allocate resources, EINVAL if @mode is 
 *         neither XDF_READ nor XDF_WRITE, or if @filename is NULL).
 */
API_EXPORTED
struct xdf* xdf_open(const char* filename, int mode, enum xdffiletype type)
{
	int fd, oflag;
	struct xdf* xdf = NULL;
	mode_t perm = 0666;

	// Argument validation
	if ((mode & ~(XDF_WRITE|XDF_READ|XDF_TRUNC)) || !filename) {
		errno = EINVAL;
		return NULL;
	}

	// Create the file
	oflag = (mode & XDF_READ) ? O_RDONLY : (O_WRONLY|O_CREAT);
	oflag |= (mode & XDF_TRUNC) ? O_TRUNC : O_EXCL;
	fd = mm_open(filename, oflag, perm);
	if (fd == -1)
		return NULL;

	// Structure creation
	mode &= ~XDF_TRUNC;
	if (mode == XDF_READ)
		xdf = create_read_xdf(type, fd);
	else
		xdf = create_write_xdf(type, fd, filename, oflag);

	if (xdf == NULL)
		mm_close(fd);
	else
		xdf->closefd_ondestroy = 1;

	return xdf;
}


/**
 * xdf_fdopen() - opens a XDF file for reading or writing
 * @fd:		file descriptor of the storage
 * @mode:	read or write
 * @type:	expected/requested type
 *
 * xdf_fdopen() is similar to xdf_open() excepting it takes as
 * first argument a file descriptor @fd instead of a filename. The file
 * descriptor must have been opened with flags compatible with the @mode
 * argument or the function will fail.
 *
 * By default, the file descriptor @fd is not closed when
 * xdf_close() is called on the returned XDF structure. However, if
 * @mode is a bitwise-inclusive OR combination of the possible opening
 * mode with the XDF_CLOSEFD flag then the file descriptor @fd will
 * be closed when xdf_close() is called.
 *
 * Return: an handle to XDF file opened in case of success.
 *         Otherwise, NULL is returned and errno is set appropriately
 *         (When calling xdf_fdopen(), all the possible error of xdf_open()
 *         can occur as well as: EBADF if the @fd argument is not a valid
 *         file descriptor or has been opened with flags incompatible with
 *         the @mode argument (O_WRONLY for XDF_READ or O_RDONLY for 
 *         XDF_WRITE).
 */
API_EXPORTED
struct xdf* xdf_fdopen(int fd, int mode, enum xdffiletype type)
{
	struct xdf* xdf = NULL;
	int closefd;

	closefd = mode & XDF_CLOSEFD;
	mode &= ~XDF_CLOSEFD;

	// Argument validation
	if (((mode != XDF_WRITE) && (mode != XDF_READ))) {
		errno = EINVAL;
		return NULL;
	}


#if !defined(_WIN32)

/* TODO: Transform the fcntl function that needs the fcntl.h API which is not
 standard. Therefore the fcntl function is not supported in windows.
 To transform the fcntl function, we need to create a function 'f' that gives
 access to the flags of a file descriptor. The function 'f' should be
 implemented in the mmLib. */
	int oflags, invalmode;

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
		xdf = create_write_xdf(type, fd, NULL, 0);

	if (xdf)
		xdf->closefd_ondestroy = closefd;
	return xdf;
}


/******************************************************
 *         Channel configuration functions            *
 ******************************************************/
/**
 * xdf_alloc_channel() - allocates a channel, initializes it with default
 *                       values and links it to the end of channel list
 * @xdf:	pointer to a valid xdf structure
 *
 * Return: the pointer to the new channel in case of success, NULL otherwise
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

/**
 * xdf_get_channel() - gets the channel descriptor handle of a particular index
 * @xdf:	pointer to a valid xdf structure
 * @index:	index of the requested channel
 *
 * xdf_get_channel() gets the channel descriptor of the channel stored at
 * index @index in the XDF file referenced by the handle @xdf.
 * 
 * Return: the handle to requested channel descriptor in case of success, 
 *         NULL otherwise, and errno is set (EINVAL if @xdf is NULL or if
 *         @index is bigger than the number of channel).
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


/**
 * xdf_add_channel() - appends a channel to a XDF file
 * @xdf:	pointer to a valid xdf structure opened for writing
 * @label:	string holding the label of the channel (can be NULL)
 *
 * xdf_add_channel() appends a channel to the file referenced by the
 * handle @xdf. The new channel is initialized with the @label
 * argument (if not NULL) and with the default channel values set in the
 * XDF file, i.e. those set using channel configuration fields in
 * xdf_set_conf() (See the related manpage).
 *
 * If the call to xdf_add_channel() is successful, the default offset
 * value (the field referenced by XDF_CF_ARROFFSET) is incremented by the
 * size of the current default stored type (field referenced by
 * XDF_CF_STOTYPE). As a consequence, if the channel default values have
 * not changed in-between, the next call to xdf_add_channel() will create
 * a channel whose location is the array will be next to the previous one.
 *
 * This type of initialization allows the user to add channels without having
 * to specifically pack them: this is achieved by default.
 *
 * Return: the handle to newly created channel descriptor in case of success.
 *         Otherwise NULL is returned and errno is set appropriately (EINVAL
 *         if @xdf is NULL, ENOMEM if the system is unable to allocate 
 *         resources, or EPERM if the file referenced by @xdf has been opened
 *         with the mode XDF_READ)
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

/**
 * xdf_set_chconf() - sets the configuration of a channel descriptor handle
 * @ch: 	pointer to a channel of a xdf file
 * @field:	identifier of the field to be set
 *
 * xdf_set_chconf() sets the configuration of the channel referenced
 * by @ch according to the variable list of argument. This list is
 * composed of successive couple grouping one variable of type enum xdffield
 * defining the feature to be set and a value whose type depends on the
 * previous field type. The list MUST finish by XDF_NOF.
 *
 * This function processes the argument list from left to right. This
 * means that if a particular field request provokes an error, none of the
 * field requests on its right will be processed. The order of processing is
 * also important for field requests that influences the value of other fields
 * (like XDF_CF_STOTYPE).
 * 
 * Here is the list of admissible value. The expected type of value is provided
 * in the parenthesis. The default value of each field is provided in
 * squared brackets (however these defaults can be overridden by a call to
 * xdf_set_conf() if the file is open for writing. If the file is
 * opened for reading, the default are meaningful only for the fields
 * XDF_CF_ARR*). If a list of data formats is specified in curl brackets, it
 * means that the field is supported only in those formats (no list means that
 * all formats support the field):
 *
 * XDF_CF_ARRINDEX (int) [0] specifies the array from/to which the channel 
 * value should be transferred. If the mode of the file is XDF_READ and the
 * value is negative, the channel will not be read. If the mode is XDF_WRITE
 * and the value is negative, the function will fail.
 *
 * XDF_CF_ARROFFSET (int) [0 for channel 0, packed channels for the rest]
 * specifies the offset in the array from/to which the channel value should
 * be transferred.
 *
 * XDF_CF_ARRDIGITAL (int) [0 if writing, 1 if reading] indicates that the
 * data in the array from/to which the channel value should be transferred
 * is provided in digital unit. This means in practice that no scaling is
 * performed during the transfer from/to the disk (non zero indicate no
 * scaling).
 *
 * XDF_CF_ARRTYPE (enum xdftype) [same as XDF_CF_STOTYPE] specifies the type
 * in the channel should casted to/from when accessed in the array.
 *
 * XDF_CF_PMIN (double) [min of XDF_CF_ARRTYPE] sets the minimal value
 * that a physical value can get. Cannot be set if XDF_READ.
 *
 * XDF_CF_PMAX (double) [max of \fBXDF_CF_ARRTYPE\fP] sets the maximal value
 * that a physical value can get. Cannot be set if XDF_READ.
 *
 * XDF_CF_STOTYPE (enum xdftype) [any datatype supported by file type]
 * specifies the type stored in the file of the channel value. If the XDF
 * file has been opened in XDF_READ, this field cannot be set. If this field
 * is successfully set, it will set as well the digital minimum (XDF_CF_DMIN)
 * and the digital maximum (XDF_CF_MAX) to the minimum and maximum values
 * allowed by the data type.
 *
 * XDF_CF_DMIN (double) [min of XDF_CF_STOTYPE] sets the minimal value that a
 * digital value can get. Cannot be set if XDF_READ. This is also automatically
 * set by XDF_CF_STOTYPE.
 *
 * XDF_CF_DMAX (double) [min of XDF_CF_STOTYPE] sets the maximal value that a
 * digital value can get. Cannot be set if XDF_READ. This is also automatically
 * set by XDF_CF_STOTYPE.
 *
 * XDF_CF_LABEL (const char*) [""] sets the label of the channel. Cannot be set
 * if XDF_READ.
 *
 * XDF_CF_UNIT (const char*) [""] {EDF BDF GDF} sets the unit of the channel.
 * Cannot be set if XDF_READ.
 *
 * XDF_CF_TRANSDUCTER (const char*) [""] {EDF BDF GDF} sets the type of sensor
 * used for this channel. Cannot be set if XDF_READ.
 *
 * XDF_CF_PREFILTERING (const char*) [""] {EDF BDF GDF} sets the information
 * about the filters already applied on channel data. Cannot be set if
 * XDF_READ.
 *
 * XDF_CF_ELECPOS (double[3]) [0,0,0] {GDF} sets the position of the
 * sensor/electrode expressed in X,Y,Z components. Cannot be set if XDF_READ.
 *
 * XDF_CF_IMPEDANCE (double) [0] {GDF} sets the impedance of the
 * sensor/electrode. Cannot be set if XDF_READ.
 *
 * Return: 0 in case of success. Otherwise -1 is returned and errno is set
 *         appropriately (EINVAL if @ch is NULL or @field is not a proper
 *         value of the enumeration xdffield, EPERM if the request submitted
 *         is not allowed for this channel or is forbidden for file opened
 *         with the mode XDF_READ, or EDOM if the value set in xdf_set_chconf()
 *         as digital or physical min/max (fields XDF_CF_{D/P}{MIN/MAX}) goes
 *         beyond the limits of respectively the stored or array data type).
 *
 * Example of usage of xdf_set_chconf():
 * //Assume xdf referenced an XDF file opened for writing
 * unsigned int iarray = 2, offset = 0;
 * const char label[] = "Channel EEG";
 * 
 * hchxdf ch = xdf_add_channel(xdf);
 * xdf_set_chconf(ch, XDF_CF_ARRINDEX, iarray,
 *                       XDF_CF_ARROFFSET, offset,
 *                       XDF_CF_LABEL, label,
 *                       XDF_NOF);
 */
API_EXPORTED int xdf_set_chconf(struct xdfch* ch, enum xdffield field, ...)
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


/**
 * xdf_copy_chconf() - configures a channel according to a template
 * @dst:	pointer to the destination xdf channel
 * @src:	pointer to the source xdf channel
 *
 * xdf_copy_chconf() configures the channel referenced by @dst
 * using the information described by @src.
 *
 * Return: 0 in case of success, -1 otherwise.
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


/**
 * xdf_set_conf() - sets the configuration of XDF file
 * @xdf:	pointer to a xdf file
 * @field:	identifier of the field to be set
 *
 * xdf_set_confP() sets the configuration (usually one of the field file
 * header) of a XDF file referenced by @xdf according to the variable list
 * of arguments. This list is composed of successive couple grouping one
 * variable of type enum xdffield defining the feature to be set and a value
 * whose type depends on the previous field type. The list MUST finish by
 * XDF_NOF.
 *
 * This function processes the argument list from left to right. This
 * means that if a particular field request provokes an error, none of the
 * field requests on its right will be processed. The order of processing is
 * also important for field requests that influences the value of other fields
 * (like XDF_F_REC_NSAMPLE or XDF_F_SAMPLING_FREQ).
 * 
 * The function accepts two types of field value. The first one are
 * file configuration field (XDF_F_*) which set different aspects of the
 * general configuration of the file. The second type are the channel
 * configuration fields (XDF_CF_*). When used in xdf_set_conf(), those
 * fields set the default values that will be used for the creation of the next
 * channel (see xdf_add_channel()). The list of channel configuration
 * fields and their meaning are specified in the documentation of
 * xdf_set_chconf().
 *
 * If the file is opened for writing, each field is initialized to sensible or
 * non-informative values in case of optional field or incorrect values in the
 * case of "must be set" field (only XDF_F_REC_NSAMPLE or XDF_F_SAMPLING_FREQ).
 * The default value are specified in squared bracked in the list.
 *
 * Here is the list of file configuration field value. The type of value
 * expected is provided in the parenthesis. If a list of data formats is
 * specified in curl brackets, it means that the field is supported only in
 * those formats (no list means that all formats support the field):
 *
 * XDF_F_REC_DURATION (double) [1] specifies the duration of one record. The
 * value should be positive.
 *
 * XDF_F_REC_NSAMPLE (int) [0] specifies the number of time points contained
 * in each record. The value should be positive. Setting the number of sample
 * per record modifies the sampling frequency (field XDF_SAMPLING_FREQ).
 *
 * XDF_F_SAMPLING_FREQ (int) [0] sets the sampling frequency of the recording.
 * Setting the sampling frequency modifies the number of sample per record
 * (field XDF_F_REC_NSAMPLE).
 *
 * XDF_F_RECTIME (double) [current time] {EDF BDF GDF} sets date and time of
 * recording. It is expressed as number of seconds elapsed since the Epoch,
 * 1970-01-01 00:00:00 +0000 (UTC). This number is the conversion to double
 * of the value returned by the function time() of the standard C library. On
 * file creation, this field is initialized to the current time.
 *
 * XDF_F_SUBJ_DESC (const char*) [""] {EDF BDF GDF} specifies the string
 * describing the subject.
 *
 * XDF_F_SESS_DESC (const char*) [""] {EDF BDF GDF} specifies the string
 * describing the session of recording.
 *
 * XDF_F_ADDICTION\fP (unsigned int) [0] {GDF}
 *
 * XDF_F_BIRTHDAY (double) [0] {GDF} sets birthday of the patient using the
 * same format as specified for field XDF_F_RECTIME.
 *
 * XDF_F_HEIGHT (double) [0] {GDF} sets height of the subject in centimeters.
 *
 * XDF_F_WEIGHT (double) [0] {GDF} sets weight of the subject in kilograms.
 *
 * XDF_F_GENDER (unsigned int) [0] {GDF} sets sex of the subject. Use 1 for
 * male, 2 for female and 0 if unknown.
 *
 * XDF_F_HANDNESS (unsigned int) [0] {GDF} sets handness of the subject. Use
 * 0 if unknown, 1 if right, 2 if left and 3 if ambidextrious.
 *
 * XDF_F_VISUAL_IMP (unsigned int) [0] {GDF} sets visual impairment. Use 0
 * if unknown, 1 if no impairment, 2 if impaired and 3 if impaired but
 * corrected.
 *
 * XDF_F_HEART_IMP (unsigned int) [0] {GDF} sets heart impairment. Use 0 if
 * unknown, 1 if no impairment, 2 if impaired and 3 if the subject wear a
 * pacemaker.
 *
 * XDF_F_LOCATION (double[3]) [0,0,0] {GDF} sets location of the recording.
 * The first 2 component specify the latitude and longitude in degrees, the
 * third specifies the altitude in meters.
 *
 * XDF_F_ICD_CLASS (char[6]) [0x000000000000] {GDF} sets patient classification
 * according to the International Statistical Classification of Diseases and
 * Related Health Problems (ICD).
 *
 * XDF_F_HEADSIZE (double[3]) [0,0,0] {GDF} sets size of the subject's head
 * (circumference, distance nasion to inion, distance left to right mastoid)
 * expressed in millimeters.
 *
 * BXDF_F_REF_POS (double[3]) [0,0,0] {GDF} sets X, Y, Z coordinates of the
 * reference electrode.
 *
 * XDF_F_GND_POS (double[3]) [0,0,0] {GDF} sets X, Y, Z coordinates of the
 * ground electrode.
 *
 * Return: 0 in case of success. Otherwise -1 is returned and errno is set
 *         appropriately (EINVAL if @xdf is NULL or @field is not a proper
 *         value of the enumeration xdffield accepted by the function (for
 *         example XDF_F_NCHANNEL), or EPERM if the request submitted to 
 *         xdf_set_conf is not allowed for this type of XDF file or is not
 *         supported with the mode XDF_READ).
 *
 * Example of usage of xdf_set_conf():
 * // Assume xdfr and xdfw reference 2 XDF files opened respectively
 * // for reading and for writing
 * const char *subjstr, *sessstr;
 *
 * xdf_get_conf(xdfr, XDF_F_SUBJ_DESC, &subjstr,
 *                  XDF_F_SESS_DESC, &sessstr,
 *                  XDF_NOF);
 *
 * xdf_set_conf(xdfw, XDF_F_SUBJ_DESC, subjstr,
 *                   XDF_F_SESS_DESC, sessstr,
 *                   XDF_NOF);
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
 * occurred, 0 otherwise
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


/**
 * xdf_get_conf() - gets the configuration of XDF file
 * @xdf:	pointer to a xdf file
 * @field:	identifier of the field to be get
 *
 * xdf_get_conf() gets the configuration of the channel referenced by
 * @xdf according to the variable list of argument. The variable list is
 * the same list terminated by XDF_NOF as for
 * xdf_set_conf() excepting that the second part of the couple is not
 * that value but a pointer to the value.
 *
 * This function processes the argument list from left to right. This
 * means that if a particular field request provokes an error, none of the
 * field requests on its right will be processed. The order of processing is
 * also important for field requests that influences the value of other fields
 * (like XDF_F_REC_NSAMPLE or XDF_F_SAMPLING_FREQ).
 * 
 * The function accepts two types of field value. The first one are
 * file configuration field (XDF_F_*) which set different aspects of the
 * general configuration of the file. The second type are the channel
 * configuration fields (XDF_CF_*).
 *
 * If the file is opened for writing, each field is initialized to sensible or
 * non-informative values in case of optional field or incorrect values in the
 * case of "must be set" field (only XDF_F_REC_NSAMPLE or XDF_F_SAMPLING_FREQ).
 * The default value are specified in squared bracked in the list.
 *
 * Here is the list of file configuration field value. The type of the pointer
 * expected is provided in the parenthesis. If a list of data formats is
 * specified in curl brackets, it means that the field is supported only in
 * those formats (no list means that all formats support the field):
 *
 * XDF_F_REC_DURATION (double) [1] specifies the duration of one record. The
 * value should be positive.
 *
 * XDF_F_REC_NSAMPLE (int) [0] specifies the number of time points contained
 * in each record. The value should be positive
 *
 * XDF_F_SAMPLING_FREQ (int) [0] gets the sampling frequency of the recording. 
 *
 * XDF_F_NCHANNEL (int) gets the number of channel in the file.
 *
 * XDF_F_NEVTTYPE (int) gets the number of different event types.
 *
 * XDF_F_NEVENT (int) gets the number of events.
 *
 * XDF_F_NREC (int) gets the number of record in the file.
 *
 * XDF_F_FILEFMT (int) gets the file format type (one of the value defined
 * by the enumeration xdffiletype other than XDF_ANY).
 *
 * XDF_F_RECTIME (double) [current time] {EDF BDF GDF} gets date and time of
 * recording. It is expressed as number of seconds elapsed since the Epoch,
 * 1970-01-01 00:00:00 +0000 (UTC). This number is the conversion to double
 * of the value returned by the function time() of the standard C library. On
 * file creation, this field is initialized to the current time.
 *
 * XDF_F_SUBJ_DESC (const char*) [""] {EDF BDF GDF} specifies the string
 * describing the subject.
 *
 * XDF_F_SESS_DESC (const char*) [""] {EDF BDF GDF} specifies the string
 * describing the session of recording.
 *
 * XDF_F_ADDICTION\fP (unsigned int) [0] {GDF}
 *
 * XDF_F_BIRTHDAY (double) [0] {GDF} gets birthday of the patient using the
 * same format as specified for field XDF_F_RECTIME.
 *
 * XDF_F_HEIGHT (double) [0] {GDF} gets height of the subject in centimeters.
 *
 * XDF_F_WEIGHT (double) [0] {GDF} gets weight of the subject in kilograms.
 *
 * XDF_F_GENDER (unsigned int) [0] {GDF} gets sex of the subject. Use 1 for
 * male, 2 for female and 0 if unknown.
 *
 * XDF_F_HANDNESS (unsigned int) [0] {GDF} gets handness of the subject. Use
 * 0 if unknown, 1 if right, 2 if left and 3 if ambidextrious.
 *
 * XDF_F_VISUAL_IMP (unsigned int) [0] {GDF} gets visual impairment. Use 0
 * if unknown, 1 if no impairment, 2 if impaired and 3 if impaired but
 * corrected.
 *
 * XDF_F_HEART_IMP (unsigned int) [0] {GDF} gets heart impairment. Use 0 if
 * unknown, 1 if no impairment, 2 if impaired and 3 if the subject wear a
 * pacemaker.
 *
 * XDF_F_LOCATION (double[3]) [0,0,0] {GDF} gets location of the recording.
 * The first 2 component specify the latitude and longitude in degrees, the
 * third specifies the altitude in meters.
 *
 * XDF_F_ICD_CLASS (char[6]) [0x000000000000] {GDF} gets patient classification
 * according to the International Statistical Classification of Diseases and
 * Related Health Problems (ICD).
 *
 * XDF_F_HEADSIZE (double[3]) [0,0,0] {GDF} gets size of the subject's head
 * (circumference, distance nasion to inion, distance left to right mastoid)
 * expressed in millimeters.
 *
 * BXDF_F_REF_POS (double[3]) [0,0,0] {GDF} gets X, Y, Z coordinates of the
 * reference electrode.
 *
 * XDF_F_GND_POS (double[3]) [0,0,0] {GDF} gets X, Y, Z coordinates of the
 * ground electrode.
 *
 * Return: 0 in case of success. Otherwise -1 is returned and errno is set
 *         appropriately (EINVAL if @xdf is NULL or @field is not a proper
 *         value of the enumeration xdffield accepted by the function, or
 *         EPERM if the request submitted is not supported with the mode
 *         XDF_READ).
 *
 * Example of usage of xdf_get_conf():
 * // Assume xdfr references a XDF file opened for reading
 * const char *subjstr, *sessstr;
 *
 * xdf_get_conf(xdfr, XDF_F_SUBJ_DESC, &subjstr,
 *                  XDF_F_SESS_DESC, &sessstr,
 *                  XDF_NOF);
 *
 * printf("subject: %s\\nrecording: %s\\n", subjstr, sessstr);
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


/**
 * xdf_copy_conf() - configures a XDF file according to a template
 * @dst:	pointer to the destination xdf file
 * @src:	pointer to the source xdf file
 *
 * xdf_copy_conf() configures the XDF file referenced by @dst using
 * the information described by @src.
 *
 * Return: 0 in case of success, -1 otherwise
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


/**
 * xdf_closest_type() - returns a compatible data type
 * @xdf:	pointer to a xdf structure
 * @type:	target data type
 *
 * xdf_closest_type() selects among the data types supported by the file
 * referenced by @xdf the type that is the closest to the @target
 * argument. The selected type can then be safely used in a call to
 * xdf_set_chconf() with the XDF_CF_STOTYPE field.
 *
 * The selection algorithm is based on the 3 following criterions (cited by
 * priority, i.e. most important cited first): data size, signed/unsigned type,
 * float/integer value. The data size criterion forces the selected type to
 * have a data size (number of byte to represent the value) equal or bigger
 * than the one of the target type (with a preference with sizes the closest to
 * the size of @target). The signed/unsigned criterion tries to
 * select a type that has the same signeness (signed or unsigned data type) as
 * the target. Finally the float/integer criterion tries to select a floating
 * point type if @target is float or double or an integer data type if
 * @target is an integer type.
 *
 * As a consequence, if @target is supported by the underlying file format
 * of @xdf, the function is ensured to return @target.
 *
 * Return: the selected data type in case of success, otherwise -1 and 
 *         errno is set appropriately (EINVAL if the @xdf pointer is NULL, 
 *         or if the argument @type is not an admissible enum xdftype value).
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

static
int write_event(struct xdf* xdf, struct xdfevent* evt)
{
#if WORDS_BIGENDIAN
	struct xdfevent be_evt = {
		.evttype = bswap_32(evt->evttype),
		.onset = bswap_64(evt->onset),
		.duration = bswap_64(evt->duration),
	};
	evt = &be_evt;
#endif

	return (mm_write(xdf->tmp_event_fd, evt, sizeof(*evt)) == -1);
}

static
int write_code(struct xdf* xdf, int code, const char* desc, int evttype)
{
	size_t host_desc_len = desc ? strlen(desc) : 0;
	uint32_t desc_len = host_desc_len;

#if WORDS_BIGENDIAN
	evttype = bswap_32(evttype);
	code = bswap_32(code);
	desc_len = bswap_32(desc_len);
#endif
	return (mm_write(xdf->tmp_code_fd, &evttype, sizeof(evttype)) == -1
	        || mm_write(xdf->tmp_code_fd, &code, sizeof(code)) == -1
	        || mm_write(xdf->tmp_code_fd, &desc_len, sizeof(desc_len)) == 1
	        || mm_write(xdf->tmp_code_fd, desc, host_desc_len) == -1);
}


/**
 * xdf_add_evttype() - adds a type of event
 * @xdf:	pointer to a xdf structure
 * @code:       code of the event
 * @desc:       label of the event
 *
 * xdf_add_evttype() adds an event type specified by combination of
 * @code and the event description @desc to the file referenced by
 * the handle @xdf opened for writing. If there is no description
 * associated with the event type, @desc should be set to NULL.
 * 
 * If an event type with the same combination has been already added, no new
 * type will be added and the previous type will be returned.
 *
 * Return: the event type in case of success. Otherwise -1 is returned and 
 *         errno is set appropriately (EINVAL if @xdf is NULL, ENOMEM if
 *         the system is unable to allocate resources, or EPERM if the
 *         file referenced by @xdf has not been opened for writing or if its
 *         file format does not support events).
 */
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

	if (xdf->tmp_code_fd >= 0)
		write_code(xdf, code, desc, evttype);

	return evttype;
}


/**
 * xdf_get_evttype() - gets information about an event type
 * @xdf: pointer to a xdf structure
 * @evttype: index of the event to retrieve (event type)
 * @code: integer filled with the code of the event
 * @desc: string filled with the label of the event
 *
 * xdf_get_evttype() returns the information of the event type
 * @evttype of the XDF file referenced by @xdf. The code and the
 * description of the event type are returned respectively in the pointers
 * @code and @desc.
 *
 * Return: 0 in case of success. Otherwise -1 is returned and errno is set
 *         appropriately (EINVAL if @xdf, @code or @desc is NULL, or ERANGE if
 *         @evttype is an invalid event type of @xdf).
 */
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


/**
 * xdf_add_event() - appends an event to the data file
 * @xdf: pointer to a xdf structure
 * @evttype: event type
 * @onset: start of the event
 * @duration: duration of the event
 *
 * xdf_add_event() appends to the file referenced by the handle @xdf
 * opened for writing an event of type @evttype at time @onset
 * lasting for a duration @dur expressed in seconds. If the event has no
 * notion of duration, @dur should be set to 0. @evttype should be a
 * value returned by a successful call to xdf_add_evttype().
 * 
 * Return: 0 in case of success. 
 *         Otherwise -1 is returned and errno is set appropriately (EINVAL
 *         if @xdf is NULL or if @evttype has not been previously created by
 *         xdf_add_evttype(), ENOMEM if the system is unable to allocate
 *         resources, or EPERM if the file referenced by @xdf has not been
 *         opened for writing or if its file format does not support events).
 */
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

	if (xdf->tmp_event_fd >= 0)
		write_event(xdf, &evt);

	return add_event(xdf->table, &evt);
}


/**
 * xdf_get_event() - gets details of an event of a data file
 * @xdf: pointer to a xdf structure
 * @index: index of the event to retrieve
 * @evttype: integer filled with the event type
 * @start: integer filled with the start of the event
 * @dur: integer filled with the duration of the event
 *
 * xdf_get_event() returns the information of the @index-th event of
 * the file referenced by the handle @xdf. The event type, start (in
 * seconds) and duration (in seconds) of the event are returned respectively to
 * the pointers @evttype, @start and @dur.
 *
 * Return: 0 in case of success. Otherwise -1 is returned and errno is set 
 *         appropriately (EINVAL if @xdf, @evttype, @start or @dur is NULL,
 *         or ERANGE if @index is bigger than the number event in the file).
 */
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

