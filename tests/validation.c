#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <xdfio.h>
#include "validation.h"

int test_closest_type(struct xdf* xdf, unsigned int ntype,
		 enum xdftype *allowed_type)
{
	unsigned int i, j;
	int is_supported, ret;

	for (i=0; i<XDF_NUM_DATA_TYPES; i++) {
		ret = xdf_closest_type(xdf, i);
		if (ret < 0)
			return -1;

		// Check ret is supported type
		is_supported = 0;
		for (j=0; j<ntype; j++) {
			if ((enum xdftype)ret == allowed_type[j])
				is_supported = 1;
		}
		if (!is_supported) {
			fprintf(stderr,	"\txdf_closest_type returns unsupported type\n");
			return -1;
		}
	}
	return 0;
}


int test_validation_param(enum xdffiletype ftype, unsigned int ntype,
					enum xdftype *allowed_type)
{
	int r1, r2, is_supported, retcode = 0;
	enum xdftype curr;
	unsigned int i, j;
	struct xdf *xdf;
	struct xdfch *ch;

	xdf = xdf_open("datafile", XDF_WRITE, ftype);
	if (!xdf) {
		fprintf(stderr, "\nCannot create an XDF file: (%i) %s\n",
				errno, strerror(errno));
	}

	ch = xdf_add_channel(xdf, NULL);

	for (i=0; i<XDF_NUM_DATA_TYPES; i++) {
		curr = i;
		r1 = xdf_set_chconf(ch, XDF_CF_STOTYPE, curr, XDF_NOF);
		r2 = xdf_set_conf(xdf, XDF_CF_STOTYPE, curr, XDF_NOF);

		is_supported = 0;
		for (j=0; j<ntype; j++) {
			if (curr == allowed_type[j])
				is_supported = 1;
		}

		if ((is_supported && (r1 || r2))
		    || (!is_supported && (!r1 || !r2)))
			retcode = -1;
	}

	if (!retcode)
		retcode = test_closest_type(xdf, ntype, allowed_type);

	xdf_close(xdf);
	
	unlink("datafile");
	return retcode;
}
