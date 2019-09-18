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
#include <stdarg.h>
#include <float.h>
#include <mmsysio.h>

#include "common.h"
#include "streamops.h"
#include "xdfevent.h"
#include "xdffile.h"
#include "xdfio.h"
#include "xdftypes.h"

#include "gdf1.h"

/******************************************************
 *               GDF1 specific declaration             *
 ******************************************************/

// GDF1 methods declaration
static int gdf1_set_channel(struct xdfch*, enum xdffield,
                           union optval, int);
static int gdf1_get_channel(const struct xdfch*, enum xdffield,
                           union optval*, int);
static int gdf1_set_conf(struct xdf*, enum xdffield, union optval, int); 
static int gdf1_get_conf(const struct xdf*, enum xdffield,
                        union optval*, int); 
static int gdf1_write_header(struct xdf*);
static int gdf1_read_header(struct xdf*);
static int gdf1_complete_file(struct xdf*);

// GDF1 channel structure
struct gdf1_channel {
	struct xdfch xdfch;
	char label[17];
	char transducter[81];
	char unit[9];
	char prefiltering[81];
	char reserved[33];
};

// GDF1 file structure
struct gdf1_file {
	struct xdf xdf;
	struct gdf1_channel default_gdf1ch;
	char subjstr[81];
	char recstr[81];
	time_t rectime;
	uint64_t epid, lid, tid;
	char sn[21];
	unsigned int version;
};

#define NUMREC_FIELD_LOC 236

#define get_gdf1(xdf_p) \
	((struct gdf1_file*)(((char*)(xdf_p))-offsetof(struct gdf1_file, xdf)))
#define get_gdf1ch(xdfch_p) 				\
	((struct gdf1_channel*)(((char*)(xdfch_p))	\
		- offsetof(struct gdf1_channel, xdfch)))

static const uint32_t gdf1_types[XDF_NUM_DATA_TYPES] = {
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
#define gdf1_tp_len (sizeof(gdf1_types)/sizeof(gdf1_types[0]))

/******************************************************
 *            GDF1 type definition declaration         *
 ******************************************************/
static const enum xdffield gdf1_ch_supported_fields[] = {
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

static const enum xdffield gdf1_file_supported_fields[] = {
	XDF_F_REC_DURATION,
	XDF_F_REC_NSAMPLE,
	XDF_F_SUBJ_DESC,
	XDF_F_SESS_DESC,
	XDF_F_RECTIME,
	XDF_NOF
};

static const struct format_operations gdf1_ops = {
	.set_channel = gdf1_set_channel,
	.get_channel = gdf1_get_channel,
	.set_conf = gdf1_set_conf,
	.get_conf = gdf1_get_conf,
	.write_header = gdf1_write_header,
	.read_header = gdf1_read_header,
	.complete_file = gdf1_complete_file,
	.type = XDF_GDF1,
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
	.choff = offsetof(struct gdf1_channel, xdfch),
	.chlen = sizeof(struct gdf1_channel),
	.fileoff = offsetof(struct gdf1_file, xdf),
	.filelen = sizeof(struct gdf1_file),
	.chfields = gdf1_ch_supported_fields,
	.filefields = gdf1_file_supported_fields
};

static const struct gdf1_channel gdf1ch_def = {
	.xdfch = {.infiletype = XDFFLOAT}
};

/******************************************************
 *            GDF1 file support implementation         *
 ******************************************************/

/* Allocate a GDF1 file
 */
LOCAL_FN struct xdf* xdf_alloc_gdf1file(void)
{
	struct gdf1_file* gdf1;
	struct eventtable* table;

	gdf1 = calloc(1, sizeof(*gdf1));
	table = create_event_table();
	if (gdf1 == NULL || table == NULL) {
		free(gdf1);
		destroy_event_table(table);
		return NULL;
	}

	gdf1->xdf.ops = &gdf1_ops;
	gdf1->xdf.defaultch = &(gdf1->default_gdf1ch.xdfch);
	gdf1->xdf.table = table;

	// Set good default for the file format
	memcpy(&(gdf1->default_gdf1ch), &gdf1ch_def, sizeof(gdf1ch_def));
	gdf1->default_gdf1ch.xdfch.owner = &(gdf1->xdf);
	gdf1->xdf.rec_duration = 1.0;
	gdf1->version = 0;
	gdf1->rectime = time(NULL);
	
	
	return &(gdf1->xdf);
}


/* \param magickey	pointer to key identifying a type of file
 *
 * Returns 1 if the supplied magickey corresponds to a GDF1 file
 */
LOCAL_FN int xdf_is_gdf1file(const unsigned char* magickey)
{
	char key[9] = {0};
	unsigned int version;

	strncpy(key, (const char*)magickey, 8);
	if (sscanf(key, "GDF 1.%u", &version) == 1)
		return 1;
	return 0;
}


/******************************************************
 *             GDF1 methods implementation         *
 ******************************************************/

/* \param ch	pointer to a xdf channel (GDF1) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to be set
 *
 * GDF1 METHOD.
 * Change the configuration field value of the channel according to val
 */
static int gdf1_set_channel(struct xdfch* ch, enum xdffield field,
                            union optval val, int prevretval)
{
	struct gdf1_channel* gdf1ch = get_gdf1ch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		strncpy(gdf1ch->label, val.str, sizeof(gdf1ch->label)-1);
	else if (field == XDF_CF_UNIT)
		strncpy(gdf1ch->unit, val.str, sizeof(gdf1ch->unit)-1);
	else if (field == XDF_CF_TRANSDUCTER)
		strncpy(gdf1ch->transducter, val.str, sizeof(gdf1ch->transducter)-1);
	else if (field == XDF_CF_PREFILTERING)
		strncpy(gdf1ch->prefiltering, val.str, sizeof(gdf1ch->prefiltering)-1);
	else if (field == XDF_CF_RESERVED)
		strncpy(gdf1ch->reserved, val.str, sizeof(gdf1ch->reserved)-1);
	else
		retval = prevretval;

	return retval;
}


/* \param ch	pointer to a xdf channel (GDF1 underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF1 METHOD.
 * Get the configuration field value of the channel and assign it to val
 */
static int gdf1_get_channel(const struct xdfch* ch, enum xdffield field,
                            union optval *val, int prevretval)
{
	struct gdf1_channel* gdf1ch = get_gdf1ch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		val->str = gdf1ch->label;
	else if (field == XDF_CF_UNIT)
		val->str = gdf1ch->unit;
	else if (field == XDF_CF_TRANSDUCTER)
		val->str = gdf1ch->transducter;
	else if (field == XDF_CF_PREFILTERING)
		val->str = gdf1ch->prefiltering;
	else if (field == XDF_CF_RESERVED)
		val->str = gdf1ch->reserved;
	else
		retval = prevretval;

	return retval;
}


/* \param xdf	pointer to a xdf file (GDF1) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to set
 *
 * GDF1 METHOD.
 * Change the configuration field value according to val
 */
static int gdf1_set_conf(struct xdf* xdf, enum xdffield field, 
                         union optval val, int prevretval)
{
	struct gdf1_file* gdf1 = get_gdf1(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		strncpy(gdf1->subjstr, val.str, sizeof(gdf1->subjstr)-1);
	else if (field == XDF_F_SESS_DESC)
		strncpy(gdf1->recstr, val.str, sizeof(gdf1->recstr)-1);
	else if (field == XDF_F_RECTIME)
		gdf1->rectime = val.d;
	else
		retval = prevretval;

	return retval;
}

/* \param xdf	pointer to a xdf file (GDF1 underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF1 METHOD.
 * Get the configuration field value and assign it to val
 */
static int gdf1_get_conf(const struct xdf* xdf, enum xdffield field,
                        union optval *val, int prevretval)
{
	struct gdf1_file* gdf1 = get_gdf1(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		val->str = gdf1->subjstr;
	else if (field == XDF_F_SESS_DESC)
		val->str = gdf1->recstr;
	else if (field == XDF_F_RECTIME)
		val->d = gdf1->rectime;
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


/* \param gdf1	pointer to a gdf1_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of the file)
 *
 * Write the general GDF1 file header. Write -1 in the number of records
 * field. The real value is written after the transfer is finished
 */
static int gdf1_write_file_header(struct gdf1_file* gdf1, FILE* file)
{
	char timestring[17];
 	uint32_t nch, recduration[2];
	int64_t nrec = -1, hdrsize;
	struct tm ltm;
	char key[9];

	convert_recduration(gdf1->xdf.rec_duration, recduration);
	nrec = gdf1->xdf.nrecord;
	nch = gdf1->xdf.numch;
 	hdrsize = 256 + (gdf1->xdf.numch)*256;

	// format time string (drop centisec precision)
	localtime_r(&(gdf1->rectime), &ltm);
	strftime(timestring, sizeof(timestring),"%Y%m%d%H%M%S00", &ltm);

	// Write data format identifier
	snprintf(key, sizeof(key), "GDF 1.%02u", gdf1->version);
	if (fwrite(key, 8, 1, file) < 1)
		return -1;

	// Write all the general file header
	if ((fprintf(file, "%-80.80s%-80.80s%16s", 
			  gdf1->subjstr,
			  gdf1->recstr,
			  timestring) < 0)
	   || write64bval(file, 1, &hdrsize)
	   || write64bval(file, 1, &(gdf1->epid))
	   || write64bval(file, 1, &(gdf1->lid))
	   || write64bval(file, 1, &(gdf1->tid))
	   || (fprintf(file, "%-20.20s", gdf1->sn) < 0)
	   || write64bval(file, 1, &nrec)
	   || write32bval(file, 2, recduration)
	   || write32bval(file, 1, &nch))
		return -1;
	
	gdf1->xdf.hdr_offset = hdrsize;
	return 0;
}

/* \param bdf	pointer to a gdf1_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of channel fields)
 *
 * Write the GDF1 channels related fields in the header.
 */
static int gdf1_write_channels_header(struct gdf1_file* gdf1, FILE* file)
{
	struct xdfch* ch;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_gdf1ch(ch)->label) < 0)
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_gdf1ch(ch)->transducter)<0)
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8.8s", get_gdf1ch(ch)->unit) < 0)
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val = ch->digital_mm[0];
		if (write64bval(file, 1, &val))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val = ch->digital_mm[1];
		if (write64bval(file, 1, &val))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_gdf1ch(ch)->prefiltering) < 0)
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t nsprec = gdf1->xdf.ns_per_rec;
		if (write32bval(file, 1, &nsprec))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t type = gdf1_types[ch->infiletype];
		if (write32bval(file, 1, &type))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-32.32s", get_gdf1ch(ch)->reserved) < 0)
			return -1;

	return 0;
}


/* \param xdf	pointer to an xdf (GDF1) file with XDF_WRITE mode
 *
 * GDF1 METHOD.
 * Write the general file header and channels fields
 * It creates from the file a stream that will be used to easily format
 * the header fields.
 */
static int gdf1_write_header(struct xdf* xdf)
{
	int retval = 0;
	struct gdf1_file* gdf1 = get_gdf1(xdf);
	FILE* file = fdopen(mm_dup(xdf->fd), "wb");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( gdf1_write_file_header(gdf1, file)
	    || gdf1_write_channels_header(gdf1, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	mm_seek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);

	return retval;
}


static
int gdf1_setup_events(struct eventtable* table, double fs, uint8_t* mode,
                     uint32_t *pos, uint16_t* code,
		     uint16_t *ch, uint32_t* dur)
{
	unsigned int i, nevent = table->nevent;
	int codeval, use_extevt = 0;
	unsigned int chval;
	const char* desc;
	struct xdfevent* evt;

	for (i=0; i<nevent; i++) {
		evt = get_event(table, i);
		pos[i] = fs * evt->onset;
		if (evt->duration > 0) {
			dur[i] = fs * evt->duration;
			use_extevt = 1;
		} else
			dur[i] = 0;
		get_event_entry(table, evt->evttype, &codeval, &desc);
		code[i] = codeval;
		ch[i] = 0;
		if (desc != NULL && sscanf(desc, "ch:%u", &chval) >= 1) {
			ch[i] = chval;
			use_extevt = 1;
		} else
			ch[i] = 0;
	}
	*mode = use_extevt ? 3 : 1;
	return 0;
}


static
int gdf1_write_event_table(struct gdf1_file* gdf1, FILE* file)
{
	struct eventtable* table = gdf1->xdf.table;
	int retcode = 0;
	uint8_t mode, fs24[3];
	double fs = gdf1->xdf.ns_per_rec / gdf1->xdf.rec_duration;
	uint32_t nevt = table->nevent, *onset = NULL, *dur = NULL;
	uint16_t *code = NULL, *ch = NULL;

	if (nevt == 0)
		return 0;

	fs24[LSB24] = (uint32_t)fs & 0x000000FF;
	fs24[    1] = ((uint32_t)fs & 0x0000FF00) / 256;
	fs24[MSB24] = ((uint32_t)fs & 0x00FF0000) / 65536;
	
	onset = malloc(nevt*sizeof(*onset));
	code = malloc(nevt*sizeof(*code));
	ch = malloc(nevt*sizeof(*ch));
	dur = malloc(nevt*sizeof(*dur));
	if (onset == NULL || code == NULL || ch == NULL || dur == NULL)
		retcode = -1;

	gdf1_setup_events(table, fs, &mode, onset, code, ch, dur);

	if (retcode
	  || write8bval(file, 1, &mode)
	  || write24bval(file, 1, fs24)
	  || write32bval(file, 1, &nevt)
	  || write32bval(file, nevt, onset)
	  || write16bval(file, nevt, code)
	  || (mode == 3 && write16bval(file, nevt, ch))
	  || (mode == 3 && write16bval(file, nevt, dur)))
		retcode = -1;

	free(onset);
	free(code);
	free(ch);
	free(dur);
	return retcode;
}


/* \param bdf	pointer to a gdf1_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the beginning of the file)
 *
 * Read the general GDF1 file header
 */
static int gdf1_read_file_header(struct gdf1_file* gdf1, FILE* file)
{
	char timestring[17];
	uint32_t nch, recduration[2];
	int64_t nrec, hdrsize;
	struct tm ltm = {.tm_isdst = -1};

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, gdf1->subjstr, 80)
	   || read_string_field(file, gdf1->recstr, 80)
	   || read_string_field(file, timestring, 16)
	   || read64bval(file, 1, &hdrsize)
	   || read64bval(file, 1, &(gdf1->epid))
	   || read64bval(file, 1, &(gdf1->lid))
	   || read64bval(file, 1, &(gdf1->tid))
	   || read_string_field(file, gdf1->sn, 20)
	   || read64bval(file, 1, &nrec)
	   || read32bval(file, 2, recduration)
	   || read32bval(file, 1, &nch) )
		return -1;

	gdf1->xdf.rec_duration = (double)recduration[0] 
	                      / (double)recduration[1];
	gdf1->xdf.hdr_offset = hdrsize;
	gdf1->xdf.nrecord = nrec;
	gdf1->xdf.numch = nch;

	// format time string (drop centisec precision)
	sscanf(timestring, "%4i%2i%2i%2i%2i%2i", 
	                    &(ltm.tm_year), &(ltm.tm_mon), &(ltm.tm_mday),
	                    &(ltm.tm_hour), &(ltm.tm_min), &(ltm.tm_sec));
	ltm.tm_mon--;
	// REMINDER: tm_year is number of years since 1900
	ltm.tm_year -= 1900;
	gdf1->rectime = mktime(&ltm);

	return 0;
}


static enum xdftype get_xdfch_type(uint32_t gdf1type)
{
	unsigned int i;

	/* Treat gdf1 char as xdf uint8 */
	if (gdf1type == 0)
		return XDFUINT8;

	for (i=0; i<gdf1_tp_len; i++) {
		if (gdf1_types[i] == gdf1type)
			return i;
	}

	return -1;
}


/* \param bdf	pointer to a gdf1_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the correct file position)
 *
 * Read all channels related field in the header and setup the channels
 * accordingly. set the channels for no scaling and inmemtype = infiletype
 */
static int gdf1_read_channels_header(struct gdf1_file* gdf1, FILE* file)
{
	struct xdfch* ch;
	int i;
	unsigned int offset = 0;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf1ch(ch)->label, 16))
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf1ch(ch)->transducter, 80))
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf1ch(ch)->unit, 8))
			return -1;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		if (read64bval(file, 1, &(ch->physical_mm[0])))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		if (read64bval(file, 1, &(ch->physical_mm[1])))
			return -1;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val;
		if (read64bval(file, 1, &val))
			return -1;
		ch->digital_mm[0] = val;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
		int64_t val;
		if (read64bval(file, 1, &val))
			return -1;
		ch->digital_mm[1] = val;
	}

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf1ch(ch)->prefiltering, 80))
			return -1;

	i = 0;
	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
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
	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next) {
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
	gdf1->xdf.filerec_size = offset*gdf1->xdf.ns_per_rec;

	for (ch = gdf1->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf1ch(ch)->reserved, 32))
			return -1;

	return 0;
}


static 
int gdf1_read_event_hdr(struct gdf1_file* gdf1, FILE* file,
                       uint32_t* nevent, uint8_t* mode, double* fs)
{
	long flen, evt_sect; 
	uint8_t fs24[8];

	// Find the filesize
	if (fseek(file, 0L, SEEK_END) || (flen = ftell(file))<0)
		return -1;

	// Check if there is an event table
	evt_sect = gdf1->xdf.hdr_offset + gdf1->xdf.nrecord*
	                                       gdf1->xdf.filerec_size;
	if ((gdf1->xdf.nrecord < 0) || (flen <= evt_sect)) {
		*nevent = 0;
		return 0;
	} 

	// Read event header
	if (fseek(file, evt_sect, SEEK_SET)
	  || read8bval(file, 1, mode)
	  || read24bval(file, 1, fs24)
	  || read32bval(file, 1, nevent))
		return -1;
	*fs = fs24[LSB24] + 256*fs24[1] + 65536*fs24[MSB24];

	return 0;
}


static
int gdf1_interpret_events(struct gdf1_file* gdf1, uint32_t nevent,
                         double fs, uint32_t *pos, uint16_t* code,
			 uint16_t *channel, uint32_t* dur)
{
	unsigned int i;
	int evttype;
	char desc[32];
	struct xdfevent evt;

	for (i=0; i<nevent; i++) {
		if (channel[i])
			snprintf(desc, sizeof(desc),
			         "ch:%u", (unsigned int)(channel[i]));
		else
			strcpy(desc, "ch:all");
		evttype = add_event_entry(gdf1->xdf.table, code[i], desc);
		if (evttype < 0)
			return -1;

		evt.onset = pos[i]/fs;
		evt.duration = (dur == NULL) ? -1 : dur[i]/fs;
		evt.evttype = evttype;
		if (add_event(gdf1->xdf.table, &evt))
			return -1;
	}
	return 0;
}


static
int gdf1_read_event_table(struct gdf1_file* gdf1, FILE* file)
{
	int retcode = 0;
	uint8_t mode;
	double fs;
	uint32_t nevt, *onset = NULL, *dur = NULL;
	uint16_t *code = NULL, *ch = NULL;

	if (gdf1_read_event_hdr(gdf1, file, &nevt, &mode, &fs))
		return -1;
	if (nevt == 0)
		return 0;
	
	onset = calloc(nevt,sizeof(*onset));
	code = calloc(nevt,sizeof(*code));
	ch = calloc(nevt,sizeof(*ch));
	dur = calloc(nevt,sizeof(*dur));

	if (onset == NULL || code == NULL || ch == NULL || dur == NULL
	  || read32bval(file, nevt, onset)
	  || read16bval(file, nevt, code)
	  || (mode == 3 && read16bval(file, nevt, ch))
	  || (mode == 3 && read16bval(file, nevt, dur))
	  || gdf1_interpret_events(gdf1, nevt, fs, onset, code, ch, dur))
		retcode = -1;

	free(onset);
	free(code);
	free(ch);
	free(dur);
	return retcode;
}

/* \param xdf	pointer to an xdf file with XDF_READ mode
 *
 * GDF1 METHOD.
 * Read the header and allocate the channels
 * It creates from the file a stream that will be used to easily interpret
 * and parse the header.
 */
static int gdf1_read_header(struct xdf* xdf)
{
	int retval = -1;
	unsigned int i;
	struct xdfch** curr = &(xdf->channels);
	struct gdf1_file* gdf1 = get_gdf1(xdf);
	FILE* file = fdopen(mm_dup(xdf->fd), "rb");
	if (!file)
		return -1;

	if (gdf1_read_file_header(gdf1, file))
		goto exit;

	// Allocate all the channels
	for (i=0; i<xdf->numch; i++) {
		if (!(*curr = xdf_alloc_channel(xdf)))
			goto exit;
		curr = &((*curr)->next);
	}

	if (gdf1_read_channels_header(gdf1, file)
	   || gdf1_read_event_table(gdf1, file))
	   	goto exit;
		
	retval = 0;
exit:
	fclose(file);
	mm_seek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
}


/* \param xdf	pointer to an xdf file with XDF_WRITE mode
 *
 * GDF1 METHOD.
 * Write the number record in the header
 */
static int gdf1_complete_file(struct xdf* xdf)
{
	int retval = 0;
	int64_t numrec = xdf->nrecord;
	FILE* file = fdopen(mm_dup(xdf->fd), "wb");
	long evt_sect = xdf->hdr_offset + xdf->nrecord*xdf->filerec_size;

	// Write the event block and the number of records in the header
	if (file == NULL
	    || fseek(file, evt_sect, SEEK_SET)
	    || gdf1_write_event_table(get_gdf1(xdf), file)
	    || fseek(file, NUMREC_FIELD_LOC, SEEK_SET)
	    || write64bval(file, 1, &numrec) 
	    || fclose(file))
		retval = -1;

	return retval;
}

