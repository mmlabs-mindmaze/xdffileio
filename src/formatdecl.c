#include <stddef.h>
#include "xdffile.h"
#include "ebdf.h"

struct dataformat_entry {
	enum xdffiletype type;
	struct xdf* (*alloc_file)(void);
	int (*is_same_type)(const unsigned char*);
};

// Declaration array
static struct dataformat_entry support_datafmt[] = {
	{
		.type=XDF_BDF,
		.alloc_file = xdf_alloc_bdffile,
		.is_same_type = xdf_is_bdffile
	},
	{
		.type=XDF_EDF,
		.alloc_file = xdf_alloc_edffile,
		.is_same_type = xdf_is_edffile
	}
};

static unsigned int num_support_datafmt = sizeof(support_datafmt) 
				/ sizeof(support_datafmt[0]);


enum xdffiletype xdf_guess_filetype(const unsigned char* magickey)
{
	unsigned int i;
	enum xdffiletype type = XDF_ANY;

	for (i=0; i<num_support_datafmt; i++) {
		if (support_datafmt[i].is_same_type(magickey)) {
			type = support_datafmt[i].type;
			break;
		}
	}
	return type;
}

struct xdf* xdf_alloc_file(enum xdffiletype type)
{
	unsigned int i;
	for (i=0; i<num_support_datafmt; i++) {
		if (type == support_datafmt[i].type)
			return support_datafmt[i].alloc_file();
	}

	return NULL;
}
