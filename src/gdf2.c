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
#include <math.h>
#include <mmsysio.h>

#include "xdfio.h"
#include "xdffile.h"
#include "xdftypes.h"
#include "xdfevent.h"
#include "streamops.h"

#include "gdf2.h"

/******************************************************
 *               GDF2 specific declaration             *
 ******************************************************/

// GDF2 methods declaration
static int gdf2_set_channel(struct xdfch*, enum xdffield,
                           union optval, int);
static int gdf2_get_channel(const struct xdfch*, enum xdffield,
                           union optval*, int);
static int gdf2_set_conf(struct xdf*, enum xdffield, union optval, int); 
static int gdf2_get_conf(const struct xdf*, enum xdffield,
                        union optval*, int); 
static int gdf2_write_header(struct xdf*);
static int gdf2_read_header(struct xdf*);
static int gdf2_complete_file(struct xdf*);

// GDF2 channel structure
struct gdf2_channel {
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

// GDF2 file structure
struct gdf2_file {
	struct xdf xdf;
	struct gdf2_channel default_gdf2ch;
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
	unsigned int version;
};

#define NUMREC_FIELD_LOC 236

#define get_gdf2(xdf_p) \
	((struct gdf2_file*)(((char*)(xdf_p))-offsetof(struct gdf2_file, xdf)))
#define get_gdf2ch(xdfch_p) 				\
	((struct gdf2_channel*)(((char*)(xdfch_p))	\
		- offsetof(struct gdf2_channel, xdfch)))

static const uint32_t gdf2_types[XDF_NUM_DATA_TYPES] = {
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
#define gdf2_tp_len (sizeof(gdf2_types)/sizeof(gdf2_types[0]))

#define copy_3dpos(dst, src) do {	\
	(dst)[0] = (src)[0];		\
	(dst)[1] = (src)[1];		\
	(dst)[2] = (src)[2];		\
} while(0)				


static
uint64_t time_to_gdf2time(double posixtime)
{
	double gdf2time;
	gdf2time = (posixtime/86400.0 + 719529.0) * 4294967296.0;
	return gdf2time;	// round to the nearest integer
}

static
double gdf2time_to_time(uint64_t gdf2time)
{
	double posixtime;
	posixtime = (((double)gdf2time)/4294967296.0 - 719529.0) * 86400.0;
	return posixtime;
}

// Values from Table 5 and Table 6 from GDF doc v2.51
enum physical_dim {
	DECA = 1,
	HECTO,
	KILO,
	MEGA,
	GIGA,
	TERA,
	PETA,
	EXA,
	ZETTA,
	YOTTA,
	DECI = 16,
	CENTI,
	MILLI,
	MICRO,
	NANO,
	PICO,
	FEMTO,
	ATTO,
	DIMENSIONLESS = 512,
	PERCENT = 544,
	DEGREE = 736,
	RADIAN,
	HERTZ = 2496,
	BLOOD_PRESSURE = 3872,
	VOLTAGE = 4256,
	OHM = 4288,
	KELVIN = 4384,
	CELSIUS = 6048,
	LITER = 3072,
	LITER_SQUARE = 2848,
	HYDRAULIC_IMP = 4128,
	PULMONARY = 6016,
};
static
uint16_t convert_unit_to_dimcode(const char* unit) {

	uint16_t dimcode;

	// Convert value to physical unit (Table 5 GDF documentation V2.51)
	if (strncmp(unit, "-", 1) == 0)	// Dimensionless
		dimcode = DIMENSIONLESS;
	else if(strncmp(unit, "%", 1) == 0) // Percentage
		dimcode = PERCENT;
	else if (strncmp(unit, "degree", 6) == 0) // Degree
		dimcode = DEGREE;
	else if (strncmp(unit, "rad", 3) == 0) // Radians
		dimcode = RADIAN;
	else if (strncmp(unit, "Hz", 2) == 0) // Hertz
		dimcode = HERTZ;
	else if (strncmp(unit, "kHz", 3) == 0) // kilo Hertz
		dimcode = HERTZ + KILO;
	else if (strncmp(unit, "mmHg", 4) == 0) // Blood pressure
		dimcode = BLOOD_PRESSURE;
	else if (strncmp(unit, "V", 1) == 0) // Voltage
		dimcode = VOLTAGE;
	else if (strncmp(unit, "mV", 2) == 0) // millivolts
		dimcode = VOLTAGE + MILLI; // 4256 + 18
	else if (strncmp(unit, "uV", 2) == 0) // microvolts
		dimcode = VOLTAGE + MICRO; // 4256 + 19
	else if (strncmp(unit, "nV", 2) == 0) // nanovolts
		dimcode = VOLTAGE + NANO; // 4256 + 20
	else if (strncmp(unit, "Ohm", 3) == 0) // Ohm
		dimcode = OHM;
	else if (strncmp(unit, "kOhm", 4) == 0)
		dimcode = OHM + KILO;
	else if (strncmp(unit, "MOhm", 4) == 0)
		dimcode = OHM + MEGA;
	else if (strncmp(unit, "K", 1) == 0) // Temperature in Kelvin
		dimcode = KELVIN;
	else if (strncmp(unit, "°C", 2) == 0) // Temperature in degree Celsius
		dimcode = CELSIUS;
	else if (strncmp(unit, "l/min", 4) == 0) // Liter per minute
		dimcode = LITER;
	else if (strncmp(unit, "l(min m^2)", 7) == 0) // Liter per minute square meter
		dimcode = LITER_SQUARE;
	else if(strncmp(unit, "dyn s / cm^5", 7) == 0) // Hydraulic impedance
		dimcode = HYDRAULIC_IMP;
	else if (strncmp(unit, "dyn s / m^2 cm^5", 7) == 0) // Pulmonary
		dimcode = PULMONARY;
	else
		dimcode = 0;

	return dimcode;
};

static
int convert_dimcode_to_unit(char unit[7], uint16_t dimcode) {

	// Convert value to physical unit (Table 5 GDF documentation V2.51)
	if (dimcode == DIMENSIONLESS) // Dimensionless
		strncpy(unit, "-", 1);
	else if (dimcode == PERCENT) // Percentage
		strncpy(unit, "%", 1);
	else if (dimcode == DEGREE) // Degree
		strncpy(unit, "degree", 6);
	else if (dimcode == RADIAN) // Radians
		strncpy(unit, "rad", 3);
	else if (dimcode == HERTZ) // Hertz
		strncpy(unit, "Hz", 2);
	else if (dimcode == HERTZ + KILO)
		strncpy(unit, "kHz", 3);
	else if (dimcode == BLOOD_PRESSURE) // Blood pressure
		strncpy(unit, "mmHg", 4);
	else if (dimcode == VOLTAGE) // Voltage
		strncpy(unit, "V", 1);
	else if (dimcode == VOLTAGE + MILLI)
		strncpy(unit, "mV", 2);
	else if (dimcode == VOLTAGE + MICRO)
		strncpy(unit, "uV", 2);
	else if (dimcode == VOLTAGE + NANO)
		strncpy(unit, "nV", 2);
	else if (dimcode == OHM) // Ohm
		strncpy(unit, "Ohm", 3);
	else if (dimcode == OHM + KILO)
		strncpy(unit, "kOhm", 4);
	else if (dimcode == OHM + MEGA)
		strncpy(unit, "MOhm", 4);
	else if (dimcode == KELVIN) // Temperature in Kelvin
		strncpy(unit, "K", 1);
	else if (dimcode == CELSIUS) // Temperature in degree Celsius
		strncpy(unit, "°C", 2);
	else if (dimcode == LITER) // Liter per minute
		strncpy(unit, "l/min", 4);
	else if (dimcode == LITER_SQUARE) // Liter per minute square meter
		strncpy(unit, "l(min m^2)", 7);
	else if (dimcode == HYDRAULIC_IMP) // Hydraulic impedance
		strncpy(unit, "dyn s / cm^5", 7);
	else if (dimcode == PULMONARY) // Pulmonary
		strncpy(unit, "dyn s / m^2 cm^5", 7);
	else
		strncpy(unit, "Unknown", 7);

	return 0;

}
/******************************************************
 *            GDF2 type definition declaration         *
 ******************************************************/
static const enum xdffield gdf2_ch_supported_fields[] = {
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

static const enum xdffield gdf2_file_supported_fields[] = {
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

static const struct format_operations gdf2_ops = {
	.set_channel = gdf2_set_channel,
	.get_channel = gdf2_get_channel,
	.set_conf = gdf2_set_conf,
	.get_conf = gdf2_get_conf,
	.write_header = gdf2_write_header,
	.read_header = gdf2_read_header,
	.complete_file = gdf2_complete_file,
	.type = XDF_GDF2,
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
	.choff = offsetof(struct gdf2_channel, xdfch),
	.chlen = sizeof(struct gdf2_channel),
	.fileoff = offsetof(struct gdf2_file, xdf),
	.filelen = sizeof(struct gdf2_file),
	.chfields = gdf2_ch_supported_fields,
	.filefields = gdf2_file_supported_fields
};

static const struct gdf2_channel gdf2ch_def = {
	.xdfch = {.infiletype = XDFFLOAT}
};

/******************************************************
 *            GDF2 file support implementation         *
 ******************************************************/

/**
 * xdf_alloc_gdf2file() - allocates a GDF2 file
 *
 * Return: the allocated structure representing a GDF2 file
 */
LOCAL_FN struct xdf* xdf_alloc_gdf2file(void)
{
	struct gdf2_file* gdf2;
	struct eventtable* table;

	gdf2 = calloc(1, sizeof(*gdf2));
	table = create_event_table();
	if (gdf2 == NULL || table == NULL) {
		free(gdf2);
		destroy_event_table(table);
		return NULL;
	}

	gdf2->xdf.ops = &gdf2_ops;
	gdf2->xdf.defaultch = &(gdf2->default_gdf2ch.xdfch);
	gdf2->xdf.table = table;

	// Set good default for the file format
	memcpy(&(gdf2->default_gdf2ch), &gdf2ch_def, sizeof(gdf2ch_def));
	gdf2->default_gdf2ch.xdfch.owner = &(gdf2->xdf);
	gdf2->xdf.rec_duration = 1.0;
	gdf2->version = 0;
	gdf2->rectime = time_to_gdf2time(time(NULL));
	
	
	return &(gdf2->xdf);
}


/**
 * xdf_is_gdf2file() - indicates whether a pointer to a type of file corresponds
 *                     to a GDF2 file or not.
 * @magickey: pointer to key identifying a type of file
 *
 * Returns 1 if the supplied magickey corresponds to a GDF2 file
 */
LOCAL_FN int xdf_is_gdf2file(const unsigned char* magickey)
{
	char key[9] = {0};
	unsigned int version;

	strncpy(key, (const char*)magickey, 8);
	if (sscanf(key, "GDF 2.%u", &version) == 1)
		return 1;
	return 0;
}


/******************************************************
 *             GDF2 methods implementation         *
 ******************************************************/

/* \param ch	pointer to a xdf channel (GDF2) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to be set
 *
 * GDF2 METHOD.
 * Change the configuration field value of the channel according to val
 */
static int gdf2_set_channel(struct xdfch* ch, enum xdffield field,
                            union optval val, int prevretval)
{
	struct gdf2_channel* gdf2ch = get_gdf2ch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		strncpy(gdf2ch->label, val.str, sizeof(gdf2ch->label)-1);
	else if (field == XDF_CF_UNIT) {
		strncpy(gdf2ch->unit, val.str, sizeof(gdf2ch->unit)-1);
		gdf2ch->dimcode = convert_unit_to_dimcode(gdf2ch->unit);
	} else if (field == XDF_CF_TRANSDUCTER)
		strncpy(gdf2ch->transducter, val.str, sizeof(gdf2ch->transducter)-1);
	else if (field == XDF_CF_PREFILTERING)
		strncpy(gdf2ch->filtering, val.str, sizeof(gdf2ch->filtering)-1);
	else if (field == XDF_CF_RESERVED)
		strncpy(gdf2ch->reserved, val.str, sizeof(gdf2ch->reserved)-1);
	else if (field == XDF_CF_ELECPOS)
		copy_3dpos(gdf2ch->pos, val.pos);
	else if (field == XDF_CF_IMPEDANCE)
		gdf2ch->impedance = val.d;
	else
		retval = prevretval;

	return retval;
}

/* \param ch	pointer to a xdf channel (GDF2 underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF2 METHOD.
 * Get the configuration field value of the channel and assign it to val
 */
static int gdf2_get_channel(const struct xdfch* ch, enum xdffield field,
                            union optval *val, int prevretval)
{
	struct gdf2_channel* gdf2ch = get_gdf2ch(ch);
	int retval = 0;

	if (prevretval < 0)
		return -1;

	if (field == XDF_CF_LABEL)
		val->str = gdf2ch->label;
	else if (field == XDF_CF_UNIT) {
		if (convert_dimcode_to_unit(gdf2ch->unit, gdf2ch->dimcode))
			return -1;
		val->str = gdf2ch->unit;
	}
	else if (field == XDF_CF_TRANSDUCTER)
		val->str = gdf2ch->transducter;
	else if (field == XDF_CF_PREFILTERING)
		val->str = gdf2ch->filtering;
	else if (field == XDF_CF_RESERVED)
		val->str = gdf2ch->reserved;
	else if (field == XDF_CF_ELECPOS)
		copy_3dpos(val->pos, gdf2ch->pos);
	else if (field == XDF_CF_IMPEDANCE)
		val->d = gdf2ch->impedance;
	else
		retval = prevretval;

	return retval;
}


/* \param xdf	pointer to a xdf file (GDF2) with XDF_WRITE mode
 * \param field identifier of the field to be changed
 * \param val	union holding the value to set
 *
 * GDF2 METHOD.
 * Change the configuration field value according to val
 */
static int gdf2_set_conf(struct xdf* xdf, enum xdffield field, 
                         union optval val, int prevretval)
{
	struct gdf2_file* gdf2 = get_gdf2(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		strncpy(gdf2->subjstr, val.str, sizeof(gdf2->subjstr)-1);
	else if (field == XDF_F_SESS_DESC)
		strncpy(gdf2->recstr, val.str, sizeof(gdf2->recstr)-1);
	else if (field == XDF_F_RECTIME)
		gdf2->rectime = time_to_gdf2time(val.d);
	else if (field == XDF_F_BIRTHDAY)
		gdf2->birthday = time_to_gdf2time(val.d);
	else if (field == XDF_F_ADDICTION)
		gdf2->addiction = val.ui;
	else if (field == XDF_F_HEIGHT)
		gdf2->height = val.d;
	else if (field == XDF_F_WEIGHT)
		gdf2->weight = val.d;
	else if (field == XDF_F_GENDER)
		gdf2->ghv = (val.ui & 0x03) | (gdf2->ghv & ~0x03);
	else if (field == XDF_F_HANDNESS)
		gdf2->ghv = ((val.ui << 2) & 0x0C) | (gdf2->ghv & ~0x0C);
	else if (field == XDF_F_VISUAL_IMP)
		gdf2->ghv = ((val.ui << 4) & 0x30) | (gdf2->ghv & ~0x30);
	else if (field == XDF_F_HEART_IMP)
		gdf2->ghv = ((val.ui << 6) & 0xC0) | (gdf2->ghv & ~0xC0);
	else if (field == XDF_F_LOCATION) {
		gdf2->location[1] = val.pos[0] * 3600000;
		gdf2->location[2] = val.pos[1] * 3600000;
		gdf2->location[3] = val.pos[2] * 100;
	} else if (field == XDF_F_ICD_CLASS)
		memcpy(gdf2->pclass, val.icd, sizeof(gdf2->pclass));
	else if (field == XDF_F_HEADSIZE)
		copy_3dpos(gdf2->headsize, val.pos);
	else if (field == XDF_F_REF_POS)
		copy_3dpos(gdf2->refpos, val.pos);
	else if (field == XDF_F_GND_POS)
		copy_3dpos(gdf2->gndpos, val.pos);
	else
		retval = prevretval;

	return retval;
}

/* \param xdf	pointer to a xdf file (GDF2 underlying)
 * \param field identifier of the field to be get
 * \param val	union holding the output of the request
 *
 * GDF2 METHOD.
 * Get the configuration field value and assign it to val
 */
static int gdf2_get_conf(const struct xdf* xdf, enum xdffield field,
                        union optval *val, int prevretval)
{
	struct gdf2_file* gdf2 = get_gdf2(xdf);
	int retval = 0;
	
	if (prevretval < 0)
		return -1;

	if (field == XDF_F_SUBJ_DESC)
		val->str = gdf2->subjstr;
	else if (field == XDF_F_SESS_DESC)
		val->str = gdf2->recstr;
	else if (field == XDF_F_RECTIME)
		val->d = gdf2time_to_time(gdf2->rectime);
	else if (field == XDF_F_BIRTHDAY)
		val->d = gdf2time_to_time(gdf2->birthday);
	else if (field == XDF_F_ADDICTION)
		val->ui = gdf2->addiction;
	else if (field == XDF_F_HEIGHT)
		val->d = gdf2->height;
	else if (field == XDF_F_WEIGHT)
		val->d = gdf2->weight;
	else if (field == XDF_F_GENDER)
		val->ui = gdf2->ghv & 0x3;
	else if (field == XDF_F_HANDNESS)
		val->ui = (gdf2->ghv >> 2) & 0x3;
	else if (field == XDF_F_VISUAL_IMP)
		val->ui = (gdf2->ghv >> 4) & 0x3;
	else if (field == XDF_F_HEART_IMP)
		val->ui = (gdf2->ghv >> 8) & 0x3;
	else if (field == XDF_F_LOCATION) {
		val->pos[0] = (double)gdf2->location[1] / 3600000.0;
		val->pos[1] = (double)gdf2->location[2] / 3600000.0;
		val->pos[2] = (double)gdf2->location[3] / 100.0;
	} else if (field == XDF_F_ICD_CLASS)
		memcpy(val->icd, gdf2->pclass, sizeof(gdf2->pclass));
	else if (field == XDF_F_HEADSIZE)
		copy_3dpos(val->pos, gdf2->headsize);
	else if (field == XDF_F_REF_POS)
		copy_3dpos(val->pos, gdf2->refpos);
	else if (field == XDF_F_GND_POS)
		copy_3dpos(val->pos, gdf2->gndpos);
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


/* \param gdf2	pointer to a gdf2_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of the file)
 *
 * Write the general GDF2 file header. Write -1 in the number of records
 * field. The real value is written after the transfer is finished
 */
static int gdf2_write_file_header(struct gdf2_file* gdf2, FILE* file)
{
 	uint32_t recduration[2];
	int64_t nrec = -1;
	uint16_t nch, nhdr_blk = gdf2->xdf.numch+1;
	uint8_t reserved[10] = {0};
	char key[9];

	convert_recduration(gdf2->xdf.rec_duration, recduration);
	nrec = gdf2->xdf.nrecord;
	nch = gdf2->xdf.numch;

	// Write data format identifier
	snprintf(key, sizeof(key), "GDF 2.%02u", gdf2->version);
	if (fwrite(key, 8, 1, file) < 1)
		return -1;

	// Write all the general file header
	if ((fprintf(file, "%-66.66s", gdf2->subjstr) < 0)
	   || write8bval(file, 10, reserved)
	   || write8bval(file, 1, &(gdf2->addiction))
	   || write8bval(file, 1, &(gdf2->weight))
	   || write8bval(file, 1, &(gdf2->height))
	   || write8bval(file, 1, &(gdf2->ghv))
	   || (fprintf(file, "%-64.64s", gdf2->recstr) < 0)
	   || write32bval(file, 4, gdf2->location)
	   || write64bval(file, 1, &(gdf2->rectime))
	   || write64bval(file, 1, &(gdf2->birthday))
	   || write16bval(file, 1, &nhdr_blk)
	   || (fwrite(gdf2->pclass, 6, 1, file) < 1)
	   || write64bval(file, 1, &(gdf2->epid))
	   || write8bval(file, 6, reserved)
	   || write16bval(file, 3, gdf2->headsize)
	   || write32bval(file, 3, gdf2->refpos)
	   || write32bval(file, 3, gdf2->gndpos)
	   || write64bval(file, 1, &nrec)
	   || write32bval(file, 2, recduration)
	   || write16bval(file, 1, &nch)
	   || write8bval(file, 2, reserved))
		return -1;
	
	gdf2->xdf.hdr_offset = 256*nhdr_blk;
	return 0;
}

/* \param bdf	pointer to a gdf2_file opened for writing
 * \param file  stream associated to the file 
 *              (should be positioned at the beginning of channel fields)
 *
 * Write the GDF2 channels related fields in the header.
 */
static int gdf2_write_channels_header(struct gdf2_file* gdf2, FILE* file)
{
	struct xdfch* ch;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_gdf2ch(ch)->label) < 0)
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_gdf2ch(ch)->transducter)<0)
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-6.6s", get_gdf2ch(ch)->unit) < 0)
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
		if (write16bval(file, 1, &(get_gdf2ch(ch)->dimcode)))
			return -1;
	}

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->digital_mm[0])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write64bval(file, 1, &(ch->digital_mm[1])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-68.68s", get_gdf2ch(ch)->filtering) < 0)
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdf2ch(ch)->lp)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdf2ch(ch)->hp)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (write32bval(file, 1, &(get_gdf2ch(ch)->sp)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t nsprec = gdf2->xdf.ns_per_rec;
		if (write32bval(file, 1, &nsprec))
			return -1;
	}

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
		int32_t type = gdf2_types[ch->infiletype];
		if (write32bval(file, 1, &type))
			return -1;
	}

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (write32bval(file, 3, get_gdf2ch(ch)->pos))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (write8bval(file, 1, &(get_gdf2ch(ch)->impedance)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-19.19s", get_gdf2ch(ch)->reserved) < 0)
			return -1;

	return 0;
}


/* \param xdf	pointer to an xdf (GDF2) file with XDF_WRITE mode
 *
 * GDF2 METHOD.
 * Write the general file header and channels fields
 * It creates from the file a stream that will be used to easily format
 * the header fields.
 */
static int gdf2_write_header(struct xdf* xdf)
{
	int retval = 0;
	struct gdf2_file* gdf2 = get_gdf2(xdf);
	FILE* file = fdopen(mm_dup(xdf->fd), "wb");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( gdf2_write_file_header(gdf2, file)
	    || gdf2_write_channels_header(gdf2, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	mm_seek(xdf->fd, xdf->hdr_offset, SEEK_SET);

	return retval;
}


static
int gdf2_setup_events(struct eventtable* table, double fs, uint8_t* mode,
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
int gdf2_write_event_table(struct gdf2_file* gdf2, FILE* file)
{
	struct eventtable* table = gdf2->xdf.table;
	int retcode = 0;
	uint32_t nevt = table->nevent;
	uint8_t mode, nevt24[3];
	float fs = (float)gdf2->xdf.ns_per_rec/(float)gdf2->xdf.rec_duration;
	uint32_t *onset = NULL, *dur = NULL;
	uint16_t *code = NULL, *ch = NULL;

	if (nevt == 0)
		return 0;

	nevt24[LSB24] = nevt & 0x000000FF;
	nevt24[    1] = (nevt & 0x0000FF00) / 256;
	nevt24[MSB24] = (nevt & 0x00FF0000) / 65536;
	
	onset = malloc(nevt*sizeof(*onset));
	code = malloc(nevt*sizeof(*code));
	ch = malloc(nevt*sizeof(*ch));
	dur = malloc(nevt*sizeof(*dur));
	if (onset == NULL || code == NULL || ch == NULL || dur == NULL)
		retcode = -1;

	gdf2_setup_events(table, fs, &mode, onset, code, ch, dur);

	if (retcode
	  || write8bval(file, 1, &mode)
	  || write24bval(file, 1, nevt24)
	  || write32bval(file, 1, &fs)
	  || write32bval(file, nevt, onset)
	  || write16bval(file, nevt, code)
	  || (mode == 3 && write16bval(file, nevt, ch))
	  || (mode == 3 && write32bval(file, nevt, dur)))
		retcode = -1;

	free(onset);
	free(code);
	free(ch);
	free(dur);
	return retcode;
}


/* \param bdf	pointer to a gdf2_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the beginning of the file)
 *
 * Read the general GDF2 file header
 */
static int gdf2_read_file_header(struct gdf2_file* gdf2, FILE* file)
{
	uint32_t recduration[2];
	uint16_t nch, nhdr_blk;
	int64_t nrec;

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, gdf2->subjstr, 66)
	   || fseek(file, 10, SEEK_CUR)
	   || read8bval(file, 1, &(gdf2->addiction))
	   || read8bval(file, 1, &(gdf2->weight))
	   || read8bval(file, 1, &(gdf2->height))
	   || read8bval(file, 1, &(gdf2->ghv))
	   || read_string_field(file, gdf2->recstr, 64)
	   || read32bval(file, 4, gdf2->location)
	   || read64bval(file, 1, &(gdf2->rectime))
	   || read64bval(file, 1, &(gdf2->birthday))
	   || read16bval(file, 1, &nhdr_blk)
	   || (fread(gdf2->pclass, 6, 1, file) < 1)
	   || read64bval(file, 1, &(gdf2->epid))
	   || fseek(file, 6, SEEK_CUR)
	   || read16bval(file, 3, gdf2->headsize)
	   || read32bval(file, 3, gdf2->refpos)
	   || read32bval(file, 3, gdf2->gndpos)
	   || read64bval(file, 1, &nrec)
	   || read32bval(file, 2, recduration)
	   || read16bval(file, 1, &nch)
	   || fseek(file, 2, SEEK_CUR))
		return -1;
	   
	gdf2->xdf.rec_duration = (double)recduration[0] 
	                      / (double)recduration[1];
	gdf2->xdf.hdr_offset = nhdr_blk*256;
	gdf2->xdf.nrecord = nrec;
	gdf2->xdf.numch = nch;


	return 0;
}


static enum xdftype get_xdfch_type(uint32_t gdf2type)
{
	unsigned int i;

	/* Treat gdf2 char as xdf uint8 */
	if (gdf2type == 0)
		return XDFUINT8;

	for (i=0; i<gdf2_tp_len; i++) {
		if (gdf2_types[i] == gdf2type)
			return i;
	}

	return -1;
}


/* \param bdf	pointer to a gdf2_file open for reading
 * \param file  stream associated to the file 
 *              (should be at the correct file position)
 *
 * Read all channels related field in the header and setup the channels
 * accordingly. set the channels for no scaling and inmemtype = infiletype
 */
static int gdf2_read_channels_header(struct gdf2_file* gdf2, FILE* file)
{
	struct xdfch* ch;
	int i;
	unsigned int offset = 0;
        struct gdf2_channel* gdf2ch;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf2ch(ch)->label, 16))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf2ch(ch)->transducter,80))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf2ch(ch)->unit, 6))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
                gdf2ch = get_gdf2ch(ch);
                gdf2ch->dimcode = 0; // unrecognized

		if (read16bval(file, 1, &(get_gdf2ch(ch)->dimcode)))
			return -1;
		if (convert_dimcode_to_unit(gdf2ch->unit, gdf2ch->dimcode))
			return -1;
	}

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->physical_mm[0])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->physical_mm[1])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->digital_mm[0])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read64bval(file, 1, &(ch->digital_mm[1])))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf2ch(ch)->filtering, 68))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdf2ch(ch)->lp)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdf2ch(ch)->hp)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read32bval(file, 1, &(get_gdf2ch(ch)->sp)))
			return -1;

	i = 0;
	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
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
	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) {
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
	gdf2->xdf.filerec_size = offset*gdf2->xdf.ns_per_rec;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read32bval(file, 3, get_gdf2ch(ch)->pos))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next) 
		if (read8bval(file, 1, &(get_gdf2ch(ch)->impedance)))
			return -1;

	for (ch = gdf2->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_gdf2ch(ch)->reserved, 19))
			return -1;

	return 0;
}


static 
int gdf2_read_event_hdr(struct gdf2_file* gdf2, FILE* file,
                       uint32_t* nevent, uint8_t* mode, float* fs)
{
	long flen, evt_sect; 
	uint8_t nevt24[3];

	// Find the filesize
	if (fseek(file, 0L, SEEK_END) || (flen = ftell(file))<0)
		return -1;

	// Check if there is an event table
	evt_sect = gdf2->xdf.hdr_offset + gdf2->xdf.nrecord*
	                                       gdf2->xdf.filerec_size;
	if ((gdf2->xdf.nrecord < 0) || (flen <= evt_sect)) {
		*nevent = 0;
		return 0;
	} 

	// Read event header
	if (fseek(file, evt_sect, SEEK_SET)
	  || read8bval(file, 1, mode)
	  || read24bval(file, 1, nevt24)
	  || read32bval(file, 1, fs))
		return -1;
	*nevent = nevt24[LSB24] + 256*nevt24[1] + 65536*nevt24[MSB24];

	return 0;
}


static
int gdf2_interpret_events(struct gdf2_file* gdf2, uint32_t nevent,
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
		evttype = add_event_entry(gdf2->xdf.table, code[i], desc);
		if (evttype < 0)
			return -1;

		evt.onset = pos[i]/fs;
		evt.duration = (dur == NULL) ? -1 : dur[i]/fs;
		evt.evttype = evttype;
		if (add_event(gdf2->xdf.table, &evt))
			return -1;
	}
	return 0;
}


static
int gdf2_read_event_table(struct gdf2_file* gdf2, FILE* file)
{
	int retcode = 0;
	uint8_t mode;
	float fs;
	uint32_t nevt, *onset = NULL, *dur = NULL;
	uint16_t *code = NULL, *ch = NULL;

	if (gdf2_read_event_hdr(gdf2, file, &nevt, &mode, &fs))
		return -1;
	if (nevt == 0)
		return 0;
	
	onset = calloc(nevt, sizeof(*onset));
	code = calloc(nevt, sizeof(*code));
	ch = calloc(nevt, sizeof(*ch));
	dur = calloc(nevt, sizeof(*dur));

	if (onset == NULL || code == NULL || ch == NULL || dur == NULL
	  || read32bval(file, nevt, onset)
	  || read16bval(file, nevt, code)
	  || (mode == 3 && read16bval(file, nevt, ch))
	  || (mode == 3 && read16bval(file, nevt, dur))
	  || gdf2_interpret_events(gdf2, nevt, fs, onset, code, ch, dur))
		retcode = -1;

	free(onset);
	free(code);
	free(ch);
	free(dur);
	return retcode;
}

/* \param xdf	pointer to an xdf file with XDF_READ mode
 *
 * GDF2 METHOD.
 * Read the header and allocate the channels
 * It creates from the file a stream that will be used to easily interpret
 * and parse the header.
 */
static int gdf2_read_header(struct xdf* xdf)
{
	int retval = -1;
	unsigned int i;
	struct xdfch** curr = &(xdf->channels);
	struct gdf2_file* gdf2 = get_gdf2(xdf);
	FILE* file = fdopen(mm_dup(xdf->fd), "rb");
	if (!file)
		return -1;

	if (gdf2_read_file_header(gdf2, file))
		goto exit;

	// Allocate all the channels
	for (i=0; i<xdf->numch; i++) {
		if (!(*curr = xdf_alloc_channel(xdf)))
			goto exit;
		curr = &((*curr)->next);
	}

	if (gdf2_read_channels_header(gdf2, file)
	   || gdf2_read_event_table(gdf2, file))
	   	goto exit;
		
	retval = 0;
exit:
	fclose(file);
	mm_seek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
}


/* \param xdf	pointer to an xdf file with XDF_WRITE mode
 *
 * GDF2 METHOD.
 * Write the number record in the header
 */
static int gdf2_complete_file(struct xdf* xdf)
{
	int retval = 0;
	int64_t numrec = xdf->nrecord;
	FILE* file = fdopen(mm_dup(xdf->fd), "wb");
	long evt_sect = xdf->hdr_offset + xdf->nrecord*xdf->filerec_size;

	// Write the event block and the number of records in the header
	if (file == NULL
	    || fseek(file, evt_sect, SEEK_SET)
	    || gdf2_write_event_table(get_gdf2(xdf), file)
	    || fseek(file, NUMREC_FIELD_LOC, SEEK_SET)
	    || write64bval(file, 1, &numrec)
	    || fclose(file))
		retval = -1;

	return retval;
}

