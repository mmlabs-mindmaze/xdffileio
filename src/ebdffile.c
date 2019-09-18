/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <mmsysio.h>

#include "ebdf.h"
#include "common.h"
#include "streamops.h"
#include "xdffile.h"
#include "xdfio.h"
#include "xdftypes.h"
/******************************************************
 *               EDF/BDF specific declaration             *
 ******************************************************/

// EDF/BDF methods declaration
static int ebdf_set_channel(struct xdfch*, enum xdffield,
                           union optval, int);
static int ebdf_get_channel(const struct xdfch*, enum xdffield,
                           union optval*, int);
static int ebdf_set_conf(struct xdf*, enum xdffield, union optval, int); 
static int ebdf_get_conf(const struct xdf*, enum xdffield,
                        union optval*, int); 
static int ebdf_write_header(struct xdf*);
static int ebdf_read_header(struct xdf*);
static int ebdf_complete_file(struct xdf*);

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
	time_t rectime;
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
static const enum xdffield ch_supported_fields[] = {
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

static const enum xdffield file_supported_fields[] = {
	XDF_F_REC_DURATION,
	XDF_F_REC_NSAMPLE,
	XDF_F_SUBJ_DESC,
	XDF_F_SESS_DESC,
	XDF_F_RECTIME,
	XDF_NOF
};

static const struct format_operations bdf_ops = {
	.set_channel = ebdf_set_channel,
	.get_channel = ebdf_get_channel,
	.set_conf = ebdf_set_conf,
	.get_conf = ebdf_get_conf,
	.write_header = ebdf_write_header,
	.read_header = ebdf_read_header,
	.complete_file = ebdf_complete_file,
	.type = XDF_BDF,
	.supported_type = {[XDFINT24] = true},
	.choff = offsetof(struct ebdf_channel, xdfch),
	.chlen = sizeof(struct ebdf_channel),
	.fileoff = offsetof(struct ebdf_file, xdf),
	.filelen = sizeof(struct ebdf_file),
	.chfields = ch_supported_fields,
	.filefields = file_supported_fields
};

static const unsigned char bdf_magickey[] = 
	{255, 'B', 'I', 'O', 'S', 'E', 'M', 'I'};

static const struct ebdf_channel bdfch_def = {
	.xdfch = { .infiletype = XDFINT24 }
};

static const struct format_operations edf_ops = {
	.set_channel = ebdf_set_channel,
	.get_channel = ebdf_get_channel,
	.set_conf = ebdf_set_conf,
	.get_conf = ebdf_get_conf,
	.write_header = ebdf_write_header,
	.read_header = ebdf_read_header,
	.complete_file = ebdf_complete_file,
	.type = XDF_EDF,
	.supported_type = {[XDFINT16] = true},
	.choff = offsetof(struct ebdf_channel, xdfch),
	.chlen = sizeof(struct ebdf_channel),
	.fileoff = offsetof(struct ebdf_file, xdf),
	.filelen = sizeof(struct ebdf_file),
	.chfields = ch_supported_fields,
	.filefields = file_supported_fields
};

static const unsigned char edf_magickey[] = 
	{'0', ' ', ' ', ' ', ' ', ' ', ' ', ' '};

static const struct ebdf_channel edfch_def = {
	.xdfch = { .infiletype = XDFINT16 }
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
	ebdf->rectime = time(NULL);
	
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
	else if (field == XDF_F_RECTIME)
		ebdf->rectime = val.d;
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
	else if (field == XDF_F_RECTIME)
		val->d = ebdf->rectime;
	else
		retval = prevretval;

	return retval;
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
	struct tm ltm;

	mkey = (type == XDF_BDF) ? bdf_magickey : edf_magickey;

	// format time string
	localtime_r(&(ebdf->rectime), &ltm);
	strftime(timestring, sizeof(timestring),"%d.%m.%y%H.%M.%S", &ltm);

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
	FILE* file = fdopen(mm_dup(xdf->fd), "wb");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( ebdf_write_file_header(bdf, file)
	    || ebdf_write_channels_header(bdf, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	mm_seek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);

	return retval;
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
	struct tm ltm = {.tm_isdst = -1};

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, bdf->subjstr, 80)
	   || read_string_field(file, bdf->recstr, 80)
	   || read_string_field(file, timestring, 16)
	   || read_int_field(file, &hdrsize, 8)
	   || read_string_field(file, type, 44)
	   || read_int_field(file, &(bdf->xdf.nrecord), 8)
	   || read_int_field(file, &recdur, 8)
	   || read_int_field(file, (int*)&(bdf->xdf.numch), 4) )
		return -1;

	bdf->xdf.rec_duration = (double)recdur;
	bdf->xdf.hdr_offset = hdrsize;

	// format time string
	sscanf(timestring, "%2i.%2i.%2i%2i.%2i.%2i", 
	                    &(ltm.tm_mday), &(ltm.tm_mon), &(ltm.tm_year),
	                    &(ltm.tm_hour), &(ltm.tm_min), &(ltm.tm_sec));
	ltm.tm_mon--;
	// REMINDER: tm_year is number of years since 1900
	if (ltm.tm_year < 80)
		ltm.tm_year += 100;
	bdf->rectime = mktime(&ltm);

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
	struct ebdf_file* ebdf = get_ebdf(xdf);
	FILE* file = fdopen(mm_dup(xdf->fd), "rb");
	if (!file)
		return -1;

	if (ebdf_read_file_header(ebdf, file))
		goto exit;

	// Allocate all the channels
	for (i=0; i<xdf->numch; i++) {
		if (xdf_alloc_channel(xdf) == NULL)
			goto exit;
	}

	if (ebdf_read_channels_header(ebdf, file))
		goto exit;

	
	retval = 0;
exit:
	fclose(file);
	mm_seek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
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
	if ( (mm_seek(xdf->fd, NUMREC_FIELD_LOC, SEEK_SET) < 0)
	    || (mm_write(xdf->fd, numrecstr, 8) < 0) )
		retval = -1;

	return retval;
}

