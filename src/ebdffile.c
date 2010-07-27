#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include "xdfio.h"
#include "xdffile.h"
#include "xdftypes.h"
#include "ebdf.h"

/******************************************************
 *               EDF/BDF specific declaration             *
 ******************************************************/

// EDF/BDF methods declaration
static int ebdf_set_channel(struct xdfch*, enum xdffield,
                           union optval, int);
static int ebdf_get_channel(const struct xdfch*, enum xdffield,
                           union optval*, int);
static int ebdf_copy_chconf(struct xdfch*, const struct xdfch*);
static struct xdfch* ebdf_alloc_channel(void);
static void ebdf_free_channel(struct xdfch* xdfch);
static int ebdf_set_conf(struct xdf*, enum xdffield, union optval, int); 
static int ebdf_get_conf(const struct xdf*, enum xdffield,
                        union optval*, int); 
static int ebdf_copy_conf(struct xdf*, const struct xdf*); 
static int ebdf_write_header(struct xdf*);
static int ebdf_read_header(struct xdf*);
static int ebdf_complete_file(struct xdf*);
static void ebdf_free_file(struct xdf*);

// EDF/BDF channel structure
struct ebdf_channel {
	struct xdfch xdfch;
	char label[17];
	char transducter[81];
	char unit[9];
	char prefiltering[81];
	char reserved[33];
};

// BDF file structure
struct ebdf_file {
	struct xdf xdf;
	struct ebdf_channel default_ebdfch;
	char subjstr[81];
	char recstr[81];
	struct tm rectime;
};

#define NUMREC_FIELD_LOC 236

#define get_ebdf(xdf_p) \
	((struct ebdf_file*)(((char*)(xdf_p))-offsetof(struct ebdf_file, xdf)))
#define get_ebdfch(xdfch_p) 				\
	((struct ebdf_channel*)(((char*)(xdfch_p))	\
		- offsetof(struct ebdf_channel, xdfch)))


/******************************************************
 *            BDF type definition declaration         *
 ******************************************************/

static const struct format_operations bdf_ops = {
	.set_channel = ebdf_set_channel,
	.get_channel = ebdf_get_channel,
	.copy_chconf = ebdf_copy_chconf,
	.alloc_channel = ebdf_alloc_channel,
	.free_channel = ebdf_free_channel,
	.set_conf = ebdf_set_conf,
	.get_conf = ebdf_get_conf,
	.copy_conf = ebdf_copy_conf,
	.write_header = ebdf_write_header,
	.read_header = ebdf_read_header,
	.complete_file = ebdf_complete_file,
	.free_file = ebdf_free_file,
	.type = XDF_BDF,
};

static const unsigned char bdf_magickey[] = 
	{255, 'B', 'I', 'O', 'S', 'E', 'M', 'I'};

static const struct ebdf_channel bdfch_def = {
	.xdfch = {
		.iarray = 0,
		.offset = 0,
		.infiletype = XDFINT24,
		.inmemtype = XDFINT32,
		.digital_inmem = 0,
		.digital_mm = {INT24_MIN, INT24_MAX},
		.physical_mm = {INT24_MIN, INT24_MAX},
	}
};

static const struct format_operations edf_ops = {
	.set_channel = ebdf_set_channel,
	.get_channel = ebdf_get_channel,
	.copy_chconf = ebdf_copy_chconf,
	.alloc_channel = ebdf_alloc_channel,
	.free_channel = ebdf_free_channel,
	.set_conf = ebdf_set_conf,
	.get_conf = ebdf_get_conf,
	.copy_conf = ebdf_copy_conf,
	.write_header = ebdf_write_header,
	.read_header = ebdf_read_header,
	.complete_file = ebdf_complete_file,
	.free_file = ebdf_free_file,
	.type = XDF_EDF,
};

static const unsigned char edf_magickey[] = 
	{'0', ' ', ' ', ' ', ' ', ' ', ' ', ' '};

static const struct ebdf_channel edfch_def = {
	.xdfch = {
		.iarray = 0,
		.offset = 0,
		.infiletype = XDFINT16,
		.inmemtype = XDFFLOAT,
		.digital_inmem = 0,
		.digital_mm = {INT16_MIN, INT16_MAX},
		.physical_mm = {INT16_MIN, INT16_MAX},
	}
};

/******************************************************
 *            BDF file support implementation         *
 ******************************************************/

/* Allocate a EDF/BDF file
 */
static struct xdf* alloc_ebdffile(const struct format_operations* ops,
                                  const struct ebdf_channel* defch)
{
	struct ebdf_file* ebdf;

	ebdf = calloc(1, sizeof(*ebdf));
	if (!ebdf)
		return NULL;

	ebdf->xdf.ops = ops;
	ebdf->xdf.defaultch = &(ebdf->default_ebdfch.xdfch);

	// Set good default for the file format
	memcpy(&(ebdf->default_ebdfch), defch, sizeof(*defch));
	ebdf->default_ebdfch.xdfch.owner = &(ebdf->xdf);
	ebdf->xdf.rec_duration = 1.0;
	
	return &(ebdf->xdf);
}

/* Allocate a BDF file
 */
 struct xdf* xdf_alloc_bdffile(void)
 {
 	return alloc_ebdffile(&bdf_ops, &bdfch_def);
 }


/* \param magickey	pointer to key identifying a type of file
 *
 * Returns 1 if the supplied magickey corresponds to a BDF file
 */
int xdf_is_bdffile(const unsigned char* magickey)
{
	if (memcmp(magickey, bdf_magickey, sizeof(bdf_magickey)) == 0)
		return 1;
	return 0;
}


/* Allocate a EDF file
 */
struct xdf* xdf_alloc_edffile(void)
{
 	return alloc_ebdffile(&edf_ops, &edfch_def);
}


/* \param magickey	pointer to key identifying a type of file
 *
 * Returns 1 if the supplied magickey corresponds to a BDF file
 */
int xdf_is_edffile(const unsigned char* magickey)
{
	if (memcmp(magickey, edf_magickey, sizeof(edf_magickey)) == 0)
		return 1;
	return 0;
}

/******************************************************
 *             EDF/BDF methods implementation         *
 ******************************************************/

/* \param ch	pointer to a xdf channel (EDF/BDF) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to be set
 *
 * EDF/BDF METHOD.
 * Change the configuration field value of the channel according to val
 */
static int ebdf_set_channel(struct xdfch* ch, enum xdffield field,
                            union optval val, int prevretval)
{
	struct ebdf_channel* ebdfch = get_ebdfch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		strncpy(ebdfch->label, val.str, sizeof(ebdfch->label)-1);
	else if (field == XDF_CF_UNIT)
		strncpy(ebdfch->unit, val.str, sizeof(ebdfch->unit)-1);
	else if (field == XDF_CF_TRANSDUCTER)
		strncpy(ebdfch->transducter, val.str, sizeof(ebdfch->transducter)-1);
	else if (field == XDF_CF_PREFILTERING)
		strncpy(ebdfch->prefiltering, val.str, sizeof(ebdfch->prefiltering)-1);
	else if (field == XDF_CF_RESERVED)
		strncpy(ebdfch->reserved, val.str, sizeof(ebdfch->reserved)-1);
	else
		retval = prevretval;

	return retval;
}


/* \param ch	pointer to a xdf channel (BDF underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * EDF/BDF METHOD.
 * Get the configuration field value of the channel and assign it to val
 */
static int ebdf_get_channel(const struct xdfch* ch, enum xdffield field,
                            union optval *val, int prevretval)
{
	struct ebdf_channel* ebdfch = get_ebdfch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		val->str = ebdfch->label;
	else if (field == XDF_CF_UNIT)
		val->str = ebdfch->unit;
	else if (field == XDF_CF_TRANSDUCTER)
		val->str = ebdfch->transducter;
	else if (field == XDF_CF_PREFILTERING)
		val->str = ebdfch->prefiltering;
	else if (field == XDF_CF_RESERVED)
		val->str = ebdfch->reserved;
	else
		retval = prevretval;

	return retval;
}


/* \param dst	pointer to a xdf channel (EDF/BDF) with XDF_WRITE mode
 * \param src	pointer to a xdf channel
 *
 * EDF/BDF METHOD.
 * Copy the fields of a xDF channel to EDF/BDF channel
 */
static int ebdf_copy_chconf(struct xdfch* dst, const struct xdfch* src)
{
	double dmin, dmax, pmin, pmax;
	enum xdftype ts, ta;
	unsigned int offset, index, digital_inmem;
	const char *label, *unit, *transducter, *filtinfo, *reserved;

	// Fast copy if they are of the same type
	if (src->owner->ops->type == dst->owner->ops->type) {
		struct ebdf_channel* ebdfsrc = get_ebdfch(src);
		struct ebdf_channel* ebdfdst = get_ebdfch(dst);
		struct xdfch* next = dst->next;
		struct xdf* owner = dst->owner;

		memcpy(ebdfdst, ebdfsrc, sizeof(*ebdfsrc));

		// Channel specific data
		dst->owner = owner;
		dst->next = next;

		return 0;
	}

	xdf_get_chconf(src, 
			XDF_CF_ARRTYPE, &ta,
			XDF_CF_PMIN, &pmin,
			XDF_CF_PMAX, &pmax,
			XDF_CF_STOTYPE, &ts,
			XDF_CF_DMIN, &dmin,
			XDF_CF_DMAX, &dmax,
			XDF_CF_ARRDIGITAL, &digital_inmem,
			XDF_CF_ARROFFSET, &offset,
			XDF_CF_ARRINDEX, &index,
			XDF_CF_LABEL, &label,
			XDF_CF_UNIT, &unit,
			XDF_CF_TRANSDUCTER, &transducter,
			XDF_CF_PREFILTERING, &filtinfo,
			XDF_CF_RESERVED, &reserved,
			XDF_NOF);

	xdf_set_chconf(dst,
			XDF_CF_ARRTYPE, ta,
			XDF_CF_PMIN, pmin,
			XDF_CF_PMAX, pmax,
			XDF_CF_STOTYPE, ts,
			XDF_CF_DMIN, dmin,
			XDF_CF_DMAX, dmax,
			XDF_CF_ARRDIGITAL, digital_inmem,
			XDF_CF_ARROFFSET, offset,
			XDF_CF_ARRINDEX, index,
			XDF_CF_LABEL, label,
			XDF_CF_UNIT, unit,
			XDF_CF_TRANSDUCTER, transducter,
			XDF_CF_PREFILTERING, filtinfo,
			XDF_CF_RESERVED, reserved,
			XDF_NOF);

	return 0;
}


/* BDF METHOD.
 * Allocate a BDF channel 
 */
static struct xdfch* ebdf_alloc_channel(void)
{
	struct ebdf_channel* ch;
	
	ch = malloc(sizeof(*ch));
	if (!ch)
		return NULL;
	memset(ch, 0, sizeof(*ch));
	return &(ch->xdfch);
}


/* \param ch	pointer to a xdf channel (underlying EDF/BDF)
 *
 * EDF/BDF METHOD.
 * Free a EDF/BDF channel
 */
static void ebdf_free_channel(struct xdfch* ch)
{
	free(get_ebdfch(ch));
}

/* \param xdf	pointer to a xdf file (EDF/BDF) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to set
 *
 * EDF/BDF METHOD.
 * Change the configuration field value according to val
 */
static int ebdf_set_conf(struct xdf* xdf, enum xdffield field, 
                         union optval val, int prevretval)
{
	struct ebdf_file* ebdf = get_ebdf(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		strncpy(ebdf->subjstr, val.str, sizeof(ebdf->subjstr)-1);
	else if (field == XDF_F_SESS_DESC)
		strncpy(ebdf->recstr, val.str, sizeof(ebdf->recstr)-1);
	else
		retval = prevretval;

	return retval;
}

/* \param xdf	pointer to a xdf file (EDF/BDF underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * EDF/BDF METHOD.
 * Get the configuration field value and assign it to val
 */
static int ebdf_get_conf(const struct xdf* xdf, enum xdffield field,
union optval *val, int prevretval)
{
	struct ebdf_file* ebdf = get_ebdf(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		val->str = ebdf->subjstr;
	else if (field == XDF_F_SESS_DESC)
		val->str = ebdf->recstr;
	else
		retval = prevretval;

	return retval;
}

/* \param dst	pointer to a xdf file (EDF/BDF) with XDF_WRITE mode
 * \param src	pointer to a xdf file
 *
 * EDF/BDF METHOD.
 * Copy the fields of a xDF file configuration to EDF/BDF file
 */
static int ebdf_copy_conf(struct xdf* dst, const struct xdf* src)
{
	double recduration;
	int ns_rec;
	const char *subj, *rec;

	xdf_get_conf(src, XDF_F_REC_DURATION, &recduration,
				XDF_F_REC_NSAMPLE, &ns_rec,
				XDF_F_SUBJ_DESC, &subj,
				XDF_F_SESS_DESC, &rec,
				XDF_NOF);

	xdf_set_conf(dst, XDF_F_REC_DURATION, recduration,
				XDF_F_REC_NSAMPLE, ns_rec,
				XDF_F_SUBJ_DESC, subj,
				XDF_F_SESS_DESC, rec,
				XDF_NOF);

	return 0;
}


/* \param ebdf	pointer to a ebdf_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of the file)
 *
 * Write the general EDF/BDF file header. Write -1 in the number of records
 * field. The real value is written after the transfer is finished
 */
static int ebdf_write_file_header(struct ebdf_file* ebdf, FILE* file)
{
	char timestring[17];
 	unsigned headersize = 256 + (ebdf->xdf.numch)*256;
	int retval;
	const unsigned char* mkey;
	enum xdffiletype type = ebdf->xdf.ops->type;

	mkey = (type == XDF_BDF) ? bdf_magickey : edf_magickey;

	// format time string
	strftime(timestring, sizeof(timestring), 
		 "%d.%m.%y%H.%M.%S", &(ebdf->rectime));

	// Write data format identifier
	if (fwrite(mkey, 8, 1, file) < 1)
		return -1;

	// Write all the general file header
	retval = fprintf(file, 
			"%-80.80s%-80.80s%16s%-8u%-44.44s%-8i%-8u%-4u",
			ebdf->subjstr,
			ebdf->recstr,
			timestring,
			headersize,
			(type == XDF_BDF) ? "24BIT" : "EDF",
			(int)-1,
			(unsigned int)1,
			ebdf->xdf.numch);
	
	if (retval < 0)
		return -1;
	
	return 0;
}

/* \param bdf	pointer to a ebdf_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of channel fields)
 *
 * Write the EDF/BDF channels related fields in the header.
 */
static int ebdf_write_channels_header(struct ebdf_file* bdf, FILE* file)
{
	struct xdfch* ch;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_ebdfch(ch)->label) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_ebdfch(ch)->transducter) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8.8s", get_ebdfch(ch)->unit) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->physical_mm[0])) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->physical_mm[1])) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->digital_mm[0])) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->digital_mm[1])) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_ebdfch(ch)->prefiltering) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8u", bdf->xdf.ns_per_rec) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-32.32s", get_ebdfch(ch)->reserved) < 0)
			return -1;

	return 0;
}


/* \param xdf	pointer to an xdf (EDF/BDF) file with XDF_WRITE mode
 *
 * EDF/BDF METHOD.
 * Write the general file header and channels fields
 * It creates from the file a stream that will be used to easily format
 * the header fields.
 */
static int ebdf_write_header(struct xdf* xdf)
{
	int retval = 0;
	struct ebdf_file* bdf = get_ebdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "w");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( ebdf_write_file_header(bdf, file)
	    || ebdf_write_channels_header(bdf, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);

	return retval;
}


/* Parse the file (field of nch characters) and assign to the integer val.
 * Advance the file pointer of exactly nch byte.
 */
static int read_int_field(FILE* file, int* val, size_t nch)
{
	char format[8];
	long pos = ftell(file);
	sprintf(format, "%%%zui", nch);
	
	if (fscanf(file, format, val) <= 0
	    || fseek(file, pos+nch, SEEK_SET))
		return -1;
	
	return 0;
}


/* Parse the file (field of nch characters) and assign to the string val.
 * Advance the file pointer of exactly nch byte.
 * It also removes trailing space characters from the string
 */
static int read_string_field(FILE* file, char* val, size_t nch)
{
	int pos;

	val[nch] = '\0';
	if (fread(val, nch, 1, file) < 1)
		return -1;

	// Remove trailing spaces
	pos = strlen(val);
	while (pos && (val[pos]==' '))
		pos--;
	val[pos] = '\0';

	return 0;
}


/* \param bdf	pointer to a ebdf_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the beginning of the file)
 *
 * Read the general BDF file header
 */
static int ebdf_read_file_header(struct ebdf_file* bdf, FILE* file)
{
	char timestring[17], type[45];
	int recdur, hdrsize, retval = 0;

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, bdf->subjstr, 80)
	   || read_string_field(file, bdf->recstr, 80)
	   || read_string_field(file, timestring, 16)
	   || read_int_field(file, &hdrsize, 8)
	   || read_string_field(file, type, 44)
	   || read_int_field(file, &(bdf->xdf.nrecord), 8)
	   || read_int_field(file, &recdur, 8)
	   || read_int_field(file, (int*)&(bdf->xdf.numch), 4) )
		retval = -1;
 
	bdf->xdf.rec_duration = (double)recdur;
	bdf->xdf.hdr_offset = hdrsize;

	// format time string
	if (!retval)
		strftime(timestring, sizeof(timestring), 
			 "%d.%m.%y%H.%M.%S", &(bdf->rectime));

	return retval;
}


/* \param bdf	pointer to a ebdf_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the correct file position)
 *
 * Read all channels related field in the header and setup the channels
 * accordingly. set the channels for no scaling and inmemtype = infiletype
 */
static int ebdf_read_channels_header(struct ebdf_file* ebdf, FILE* file)
{
	struct xdfch* ch;
	int ival;
	unsigned int offset = 0;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_ebdfch(ch)->label, 16))
			return -1;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_ebdfch(ch)->transducter, 80))
			return -1;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_ebdfch(ch)->unit, 8))
			return -1;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->physical_mm[0] = (double)ival;
	}

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->physical_mm[1] = (double)ival;
	}

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->digital_mm[0] = (double)ival;
	}

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->digital_mm[1] = (double)ival;
	}

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_ebdfch(ch)->prefiltering, 80))
			return -1;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_int_field(file, (int*)&(ebdf->xdf.ns_per_rec), 8))
			return -1;

	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_ebdfch(ch)->reserved, 32))
			return -1;

	// Guess the infile type and setup the offsets
	// (assuming only one array of packed values)
	for (ch = ebdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (ebdf->xdf.ops->type == XDF_BDF)
			ch->infiletype = ch->inmemtype = XDFINT24;
		else
			ch->infiletype = ch->inmemtype = XDFINT16;
		ch->digital_inmem = 1;
		ch->offset = offset;
		offset += xdf_get_datasize(ch->inmemtype);
	}


	return 0;
}

/* \param xdf	pointer to an xdf file with XDF_READ mode
 *
 * EDF/BDF METHOD.
 * Read the header and allocate the channels
 * It creates from the file a stream that will be used to easily interpret
 * and parse the header.
 */
static int ebdf_read_header(struct xdf* xdf)
{
	int retval = -1;
	unsigned int i;
	struct xdfch** curr = &(xdf->channels);
	struct ebdf_file* ebdf = get_ebdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "r");
	if (!file)
		return -1;

	if (ebdf_read_file_header(ebdf, file))
		goto exit;

	// Allocate all the channels
	for (i=0; i<xdf->numch; i++) {
		if (!(*curr = xdf_alloc_channel(xdf)))
			goto exit;
		curr = &((*curr)->next);
	}

	if (ebdf_read_channels_header(ebdf, file))
		goto exit;
	
	retval = 0;
exit:
	fclose(file);
	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
}


/* \param xdf	pointer to an xdf file
 *
 * BDF METHOD.
 * Free all memory allocated to the structure
 */
static void ebdf_free_file(struct xdf* xdf)
{
	free(get_ebdf(xdf));
}



/* \param xdf	pointer to an xdf file with XDF_WRITE mode
 *
 * BDF/EDF METHOD.
 * Write the number record in the header
 */
static int ebdf_complete_file(struct xdf* xdf)
{
	int retval = 0;
	char numrecstr[9];

	// Write the number of records in the header
	snprintf(numrecstr, 9, "%-8i", xdf->nrecord);
	if ( (lseek(xdf->fd, NUMREC_FIELD_LOC, SEEK_SET) < 0)
	    || (write(xdf->fd, numrecstr, 8) < 0) )
		retval = -1;

	return retval;
}

