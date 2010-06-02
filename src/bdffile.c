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
#include "bdf.h"


static int bdf_set_channel(struct xdfch*, enum xdfchfield, union optval);
static int bdf_get_channel(const struct xdfch*, enum xdfchfield, union
optval*);
static int bdf_copy_chconf(struct xdfch*, const struct xdfch*);
static struct xdfch* bdf_alloc_channel(void);
static void bdf_free_channel(struct xdfch* xdfch);
static int bdf_set_conf(struct xdf*, enum xdffield, union optval); 
static int bdf_get_conf(const struct xdf*, enum xdffield, union optval*); 
static int bdf_copy_conf(struct xdf*, const struct xdf*); 
static int bdf_write_header(struct xdf*);
static int bdf_read_header(struct xdf*);
static int bdf_close_file(struct xdf*);
static void bdf_free_file(struct xdf*);

static const struct format_operations bdf_ops = {
	.set_channel = bdf_set_channel,
	.get_channel = bdf_get_channel,
	.copy_chconf = bdf_copy_chconf,
	.alloc_channel = bdf_alloc_channel,
	.free_channel = bdf_free_channel,
	.set_conf = bdf_set_conf,
	.get_conf = bdf_get_conf,
	.copy_conf = bdf_copy_conf,
	.write_header = bdf_write_header,
	.read_header = bdf_read_header,
	.close_file = bdf_close_file,
	.free_file = bdf_free_file
};

struct bdf_file {
	struct xdf xdf;
	char subj_ident[81];
	char rec_ident[81];
	struct tm rectime;
};

struct bdf_channel {
	struct xdfch xdfch;
	char label[17];
	char transducter[81];
	char unit[9];
	char prefiltering[81];
	char reserved[33];
};

static const unsigned char bdf_magickey[] = {255, 'B', 'I', 'O', 'S', 'E', 'M', 'I'};
#define NUMREC_FIELD_LOC 236

#define get_bdf(xdf_p) \
	((struct bdf_file*)(((char*)(xdf_p))-offsetof(struct bdf_file, xdf)))
#define get_bdfch(xdfch_p) 				\
	((struct bdf_channel*)(((char*)(xdfch_p))	\
		- offsetof(struct bdf_channel, xdfch)))

struct xdf* bdf_alloc_xdffile()
{
	struct bdf_file* bdf;

	bdf = calloc(1, sizeof(*bdf));
	if (!bdf)
		return NULL;

	bdf->xdf.ops = &bdf_ops;
	return &(bdf->xdf);
}

int bdf_is_same_type(const unsigned char* magickey)
{
	if (memcmp(magickey, bdf_magickey, sizeof(bdf_magickey)) == 0)
		return 1;
	return 0;
}


static int bdf_set_channel(struct xdfch* ch, enum xdfchfield field, union optval val)
{
	struct bdf_channel* bdfch = get_bdfch(ch);
	int retval = 0;

	if (field == XDF_CHFIELD_LABEL)
		strncpy(bdfch->label, val.str, sizeof(bdfch->label)-1);
	else if (field == XDF_CHFIELD_UNIT)
		strncpy(bdfch->unit, val.str, sizeof(bdfch->unit)-1);
	else if (field == XDF_CHFIELD_TRANSDUCTER)
		strncpy(bdfch->transducter, val.str, sizeof(bdfch->transducter)-1);
	else if (field == XDF_CHFIELD_PREFILTERING)
		strncpy(bdfch->prefiltering, val.str, sizeof(bdfch->prefiltering)-1);
	else if (field == XDF_CHFIELD_RESERVED)
		strncpy(bdfch->reserved, val.str, sizeof(bdfch->reserved)-1);
	else
		retval = -1;

	return retval;
}

static int bdf_get_channel(const struct xdfch* ch, enum xdfchfield
field, union optval *val)
{
	struct bdf_channel* bdfch = get_bdfch(ch);
	int retval = 0;

	if (field == XDF_CHFIELD_LABEL)
		val->str = bdfch->label;
	else if (field == XDF_CHFIELD_UNIT)
		val->str = bdfch->unit;
	else if (field == XDF_CHFIELD_TRANSDUCTER)
		val->str = bdfch->transducter;
	else if (field == XDF_CHFIELD_PREFILTERING)
		val->str = bdfch->prefiltering;
	else if (field == XDF_CHFIELD_RESERVED)
		val->str = bdfch->reserved;
	else
		retval = -1;

	return retval;
}

static int bdf_copy_chconf(struct xdfch* dst, const struct xdfch* src)
{
	double dmin, dmax, pmin, pmax;
	enum xdftype ts, ta;
	unsigned int offset, index, digital_inmem;
	const char *label, *unit, *transducter, *filtinfo, *reserved;

	xdf_get_chconf(src, XDF_CHFIELD_PHYSICAL_MIN, &pmin,
			XDF_CHFIELD_PHYSICAL_MAX, &pmax,
			XDF_CHFIELD_DIGITAL_MIN, &dmin,
			XDF_CHFIELD_DIGITAL_MAX, &dmax,
			XDF_CHFIELD_ARRAY_DIGITAL, &digital_inmem,
			XDF_CHFIELD_STORED_TYPE, &ts,
			XDF_CHFIELD_ARRAY_TYPE, &ta,
			XDF_CHFIELD_ARRAY_OFFSET, &offset,
			XDF_CHFIELD_ARRAY_INDEX, &index,
			XDF_CHFIELD_LABEL, &label,
			XDF_CHFIELD_UNIT, &unit,
			XDF_CHFIELD_TRANSDUCTER, &transducter,
			XDF_CHFIELD_PREFILTERING, &filtinfo,
			XDF_CHFIELD_RESERVED, &reserved,
			XDF_CHFIELD_NONE);

	xdf_set_chconf(dst, XDF_CHFIELD_PHYSICAL_MIN, pmin,
			XDF_CHFIELD_PHYSICAL_MAX, pmax,
			XDF_CHFIELD_DIGITAL_MIN, dmin,
			XDF_CHFIELD_DIGITAL_MAX, dmax,
			XDF_CHFIELD_ARRAY_DIGITAL, digital_inmem,
			XDF_CHFIELD_STORED_TYPE, ts,
			XDF_CHFIELD_ARRAY_TYPE, ta,
			XDF_CHFIELD_ARRAY_OFFSET, offset,
			XDF_CHFIELD_ARRAY_INDEX, index,
			XDF_CHFIELD_LABEL, label,
			XDF_CHFIELD_UNIT, unit,
			XDF_CHFIELD_TRANSDUCTER, transducter,
			XDF_CHFIELD_PREFILTERING, filtinfo,
			XDF_CHFIELD_RESERVED, reserved,
			XDF_CHFIELD_NONE);

	return 0;
}

static struct xdfch* bdf_alloc_channel(void)
{
	struct bdf_channel* ch;
	
	ch = malloc(sizeof(*ch));
	if (!ch)
		return NULL;
	memset(ch, 0, sizeof(*ch));
	ch->xdfch.ops = &bdf_ops;

	return &(ch->xdfch);
}

static void bdf_free_channel(struct xdfch* xdfch)
{
	free(get_bdfch(xdfch));
}

static int bdf_set_conf(struct xdf* xdf, enum xdffield field, union
optval val)
{
	struct bdf_file* bdf = get_bdf(xdf);
	int retval = 0;
	
	if (field == XDF_FIELD_SUBJ_DESC)
		strncpy(bdf->subj_ident, val.str, sizeof(bdf->subj_ident)-1);
	else if (field == XDF_FIELD_REC_DESC)
		strncpy(bdf->rec_ident, val.str, sizeof(bdf->rec_ident)-1);
	else
		retval = -1;

	return retval;
}

static int bdf_get_conf(const struct xdf* xdf, enum xdffield field,
union optval *val)
{
	struct bdf_file* bdf = get_bdf(xdf);
	int retval = 0;
	
	if (field == XDF_FIELD_SUBJ_DESC)
		val->str = bdf->subj_ident;
	else if (field == XDF_FIELD_REC_DESC)
		val->str = bdf->rec_ident;
	else
		retval = -1;

	return retval;
}

static int bdf_copy_conf(struct xdf* dst, const struct xdf* src)
{
	double recduration;
	int ns_rec;
	const char *subj, *rec;

	xdf_get_conf(src, XDF_FIELD_RECORD_DURATION, &recduration,
				XDF_FIELD_NSAMPLE_PER_RECORD, &ns_rec,
				XDF_FIELD_SUBJ_DESC, &subj,
				XDF_FIELD_REC_DESC, &rec,
				XDF_FIELD_NONE);

	xdf_set_conf(dst, XDF_FIELD_RECORD_DURATION, recduration,
				XDF_FIELD_NSAMPLE_PER_RECORD, ns_rec,
				XDF_FIELD_SUBJ_DESC, subj,
				XDF_FIELD_REC_DESC, rec,
				XDF_FIELD_NONE);

	return 0;
}

static int bdf_write_file_header(struct bdf_file* bdf, FILE* file)
{
	char timestring[17];
	unsigned headersize = 256 + (bdf->xdf.numch)*256;
	int retval;

	// format time string
	strftime(timestring, sizeof(timestring), 
		 "%d.%m.%y%H.%M.%S", &(bdf->rectime));

	// Write data format identifier
	if (fwrite(bdf_magickey, sizeof(bdf_magickey), 1, file) < 1)
		return -1;

	// Write all the general file header
	retval = fprintf(file, 
			"%-80.80s%-80.80s%16s%-8u%-44.44s%-8i%-8u%-4u",
			bdf->subj_ident,
			bdf->rec_ident,
			timestring,
			headersize,
			"24BIT",
			(int)-1,
			(unsigned int)1,
			bdf->xdf.numch);
	
	if (retval < 0)
		return -1;
	
	return 0;
}

static int bdf_write_channels_header(struct bdf_file* bdf, FILE* file)
{
	struct xdfch* ch;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_bdfch(ch)->label) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_bdfch(ch)->transducter) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8.8s", get_bdfch(ch)->unit) < 0)
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
		if (fprintf(file, "%-80.80s", get_bdfch(ch)->prefiltering) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8u", bdf->xdf.ns_per_rec) < 0)
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-32.32s", get_bdfch(ch)->reserved) < 0)
			return -1;

	return 0;
}


static int bdf_write_header(struct xdf* xdf)
{
	int retval = 0;
	struct bdf_file* bdf = get_bdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "w");
	if (!file)
		return -1;

	// Write file header each field of all channels
	if ( bdf_write_file_header(bdf, file)
	    || bdf_write_channels_header(bdf, file) )
		retval = -1;

	if (fflush(file) || fclose(file))
		retval = -1;

	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);

	return retval;
}


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

static int bdf_read_file_header(struct bdf_file* bdf, FILE* file)
{
	char timestring[17], type[45];
	int recdur, hdrsize, retval = 0;

	fseek(file, 8, SEEK_SET);

	if (read_string_field(file, bdf->subj_ident, 80)
	   || read_string_field(file, bdf->rec_ident, 80)
	   || read_string_field(file, timestring, 16)
	   || read_int_field(file, &hdrsize, 8)
	   || read_string_field(file, type, 44)
	   || read_int_field(file, &(bdf->xdf.nrecord), 8)
	   || read_int_field(file, &recdur, 8)
	   || read_int_field(file, (int*)&(bdf->xdf.numch), 4) )
		retval = -1;
 
	bdf->xdf.rec_duration = (double)recdur;

	// format time string
	if (!retval)
		strftime(timestring, sizeof(timestring), 
			 "%d.%m.%y%H.%M.%S", &(bdf->rectime));

	return retval;
}

static int bdf_read_channels_header(struct bdf_file* bdf, FILE* file)
{
	struct xdfch* ch;
	int ival;
	unsigned int offset = 0;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_bdfch(ch)->label, 16))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_bdfch(ch)->transducter, 80))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_bdfch(ch)->unit, 8))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->physical_mm[0] = (double)ival;
	}

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->physical_mm[1] = (double)ival;
	}

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->digital_mm[0] = (double)ival;
	}

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next) {
		if (read_int_field(file, &ival, 8))
			return -1;
		ch->digital_mm[1] = (double)ival;
	}

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_bdfch(ch)->prefiltering, 80))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_int_field(file, (int*)&(bdf->xdf.ns_per_rec), 8))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (read_string_field(file, get_bdfch(ch)->reserved, 32))
			return -1;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next) {
		if ((ch->digital_mm[0] >= 0) && (ch->digital_mm[1] > 8388607.0))
			ch->infiletype = ch->inmemtype = XDFUINT24;
		else
			ch->infiletype = ch->inmemtype = XDFINT24;
		ch->digital_inmem = 1;
		ch->offset = offset;
		offset += get_data_size(ch->infiletype);
	}


	return 0;
}


static int bdf_read_header(struct xdf* xdf)
{
	int retval = -1;
	unsigned int i;
	struct xdfch** curr = &(xdf->channels);
	struct bdf_file* bdf = get_bdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "r");
	if (!file)
		return -1;

	if (bdf_read_file_header(bdf, file))
		goto exit;

	// Allocate all the channels

	for (i=0; i<xdf->numch; i++) {
		if (!(*curr = bdf_alloc_channel()))
			goto exit;
		curr = &((*curr)->next);
	}

	if (bdf_read_channels_header(bdf, file))
		goto exit;
	
	retval = 0;
exit:
	fclose(file);

	lseek(xdf->fd, (xdf->numch+1)*256, SEEK_SET);
	return retval;
}


static void bdf_free_file(struct xdf* xdf)
{
	free(get_bdf(xdf));
}


static int bdf_close_file(struct xdf* xdf)
{
	int retval = 0;
	char numrecstr[9];

	// Write the number of records
	if (xdf->ready && (xdf->mode == XDF_WRITE)) {
		snprintf(numrecstr, 9, "%-8i", xdf->nrecord);
		if ( (lseek(xdf->fd, NUMREC_FIELD_LOC, SEEK_SET) < 0)
		    || (write(xdf->fd, numrecstr, 8) < 0) )
		  	retval = -1;
	}

	return retval;
}

