#include <xdfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "copy_xdf.h"

#define NSAMPLE	23

int copy_xdf(const char* genfilename, const char* reffilename, int fformat)
{
	struct xdf *dst, *src;
	struct xdfch *dstch, *srcch;
	unsigned int ich = 0, samplesize, stride[1];
	int nch;
	void* buffer;
	ssize_t ns;
	int offset;

	src = xdf_open(reffilename, XDF_READ, fformat);
	if (!src) {
		fprintf(stderr, 
		       "\tFailed opening reference file: (%i) %s\n",
		       errno, strerror(errno));
		return 1;
	}
		
	dst = xdf_open(genfilename, XDF_WRITE, fformat);
	if (!dst) {
		fprintf(stderr,
		       "\tFailed opening file for writing: (%i) %s\n",
		       errno, strerror(errno));
		return 1;
	}

	// Copy header and configuration
	xdf_copy_conf(dst, src);
	offset = 0;
	while ((srcch = xdf_get_channel(src, ich))) {
		dstch = xdf_add_channel(dst, NULL);
		/*if (ich != NEEG+NEXG) {
			xdf_set_chconf(srcch, 
			            XDF_CF_ARRDIGITAL, 0,
		                    XDF_CF_ARRTYPE, XDFFLOAT,
				    XDF_CF_ARROFFSET, offset,
				    XDF_NOF);
			offset += sizeof(float);
		} else {
			xdf_set_chconf(srcch, 
			            XDF_CF_ARRDIGITAL, 0,
		                    XDF_CF_ARRTYPE, XDFINT32,
				    XDF_CF_ARROFFSET, offset,
				    XDF_NOF);
			offset += sizeof(int32_t);
		}*/
		xdf_copy_chconf(dstch, srcch);
		ich++;
	}
	xdf_get_conf(src, XDF_F_NCHANNEL, &nch, XDF_NOF);
	if (nch != (int)ich) {
		fprintf(stderr, "\tich=%u, nch=%i\n", ich, nch);
		return 1;
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
		return 2;
	}

	while (1) {
		ns = xdf_read(src, NSAMPLE, buffer);
		if (ns <= 0)
			break;
		xdf_write(dst, ns, buffer);
	}

	free(buffer);
	xdf_close(src);
	xdf_close(dst);

	return 0;
}
