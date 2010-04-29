#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "xdferror.h"
#include "xdfio.h"
#include "xdftypes.h"
#include "xdfchannels.h"
#include "xdffile.h"

struct xdf_channel* xdf_get_channel(struct xdffile* xdf, unsigned int index)
{
	struct xdf_channel* ch = xdf->channels;
	unsigned int ich = 0;

	while (ch && (ich<index)) {
		ich++;
		ch = ch->next;
	}

	return ch;
}


int xdf_setconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int retval = 0;
	const char* string = NULL;
	double dval;
	unsigned int ival;
	enum xdftype type;

	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (field != XDF_CHFIELD_NONE) {
		switch (field) {
		case XDF_CHFIELD_ARRAY_INDEX:	/* unsigned int */
		case XDF_CHFIELD_ARRAY_OFFSET:	/* unsigned int */
			ival = va_arg(ap, unsigned int);
			retval = ch->owner->set_channel_proc(ch, field, ival);
			break;

		case XDF_CHFIELD_ARRAY_TYPE:		/* enum xdftype */
		case XDF_CHFIELD_STORED_TYPE:		/* enum xdftype */
			type = va_arg(ap, enum xdftype);
			retval = ch->owner->set_channel_proc(ch, field, type);
			break;

		case XDF_CHFIELD_STORED_LABEL:       /* const char*  */
			string = va_arg(ap, const char*);
			retval = ch->owner->set_channel_proc(ch, field, string);
			break;

		case XDF_CHFIELD_PHYSICAL_MIN:	/* double 	*/
		case XDF_CHFIELD_PHYSICAL_MAX:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MIN:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MAX:	/* double 	*/
			dval = va_arg(ap, double);
			retval = ch->owner->set_channel_proc(ch, field, dval);
			break;
		default:
			break;
		}
	}
	va_end(ap);
	
	return retval;
}


int xdf_getconf_channel(struct xdf_channel* ch, enum xdfchfield field, ...)
{
	va_list ap;
	int retval = 0, out = 0;
	const char** string = NULL;
	double* dval;
	unsigned int* ival;
	enum xdftype* type;

	if (ch == NULL) 
		return set_xdf_error(NULL, EINVAL);

	va_start(ap, field);
	while (!out) {
		switch (field) {
		case XDF_CHFIELD_NONE:
			out = 1;
			break;

		case XDF_CHFIELD_ARRAY_INDEX:	/* unsigned int */
		case XDF_CHFIELD_ARRAY_OFFSET:	/* unsigned int */
			ival = va_arg(ap, unsigned int*);
			retval = ch->owner->get_channel_proc(ch, field, ival);
			break;

		case XDF_CHFIELD_ARRAY_TYPE:		/* enum xdftype */
		case XDF_CHFIELD_STORED_TYPE:		/* enum xdftype */
			type = va_arg(ap, enum xdftype*);
			retval = ch->owner->get_channel_proc(ch, field, type);
			break;

		case XDF_CHFIELD_STORED_LABEL:       /* const char*  */
			string = va_arg(ap, const char**);
			retval = ch->owner->get_channel_proc(ch, field, string);
			break;

		case XDF_CHFIELD_PHYSICAL_MIN:	/* double 	*/
		case XDF_CHFIELD_PHYSICAL_MAX:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MIN:	/* double 	*/
		case XDF_CHFIELD_DIGITAL_MAX:	/* double 	*/
			dval = va_arg(ap, double*);
			retval = ch->owner->get_channel_proc(ch, field, dval);
			break;
		}
	}
	va_end(ap);
	
	return retval;
}


int xdf_copy_channel(struct xdf_channel* dst, struct xdf_channel* src)
{
	
	if (!dst || !src)
		return set_xdf_error(NULL, EINVAL);

	return dst->owner->copy_channel_proc(dst, src);
}


struct xdf_channel* xdf_add_channel(struct xdffile* xdf)
{
	struct xdf_channel** curr = &(xdf->channels);
	struct xdf_channel* ch;
	unsigned int offset;
	enum xdftype type;

	// go to the end of the list of channel of the xdffile
	while (*curr)
		curr = &((*curr)->next);

	// Allocate new channel
	ch = xdf->alloc_channel_proc();
	if (!ch)
		return NULL;

	// Init the new channel with the previous one
	if (*curr) {
		xdf_copy_channel(ch, *curr);
		xdf_getconf_channel(ch, XDF_CHFIELD_ARRAY_OFFSET, &offset,
		                        XDF_CHFIELD_ARRAY_TYPE, &type,
					XDF_CHFIELD_NONE);
		offset += get_data_size(type);
		xdf_setconf_channel(ch, XDF_CHFIELD_ARRAY_OFFSET, offset,
					XDF_CHFIELD_NONE);
	}

	// Link the channel to the end
	ch->next = NULL;
	*curr = ch;

	return ch;
}


