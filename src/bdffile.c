#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "xdfio.h"
#include "xdffile.h"
#include "xdferror.h"
#include "xdfformatops.h"
#include "bdf.h"


static int bdf_set_channel(struct xdf_channel*, enum xdfchfield, ...);
static int bdf_get_channel(const struct xdf_channel*, enum xdfchfield, ...);
static int bdf_copy_channel(struct xdf_channel*, const struct xdf_channel*);
static struct xdf_channel* bdf_alloc_channel(void);
static void bdf_free_channel(struct xdf_channel* xdfch);
static int bdf_set_info(struct xdffile*, enum xdffield, ...); 
static int bdf_get_info(const struct xdffile*, enum xdffield, ...); 
static int bdf_copy_info(struct xdffile*, const struct xdffile*); 
static int bdf_write_header(struct xdffile*);
static int bdf_read_header(struct xdffile*);
static int bdf_close_file(struct xdffile*);

static const struct format_operations bdf_ops = {
	.set_channel = bdf_set_channel,
	.get_channel = bdf_get_channel,
	.copy_channel = bdf_copy_channel,
	.alloc_channel = bdf_alloc_channel,
	.free_channel = bdf_free_channel,
	.set_info = bdf_set_info,
	.get_info = bdf_get_info,
	.copy_info = bdf_copy_info,
	.write_header = bdf_write_header,
	.read_header = bdf_read_header,
	.close_file = bdf_close_file
};

struct bdf_file {
	struct xdffile xdf;
	char subj_ident[81];
	char rec_ident[81];
	struct tm rectime;
};

struct bdf_channel {
	struct xdf_channel xdfch;
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

struct xdffile* bdf_alloc_xdffile()
{
	struct bdf_file* bdf;

	bdf = calloc(1, sizeof(*bdf));
	if (bdf)
		bdf->xdf.ops = &bdf_ops;
	return &(bdf->xdf);
}

int bdf_is_same_type(const unsigned char* magickey)
{
	if (memcmp(magickey, bdf_magickey, sizeof(bdf_magickey)) == 0)
		return 1;
	return 0;
}


static int bdf_set_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	struct bdf_channel* bdfch = get_bdfch(ch);
	va_list ap;
	int retval = 0;

	va_start(ap, field);
	if (field == XDF_CHFIELD_LABEL)
		strncpy(bdfch->label, va_arg(ap, const char*), sizeof(bdfch->label)-1);
	else if (field == XDF_CHFIELD_UNIT)
		strncpy(bdfch->unit, va_arg(ap, const char*), sizeof(bdfch->unit)-1);
	else if (field == XDF_CHFIELD_TRANSDUCTER)
		strncpy(bdfch->transducter, va_arg(ap, const char*), sizeof(bdfch->transducter)-1);
	else if (field == XDF_CHFIELD_PREFILTERING)
		strncpy(bdfch->prefiltering, va_arg(ap, const char*), sizeof(bdfch->prefiltering)-1);
	else if (field == XDF_CHFIELD_RESERVED)
		strncpy(bdfch->reserved, va_arg(ap, const char*), sizeof(bdfch->reserved)-1);
	else
		retval = -1;
	va_end(ap);

	return retval;
}

static int bdf_get_channel(const struct xdf_channel* ch, enum xdfchfield field, ...)
{
	struct bdf_channel* bdfch = get_bdfch(ch);
	va_list ap;
	int retval = 0;

	va_start(ap, field);
	if (field == XDF_CHFIELD_LABEL)
		*va_arg(ap, const char**) = bdfch->label;
	else if (field == XDF_CHFIELD_UNIT)
		*va_arg(ap, const char**) = bdfch->unit;
	else if (field == XDF_CHFIELD_TRANSDUCTER)
		*va_arg(ap, const char**) = bdfch->transducter;
	else if (field == XDF_CHFIELD_PREFILTERING)
		*va_arg(ap, const char**) = bdfch->prefiltering;
	else if (field == XDF_CHFIELD_RESERVED)
		*va_arg(ap, const char**) = bdfch->reserved;
	else
		retval = -1;
	va_end(ap);

	return retval;
}

static int bdf_copy_channel(struct xdf_channel* dst, const struct xdf_channel* src)
{
	double dmin, dmax, pmin, pmax;
	enum xdftype ts, ta;
	unsigned int offset, index;
	const char *label, *unit, *transducter, *filtinfo, *reserved;

	src->ops->get_channel(src, XDF_CHFIELD_PHYSICAL_MIN, &pmin,
				   XDF_CHFIELD_PHYSICAL_MAX, &pmax,
				   XDF_CHFIELD_DIGITAL_MIN, &dmin,
				   XDF_CHFIELD_DIGITAL_MAX, &dmax,
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

	src->ops->set_channel(dst, XDF_CHFIELD_PHYSICAL_MIN, pmin,
				   XDF_CHFIELD_PHYSICAL_MAX, pmax,
				   XDF_CHFIELD_DIGITAL_MIN, dmin,
				   XDF_CHFIELD_DIGITAL_MAX, dmax,
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

static struct xdf_channel* bdf_alloc_channel(void)
{
	struct bdf_channel* ch;
	
	ch = malloc(sizeof(*ch));
	if (ch) {
		memset(ch, 0, sizeof(*ch));
		ch->xdfch.ops = &bdf_ops;
	}
	return &(ch->xdfch);
}

static void bdf_free_channel(struct xdf_channel* xdfch)
{
	free(get_bdfch(xdfch));
}

static int bdf_set_info(struct xdffile* xdf, enum xdffield field, ...)
{
	struct bdf_file* bdf = get_bdf(xdf);
	va_list ap;
	int retval = 0;
	
	va_start(ap, field);
	if (field == XDF_FIELD_SUBJ_DESC)
		strncpy(bdf->subj_ident, va_arg(ap, const char*), sizeof(bdf->subj_ident)-1);
	else if (field == XDF_FIELD_REC_DESC)
		strncpy(bdf->rec_ident, va_arg(ap, const char*), sizeof(bdf->rec_ident)-1);
	else
		retval = -1;
	va_end(ap);

	return retval;
}

static int bdf_get_info(const struct xdffile* xdf, enum xdffield field, ...)
{
	struct bdf_file* bdf = get_bdf(xdf);
	va_list ap;
	int retval = 0;
	
	va_start(ap, field);
	if (field == XDF_FIELD_SUBJ_DESC)
		*va_arg(ap, const char**) = bdf->subj_ident;
	else if (field == XDF_FIELD_REC_DESC)
		*va_arg(ap, const char**) = bdf->rec_ident;
	else
		retval = -1;
	va_end(ap);

	return retval;
}

static int bdf_copy_info(struct xdffile* dst, const struct xdffile* src)
{
	double recduration;
	int ns_rec;
	const char *subj, *rec;

	src->ops->get_info(src, XDF_FIELD_RECORD_DURATION, &recduration,
				XDF_FIELD_NSAMPLE_PER_RECORD, &ns_rec,
				XDF_FIELD_SUBJ_DESC, &subj,
				XDF_FIELD_REC_DESC, &rec,
				XDF_FIELD_NONE);

	src->ops->set_info(dst, XDF_FIELD_RECORD_DURATION, recduration,
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
		return set_xdf_error(&(bdf->xdf), errno);

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
	if (retval < -1)
		return set_xdf_error(&(bdf->xdf), errno);

	return 0;
}

static int bdf_write_channels_header(struct bdf_file* bdf, FILE* file)
{
	struct xdf_channel* ch;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-16.16s", get_bdfch(ch)->label) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_bdfch(ch)->transducter) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8.8s", get_bdfch(ch)->unit) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->physical_mm[0])) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->physical_mm[1])) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->digital_mm[0])) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8i", (int)(ch->digital_mm[1])) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-80.80s", get_bdfch(ch)->prefiltering) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-8u", bdf->xdf.ns_per_rec) < 0)
			goto error;

	for (ch = bdf->xdf.channels; ch != NULL; ch = ch->next)
		if (fprintf(file, "%-32.32s", get_bdfch(ch)->reserved) < 0)
			goto error;

	return 0;

error:
	set_xdf_error(&(bdf->xdf), errno);
	return -1;
}


static int bdf_write_header(struct xdffile* xdf)
{
	struct bdf_file* bdf = get_bdf(xdf);
	FILE* file = fdopen(dup(xdf->fd), "w");
	if (!file)
		return set_xdf_error(xdf, errno);

	// Write file header
	if (bdf_write_file_header(bdf, file))
		return -1;

	// Write each field of all channels
	if (bdf_write_channels_header(bdf, file))
		return -1;

	fflush(file);
	fclose(file);

	return 0;
}

static int bdf_read_header(struct xdffile* xdf)
{
	(void)xdf;
	return -1;
}


static int bdf_close_file(struct xdffile* xdf)
{
	int errnum = 0;
	char numrecstr[9];
	struct bdf_file* bdf = get_bdf(xdf);

	// Write the number of records
	if (xdf->ready && (xdf->mode == XDF_WRITE)) {
		snprintf(numrecstr, 9, "%-8i", xdf->nrecord);
		if ( (lseek(xdf->fd, NUMREC_FIELD_LOC, SEEK_SET) < 0)
		    || (write(xdf->fd, numrecstr, 8) < 0) )
		  	errnum = errno;
	}
	
	// Free resources
	free(bdf);

	// TODO handle failure  in close in a better way
	return set_xdf_error(NULL, errnum);
}

