#include "xdffile.h"
#include "bdf.h"
#include <stddef.h>

struct dataformat_entry {
	enum xdffiletype type;
	struct xdf* (*alloc_file)(void);
	int (*is_same_type)(const unsigned char*);
};

// Declaration array
struct dataformat_entry support_datafmt[] = {
	{
		.type=XDF_BDF,
		.alloc_file = xdf_alloc_bdffile,
		.is_same_type = xdf_is_bdffile
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
