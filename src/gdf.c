/*
	Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
	Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <float.h>

#include "xdfio.h"
#include "xdffile.h"
#include "xdftypes.h"
#include "streamops.h"

#include "gdf.h"

/******************************************************
 *               GDF specific declaration             *
 ******************************************************/

// GDF methods declaration
static int gdf_set_channel(struct xdfch*, enum xdffield,
                           union optval, int);
static int gdf_get_channel(const struct xdfch*, enum xdffield,
                           union optval*, int);
static int gdf_set_conf(struct xdf*, enum xdffield, union optval, int); 
static int gdf_get_conf(const struct xdf*, enum xdffield,
                        union optval*, int); 
static int gdf_write_header(struct xdf*);
static int gdf_read_header(struct xdf*);
static int gdf_complete_file(struct xdf*);

// GDF channel structure
struct gdf_channel {
	struct xdfch xdfch;
	char label[17];
	char transducter[81];
	char unit[9];
	char prefiltering[81];
	char reserved[33];
};

// GDF file structure
struct gdf_file {
	struct xdf xdf;
	struct gdf_channel default_gdfch;
	char subjstr[81];
	char recstr[81];
	time_t rectime;
	uint64_t epid, lid, tid;
	char sn[21];
};

#define NUMREC_FIELD_LOC 236

#define get_gdf(xdf_p) \
	((struct gdf_file*)(((char*)(xdf_p))-offsetof(struct gdf_file, xdf)))
#define get_gdfch(xdfch_p) 				\
	((struct gdf_channel*)(((char*)(xdfch_p))	\
		- offsetof(struct gdf_channel, xdfch)))

static const uint32_t gdf_types[XDF_NUM_DATA_TYPES] = {
	[XDFINT8] = 1,
	[XDFUINT8] = 2,
	[XDFINT16] = 3,
	[XDFUINT16] = 4,
	[XDFINT24] = 279,
	[XDFUINT24] = 525,
	[XDFINT32] = 5,
	[XDFUINT32] = 6,
	[XDFFLOAT] = 16,
	[XDFDOUBLE] = 17,
	[XDFINT64] = 7,
	[XDFUINT64] = 8
};
#define gdf_tp_len (sizeof(gdf_types)/sizeof(gdf_types[0]))

/******************************************************
 *            GDF type definition declaration         *
 ******************************************************/
static const enum xdffield gdf_ch_supported_fields[] = {
	XDF_CF_ARRTYPE,
	XDF_CF_PMIN,
	XDF_CF_PMAX,
	XDF_CF_STOTYPE,
	XDF_CF_DMIN,
	XDF_CF_DMAX,
	XDF_CF_ARRDIGITAL,
	XDF_CF_ARROFFSET,
	XDF_CF_ARRINDEX,
	XDF_CF_LABEL,
	XDF_CF_UNIT,
	XDF_CF_TRANSDUCTER,
	XDF_CF_PREFILTERING,
	XDF_CF_RESERVED,
	XDF_NOF
};

static const enum xdffield gdf_file_supported_fields[] = {
	XDF_F_REC_DURATION,
	XDF_F_REC_NSAMPLE,
	XDF_F_SUBJ_DESC,
	XDF_F_SESS_DESC,
	XDF_F_RECTIME,
	XDF_NOF
};

static const struct format_operations gdf_ops = {
	.set_channel = gdf_set_channel,
	.get_channel = gdf_get_channel,
	.set_conf = gdf_set_conf,
	.get_conf = gdf_get_conf,
	.write_header = gdf_write_header,
	.read_header = gdf_read_header,
	.complete_file = gdf_complete_file,
	.type = XDF_GDF,
	.supported_type = {
		[XDFUINT8] = true,
		[XDFINT8] = true,
		[XDFINT16] = true,
		[XDFUINT16] = true,
		[XDFUINT24] = true,
		[XDFINT24] = true,
		[XDFUINT32] = true,
		[XDFINT32] = true,
		[XDFUINT64] = true,
		[XDFINT64] = true,
		[XDFFLOAT] = true,
		[XDFDOUBLE] = true
	},
	.choff = offsetof(struct gdf_channel, xdfch),
	.chlen = sizeof(struct gdf_channel),
	.fileoff = offsetof(struct gdf_file, xdf),
	.filelen = sizeof(struct gdf_file),
	.chfields = gdf_ch_supported_fields,
	.filefields = gdf_file_supported_fields
};

static const unsigned char gdf_magickey[] = 
	{'G','D','F',' ','1','.','0','0'};

static const struct gdf_channel gdfch_def = {
	.xdfch = {.infiletype = XDFFLOAT}
};

/******************************************************
 *            GDF file support implementation         *
 ******************************************************/

/* Allocate a GDF file
 */
XDF_LOCAL struct xdf* xdf_alloc_gdffile(void)
{
	struct gdf_file* gdf;

	gdf = calloc(1, sizeof(*gdf));
	if (!gdf)
		return NULL;

	gdf->xdf.ops = &gdf_ops;
	gdf->xdf.defaultch = &(gdf->default_gdfch.xdfch);

	// Set good default for the file format
	memcpy(&(gdf->default_gdfch), &gdfch_def, sizeof(gdfch_def));
	gdf->default_gdfch.xdfch.owner = &(gdf->xdf);
	gdf->xdf.rec_duration = 1.0;
	gdf->rectime = time(NULL);
	
	return &(gdf->xdf);
}


/* \param magickey	pointer to key identifying a type of file
 *
 * Returns 1 if the supplied magickey corresponds to a GDF file
 */
XDF_LOCAL int xdf_is_gdffile(const unsigned char* magickey)
{
	if (memcmp(magickey, gdf_magickey, sizeof(gdf_magickey)) == 0)
		return 1;
	return 0;
}


/******************************************************
 *             GDF methods implementation         *
 ******************************************************/

/* \param ch	pointer to a xdf channel (GDF) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to be set
 *
 * GDF METHOD.
 * Change the configuration field value of the channel according to val
 */
static int gdf_set_channel(struct xdfch* ch, enum xdffield field,
                            union optval val, int prevretval)
{
	struct gdf_channel* gdfch = get_gdfch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		strncpy(gdfch->label, val.str, sizeof(gdfch->label)-1);
	else if (field == XDF_CF_UNIT)
		strncpy(gdfch->unit, val.str, sizeof(gdfch->unit)-1);
	else if (field == XDF_CF_TRANSDUCTER)
		strncpy(gdfch->transducter, val.str, sizeof(gdfch->transducter)-1);
	else if (field == XDF_CF_PREFILTERING)
		strncpy(gdfch->prefiltering, val.str, sizeof(gdfch->prefiltering)-1);
	else if (field == XDF_CF_RESERVED)
		strncpy(gdfch->reserved, val.str, sizeof(gdfch->reserved)-1);
	else
		retval = prevretval;

	return retval;
}


/* \param ch	pointer to a xdf channel (GDF underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF METHOD.
 * Get the configuration field value of the channel and assign it to val
 */
static int gdf_get_channel(const struct xdfch* ch, enum xdffield field,
                            union optval *val, int prevretval)
{
	struct gdf_channel* gdfch = get_gdfch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		val->str = gdfch->label;
	else if (field == XDF_CF_UNIT)
		val->str = gdfch->unit;
	else if (field == XDF_CF_TRANSDUCTER)
		val->str = gdfch->transducter;
	else if (field == XDF_CF_PREFILTERING)
		val->str = gdfch->prefiltering;
	else if (field == XDF_CF_RESERVED)
		val->str = gdfch->reserved;
	else
		retval = prevretval;

	return retval;
}


/* \param xdf	pointer to a xdf file (GDF) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to set
 *
 * GDF METHOD.
 * Change the configuration field value according to val
 */
static int gdf_set_conf(struct xdf* xdf, enum xdffield field, 
                         union optval val, int prevretval)
{
	struct gdf_file* gdf = get_gdf(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		strncpy(gdf->subjstr, val.str, sizeof(gdf->subjstr)-1);
	else if (field == XDF_F_SESS_DESC)
		strncpy(gdf->recstr, val.str, sizeof(gdf->recstr)-1);
	else if (field == XDF_F_RECTIME)
		gdf->rectime = val.ts;
	else
		retval = prevretval;

	return retval;
}

/* \param xdf	pointer to a xdf file (GDF underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF METHOD.
 * Get the configuration field value and assign it to val
 */
static int gdf_get_conf(const struct xdf* xdf, enum xdffield field,
                        union optval *val, int prevretval)
{
	struct gdf_file* gdf = get_gdf(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		val->str = gdf->subjstr;
	else if (field == XDF_F_SESS_DESC)
		val->str = gdf->recstr;
	else if (field == XDF_F_RECTIME)
		val->ts = gdf->rectime;
	else
		retval = prevretval;

	return retval;
}


static void convert_recduration(double len, uint32_t ratio[2])
{
	if (len >= 1.0) {
		ratio[0] = len;
		ratio[1] = 1;
	} else {
		ratio[0] = 1;
		ratio[1] = 1.0/len;
	}
}


/* \param gdf	pointer to a gdf_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of the file)
 *
 * Write the general GDF file header. Write -1 in the number of records
 * field. The real value is written after the transfer is finished
 */
static int gdf_write_file_header(struct gdf_file* gdf, FILE* file)
{
	char timestring[17];
 	uint32_t nch, recduration[2];
	int64_t nrec = -1, hdrsize;
	struct tm ltm;

	convert_recduration(gdf->xdf.rec_duration, recduration);
	nrec = gdf->xdf.nrecord;
	nch = gdf->xdf.numch;
 	hdrsize = 256 + (gdf->xdf.numch)*256;

	// format time string (drop centisec precision)
	localtime_r(&(gdf->rectime), &ltm);
	strftime(timestring, sizeof(timestring),"%Y%m%d%H%M%S00", &ltm);

	// Write data format identifier
	if (fwrite(gdf_magickey, 8, 1, file) < 1)
		return -1;

	// Write all the general file header
	if ((fprintf(file, "%-80.80s%-80.80s%16s", 
			  gdf->subjstr,
			  gdf->recstr,
			  timestring) < 0)
	   || write64bval(file, 1, &hdrsize)
	   || write64bval(file, 1, &(gdf->epid))
	   || write64bval(file, 1, &(gdf->lid))
	   || write64bval(file, 1, &(gdf->tid))
	   || (fprintf(file, "%-20.20s", gdf->sn) < 0)
	   || write64bval(file, 1, &nrec)
	   || write32bval(file, 2, recduration)
	   || write32bval(file, 1, &nch))
		return -1;
	
	return 0;
}

/* \param bdf	pointer to a gdf_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of channel fields)
 *
 * Write the GDF channels related fields in the header.
 */
static int gdf_write_channels_header(struct gdf_file* gdf, FILE* file)
{
	struct xdfch* ch;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_gdfch(ch)->label) < 0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_gdfch(ch)->transducter)<0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8.8s", get_gdfch(ch)->unit) < 0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val = ch->digital_mm[0];
		if (write64bval(file, 1, &val))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val = ch->digital_mm[1];
		if (write64bval(file, 1, &val))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_gdfch(ch)->prefiltering) < 0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t nsprec = gdf->xdf.ns_per_rec;
		if (write32bval(file, 1, &nsprec))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t type = gdf_types[ch->infiletype];
		if (write32bval(file, 1, &type))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-32.32s", get_gdfch(ch)->reserved) < 0)
			return -1;

	return 0;
}


/* \param xdf	pointer to an xdf (GDF) file with XDF_WRITE mode
 *
 * GDF METHOD.
 * Write the general file header and channels fields
 * It creates from the file a stream that will be used to easily format
 * the header fields.
 */
static int gdf_write_header(struct xdf* xdf)
{
	int retval = 0;
	struct gdf_file* gdf = get_gdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "wb");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( gdf_write_file_header(gdf, file)
	    || gdf_write_channels_header(gdf, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);

	return retval;
}


/* \param bdf	pointer to a gdf_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the beginning of the file)
 *
 * Read the general GDF file header
 */
static int gdf_read_file_header(struct gdf_file* gdf, FILE* file)
{
	char timestring[17];
	uint32_t nch, recduration[2];
	int64_t nrec, hdrsize;
	struct tm ltm = {.tm_isdst = -1};

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, gdf->subjstr, 80)
	   || read_string_field(file, gdf->recstr, 80)
	   || read_string_field(file, timestring, 16)
	   || read64bval(file, 1, &hdrsize)
	   || read64bval(file, 1, &(gdf->epid))
	   || read64bval(file, 1, &(gdf->lid))
	   || read64bval(file, 1, &(gdf->tid))
	   || read_string_field(file, gdf->sn, 20)
	   || read64bval(file, 1, &nrec)
	   || read32bval(file, 2, recduration)
	   || read32bval(file, 1, &nch) )
		return -1;

	gdf->xdf.rec_duration = (double)recduration[0] 
	                      / (double)recduration[1];
	gdf->xdf.hdr_offset = hdrsize;
	gdf->xdf.nrecord = nrec;
	gdf->xdf.numch = nch;

	// format time string (drop centisec precision)
	sscanf(timestring, "%4i%2i%2i%2i%2i%2i", 
	                    &(ltm.tm_year), &(ltm.tm_mon), &(ltm.tm_mday),
	                    &(ltm.tm_hour), &(ltm.tm_min), &(ltm.tm_sec));
	ltm.tm_mon--;
	// REMINDER: tm_year is number of years since 1900
	ltm.tm_year -= 1900;
	gdf->rectime = mktime(&ltm);

	return 0;
}


static enum xdftype get_xdfch_type(uint32_t gdftype)
{
	unsigned int i;

	/* Treat gdf char as xdf uint8 */
	if (gdftype == 0)
		return XDFUINT8;

	for (i=0; i<gdf_tp_len; i++) {
		if (gdf_types[i] == gdftype)
			return i;
	}

	return -1;
}


/* \param bdf	pointer to a gdf_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the correct file position)
 *
 * Read all channels related field in the header and setup the channels
 * accordingly. set the channels for no scaling and inmemtype = infiletype
 */
static int gdf_read_channels_header(struct gdf_file* gdf, FILE* file)
{
	struct xdfch* ch;
	int i;
	unsigned int offset = 0;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->label, 16))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->transducter, 80))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->unit, 8))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read64bval(file, 1, &(ch->physical_mm[0])))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read64bval(file, 1, &(ch->physical_mm[1])))
			return -1;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val;
		if (read64bval(file, 1, &val))
			return -1;
		ch->digital_mm[0] = val;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val;
		if (read64bval(file, 1, &val))
			return -1;
		ch->digital_mm[1] = val;
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->prefiltering, 80))
			return -1;

	i = 0;
	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		uint32_t val;
		if (read32bval(file, 1, &val))
			return -1;
		if (i++ == 0)
			ch->owner->ns_per_rec = val;
		else if (ch->owner->ns_per_rec != val) {
			errno = EPERM;
			return -1;
		}
	}

	// Guess the infile type and setup the offsets
	// (assuming only one array of packed values)
	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) {
		uint32_t type;
		int xdftype;
		if (read32bval(file, 1, &(type)))
			return -1;
		if ((xdftype = get_xdfch_type(type)) == -1) {
			errno = EILSEQ;
			return -1;
		}
		ch->inmemtype = ch->infiletype = xdftype;
		ch->digital_inmem = 1;
		ch->offset = offset;
		offset += xdf_get_datasize(ch->inmemtype);
	}

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->reserved, 32))
			return -1;

	return 0;
}

/* \param xdf	pointer to an xdf file with XDF_READ mode
 *
 * GDF METHOD.
 * Read the header and allocate the channels
 * It creates from the file a stream that will be used to easily interpret
 * and parse the header.
 */
static int gdf_read_header(struct xdf* xdf)
{
	int retval = -1;
	unsigned int i;
	struct xdfch** curr = &(xdf->channels);
	struct gdf_file* gdf = get_gdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "rb");
	if (!file)
		return -1;

	if (gdf_read_file_header(gdf, file))
		goto exit;

	// Allocate all the channels
	for (i=0; i<xdf->numch; i++) {
		if (!(*curr = xdf_alloc_channel(xdf)))
			goto exit;
		curr = &((*curr)->next);
	}

	if (gdf_read_channels_header(gdf, file))
		goto exit;

	
	retval = 0;
exit:
	fclose(file);
	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
}


/* \param xdf	pointer to an xdf file with XDF_WRITE mode
 *
 * GDF METHOD.
 * Write the number record in the header
 */
static int gdf_complete_file(struct xdf* xdf)
{
	int retval = 0;
	int64_t numrec = xdf->nrecord;

	// Write the number of records in the header
	if ( (lseek(xdf->fd, NUMREC_FIELD_LOC, SEEK_SET) < 0)
	    || (write(xdf->fd, &numrec, sizeof(numrec)) < 0) )
		retval = -1;

	return retval;
}

