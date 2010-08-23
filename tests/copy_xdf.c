#include <xdfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "copy_xdf.h"

#define NSAMPLE	23

int copy_xdf(const char* genfilename, const char* reffilename, int fformat)
{
	struct xdf *dst = NULL, *src = NULL;
	struct xdfch *dstch, *srcch;
	unsigned int ich = 0, samplesize;
	int nch, retcode = -1;
	void* buffer = NULL;
	ssize_t nssrc, nsdst;
	size_t nstot, stride[1];
	int offset;

	src = xdf_open(reffilename, XDF_READ, fformat);
	if (!src) {
		fprintf(stderr, 
		       "\tFailed opening reference file: (%i) %s\n",
		       errno, strerror(errno));
		goto exit;
	}
		
	dst = xdf_open(genfilename, XDF_WRITE, fformat);
	if (!dst) {
		fprintf(stderr,
		       "\tFailed opening file for writing: (%i) %s\n",
		       errno, strerror(errno));
		goto exit;
	}

	// Copy header and configuration
	xdf_copy_conf(dst, src);
	offset = 0;
	while ((srcch = xdf_get_channel(src, ich))) {
		xdf_set_chconf(srcch, XDF_CF_ARRINDEX, 0, XDF_NOF);
		dstch = xdf_add_channel(dst, NULL);
		if (xdf_copy_chconf(dstch, srcch)) {
			fprintf(stderr, "\tFailed copying channel %i: (%i) %s\n",
			        ich, errno, strerror(errno));
			goto exit;
		}
		ich++;
	}
	xdf_get_conf(src, XDF_F_NCHANNEL, &nch, XDF_NOF);
	if (nch != (int)ich) {
		fprintf(stderr, "\tich=%u, nch=%i\n", ich, nch);
		goto exit;
	}

	samplesize = nch*4;
	buffer = malloc(samplesize*NSAMPLE);
	stride[0] = samplesize;
	xdf_define_arrays(src, 1, stride);
	xdf_define_arrays(dst, 1, stride);
	xdf_prepare_transfer(src);
	xdf_prepare_transfer(dst);

	if ( (xdf_seek(src, 1000, SEEK_CUR) < 0)
	    || (xdf_seek(src, 0, SEEK_SET) < 0) ) {
		fprintf(stderr, "\txdf_seek function failed: %s\n", strerror(errno));
		goto exit;
	}

	nstot = 0;
	while (1) {
		nssrc = xdf_read(src, NSAMPLE, buffer);
		if (nssrc < 0) {
			fprintf(stderr, 
			       "\tfailed reading a chunk of %i samples "
			       "after %u samples\nerror caught (%i), %s\n",
			       (int)NSAMPLE, (unsigned int)nstot,
			       errno, strerror(errno));
			goto exit;
		}
		if (nssrc == 0)
			break;
		nsdst = xdf_write(dst, nssrc, buffer);
		if (nsdst != nssrc) {
			fprintf(stderr, 
			       "\tfailed writing a chunk of %i samples "
			       "after %i samples\nerror caught (%i), %s\n",
			       (unsigned int)nssrc, (unsigned int)nstot, 
			       errno, strerror(errno));
			goto exit;
		}
		nstot += nsdst;
	}
	retcode = 0;

exit:
	free(buffer);
	xdf_close(src);
	xdf_close(dst);

	return retcode;
}
