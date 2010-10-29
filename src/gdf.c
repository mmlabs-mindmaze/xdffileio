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
#include <math.h>

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
	char unit[7];
	uint16_t dimcode;
	char filtering[69];
	float lp, hp, sp;
	float pos[3];
	uint8_t impedance;
	char reserved[20];
};

// GDF file structure
struct gdf_file {
	struct xdf xdf;
	struct gdf_channel default_gdfch;
	char subjstr[67];
	uint8_t addiction;
	uint8_t weight;
	uint8_t height;
	uint8_t ghv;
	char recstr[65];
	int32_t location[4];
	uint64_t rectime;
	uint64_t birthday;
	uint8_t pclass[6];
	uint16_t headsize[3];
	float refpos[3];
	float gndpos[3];
	uint64_t epid;
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

#define copy_3dpos(dst, src) do {	\
	(dst)[0] = (src)[0];		\
	(dst)[1] = (src)[1];		\
	(dst)[2] = (src)[2];		\
} while(0)				


static
uint64_t time_to_gdftime(time_t posixtime)
{
	double gdftime;
	gdftime = (((double)posixtime)/86400.0 + 719529.0) * 4294967296.0;
	return (gdftime + 0.5);	// round to the nearest integer
}

static
time_t gdftime_to_time(uint64_t gdftime)
{
	double posixtime;
	posixtime = (((double)gdftime)/4294967296.0 - 719529.0) * 86400.0;
	return posixtime;
}

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
	XDF_CF_ELECPOS,
	XDF_CF_IMPEDANCE,
	XDF_NOF
};

static const enum xdffield gdf_file_supported_fields[] = {
	XDF_F_REC_DURATION,
	XDF_F_REC_NSAMPLE,
	XDF_F_SUBJ_DESC,
	XDF_F_SESS_DESC,
	XDF_F_RECTIME,
	XDF_F_ADDICTION,
	XDF_F_BIRTHDAY,
	XDF_F_HEIGHT,
	XDF_F_WEIGHT,
	XDF_F_GENDER,
	XDF_F_HANDNESS,
	XDF_F_VISUAL_IMP,
	XDF_F_HEART_IMP,
	XDF_F_LOCATION,
	XDF_F_ICD_CLASS,
	XDF_F_HEADSIZE,
	XDF_F_REF_POS,
	XDF_F_GND_POS,
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
	{'G','D','F',' ','2','.','0','0'};

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
	gdf->rectime = time_to_gdftime(time(NULL));
	
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
		strncpy(gdfch->filtering, val.str, sizeof(gdfch->filtering)-1);
	else if (field == XDF_CF_RESERVED)
		strncpy(gdfch->reserved, val.str, sizeof(gdfch->reserved)-1);
	else if (field == XDF_CF_ELECPOS)
		copy_3dpos(gdfch->pos, val.pos);
	else if (field == XDF_CF_IMPEDANCE)
		gdfch->impedance = val.d;
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
		val->str = gdfch->filtering;
	else if (field == XDF_CF_RESERVED)
		val->str = gdfch->reserved;
	else if (field == XDF_CF_ELECPOS)
		copy_3dpos(val->pos, gdfch->pos);
	else if (field == XDF_CF_IMPEDANCE)
		val->d = gdfch->impedance;
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
		gdf->rectime = time_to_gdftime(val.ts);
	else if (field == XDF_F_BIRTHDAY)
		gdf->birthday = time_to_gdftime(val.ts);
	else if (field == XDF_F_ADDICTION)
		gdf->addiction = val.ui;
	else if (field == XDF_F_HEIGHT)
		gdf->height = val.d;
	else if (field == XDF_F_WEIGHT)
		gdf->weight = val.d;
	else if (field == XDF_F_GENDER)
		gdf->ghv = (val.ui & 0x03) | (gdf->ghv & ~0x03);
	else if (field == XDF_F_HANDNESS)
		gdf->ghv = ((val.ui << 2) & 0x0C) | (gdf->ghv & ~0x0C);
	else if (field == XDF_F_VISUAL_IMP)
		gdf->ghv = ((val.ui << 4) & 0x30) | (gdf->ghv & ~0x30);
	else if (field == XDF_F_HEART_IMP)
		gdf->ghv = ((val.ui << 6) & 0xC0) | (gdf->ghv & ~0xC0);
	else if (field == XDF_F_LOCATION) {
		gdf->location[1] = val.pos[0] * 3600000;
		gdf->location[2] = val.pos[1] * 3600000;
		gdf->location[3] = val.pos[2] * 100;
	} else if (field == XDF_F_ICD_CLASS)
		memcpy(gdf->pclass, val.icd, sizeof(gdf->pclass));
	else if (field == XDF_F_HEADSIZE)
		copy_3dpos(gdf->headsize, val.pos);
	else if (field == XDF_F_REF_POS)
		copy_3dpos(gdf->refpos, val.pos);
	else if (field == XDF_F_GND_POS)
		copy_3dpos(gdf->gndpos, val.pos);
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
		val->ts = gdftime_to_time(gdf->rectime);
	else if (field == XDF_F_BIRTHDAY)
		val->ts = gdftime_to_time(gdf->birthday);
	else if (field == XDF_F_ADDICTION)
		val->ui = gdf->addiction;
	else if (field == XDF_F_HEIGHT)
		val->d = gdf->height;
	else if (field == XDF_F_WEIGHT)
		val->d = gdf->weight;
	else if (field == XDF_F_GENDER)
		val->ui = gdf->ghv & 0x3;
	else if (field == XDF_F_HANDNESS)
		val->ui = (gdf->ghv >> 2) & 0x3;
	else if (field == XDF_F_VISUAL_IMP)
		val->ui = (gdf->ghv >> 4) & 0x3;
	else if (field == XDF_F_HEART_IMP)
		val->ui = (gdf->ghv >> 8) & 0x3;
	else if (field == XDF_F_LOCATION) {
		val->pos[0] = (double)gdf->location[1] / 3600000.0;
		val->pos[1] = (double)gdf->location[2] / 3600000.0;
		val->pos[2] = (double)gdf->location[3] / 100.0;
	} else if (field == XDF_F_ICD_CLASS)
		memcpy(val->icd, gdf->pclass, sizeof(gdf->pclass));
	else if (field == XDF_F_HEADSIZE)
		copy_3dpos(val->pos, gdf->headsize);
	else if (field == XDF_F_REF_POS)
		copy_3dpos(val->pos, gdf->refpos);
	else if (field == XDF_F_GND_POS)
		copy_3dpos(val->pos, gdf->gndpos);
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
 	uint32_t recduration[2];
	int64_t nrec = -1;
	uint16_t nch, nhdr_blk = gdf->xdf.numch+1;
	uint8_t reserved[10] = {0};

	convert_recduration(gdf->xdf.rec_duration, recduration);
	nrec = gdf->xdf.nrecord;
	nch = gdf->xdf.numch;

	// Write data format identifier
	if (fwrite(gdf_magickey, 8, 1, file) < 1)
		return -1;

	// Write all the general file header
	if ((fprintf(file, "%-66.66s", gdf->subjstr) < 0)
	   || write8bval(file, 10, reserved)
	   || write8bval(file, 1, &(gdf->addiction))
	   || write8bval(file, 1, &(gdf->weight))
	   || write8bval(file, 1, &(gdf->height))
	   || write8bval(file, 1, &(gdf->ghv))
	   || (fprintf(file, "%-64.64s", gdf->recstr) < 0)
	   || write32bval(file, 4, gdf->location)
	   || write64bval(file, 1, &(gdf->rectime))
	   || write64bval(file, 1, &(gdf->birthday))
	   || write16bval(file, 1, &nhdr_blk)
	   || (fwrite(gdf->pclass, 6, 1, file) < 1)
	   || write64bval(file, 1, &(gdf->epid))
	   || write8bval(file, 6, reserved)
	   || write16bval(file, 3, gdf->headsize)
	   || write32bval(file, 3, gdf->refpos)
	   || write32bval(file, 3, gdf->gndpos)
	   || write64bval(file, 1, &nrec)
	   || write32bval(file, 2, recduration)
	   || write16bval(file, 1, &nch)
	   || write8bval(file, 2, reserved))
		return -1;
	
	gdf->xdf.hdr_offset = 256*nhdr_blk;
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
		if (fprintf(file, "%-6.6s", get_gdfch(ch)->unit) < 0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write16bval(file, 1, &(get_gdfch(ch)->dimcode)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->digital_mm[0])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->digital_mm[1])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-68.68s", get_gdfch(ch)->filtering) < 0)
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdfch(ch)->lp)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdfch(ch)->hp)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdfch(ch)->sp)))
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
		if (write32bval(file, 3, get_gdfch(ch)->pos))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (write8bval(file, 1, &(get_gdfch(ch)->impedance)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-19.19s", get_gdfch(ch)->reserved) < 0)
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

	lseek(xdf->fd, xdf->hdr_offset, SEEK_SET);

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
	uint32_t recduration[2];
	uint16_t nch, nhdr_blk;
	int64_t nrec;

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, gdf->subjstr, 66)
	   || fseek(file, 10, SEEK_CUR)
	   || read8bval(file, 1, &(gdf->addiction))
	   || read8bval(file, 1, &(gdf->weight))
	   || read8bval(file, 1, &(gdf->height))
	   || read8bval(file, 1, &(gdf->ghv))
	   || read_string_field(file, gdf->recstr, 64)
	   || read32bval(file, 4, gdf->location)
	   || read64bval(file, 1, &(gdf->rectime))
	   || read64bval(file, 1, &(gdf->birthday))
	   || read16bval(file, 1, &nhdr_blk)
	   || (fread(gdf->pclass, 6, 1, file) < 1)
	   || read64bval(file, 1, &(gdf->epid))
	   || fseek(file, 6, SEEK_CUR)
	   || read16bval(file, 3, gdf->headsize)
	   || read32bval(file, 3, gdf->refpos)
	   || read32bval(file, 3, gdf->gndpos)
	   || read64bval(file, 1, &nrec)
	   || read32bval(file, 2, recduration)
	   || read16bval(file, 1, &nch)
	   || fseek(file, 2, SEEK_CUR))
		return -1;
	   
	gdf->xdf.rec_duration = (double)recduration[0] 
	                      / (double)recduration[1];
	gdf->xdf.hdr_offset = nhdr_blk*256;
	gdf->xdf.nrecord = nrec;
	gdf->xdf.numch = nch;


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
		if (read_string_field(file, get_gdfch(ch)->unit, 6))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read16bval(file, 1, &(get_gdfch(ch)->dimcode)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->digital_mm[0])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->digital_mm[1])))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->filtering, 68))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdfch(ch)->lp)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdfch(ch)->hp)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdfch(ch)->sp)))
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
		if (read32bval(file, 3, get_gdfch(ch)->pos))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next) 
		if (read8bval(file, 1, &(get_gdfch(ch)->impedance)))
			return -1;

	for (ch = gdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdfch(ch)->reserved, 19))
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

