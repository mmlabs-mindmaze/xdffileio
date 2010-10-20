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
#include <xdfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define CHUNK_NS	8
#define FILENAME	"ref128-13-97-50-11-7-1.bdf"
struct xdf *xdfr = NULL, *xdft = NULL;
double *buffr = NULL, *bufft = NULL;

static int keepch(int i, int nchskip)
{
	if (nchskip == 0)
		return 1;
	return (i / nchskip)%2;
}

static int setup_files(const char* filename, int nchskip, int* nchr, int* ncht)
{
	int i, offset, arr;
	size_t st[1];
	int arrtp = XDFDOUBLE;
	struct xdfch* ch;

	xdfr = xdf_open(filename, XDF_READ, XDF_ANY);
	if (!xdfr)
		goto error;
	xdft = xdf_open(filename, XDF_READ, XDF_ANY);
	if (!xdft)
		goto error;

	// Setup reference file
	offset = i = 0;
	*nchr = 0;
	while ((ch = xdf_get_channel(xdfr, i))) {
		if (xdf_set_chconf(ch, XDF_CF_ARRTYPE, arrtp,
				    XDF_CF_ARROFFSET, offset,
				    XDF_NOF))
			goto error;
		offset += sizeof(double);
		*nchr += 1;
		i++;
	}
	st[0] = (unsigned int)offset;
	if (xdf_define_arrays(xdfr, 1, st) || xdf_prepare_transfer(xdfr))
		goto error;

	
	// Setup test file
	offset = i = 0;
	*ncht = 0;
	while ((ch = xdf_get_channel(xdft, i))) {
		// Skip block of nchskip channel
		arr = keepch(i, nchskip) ? 0 : -1;

		if (xdf_set_chconf(ch, XDF_CF_ARRTYPE, arrtp,
		                    XDF_CF_ARRINDEX, arr,
				    XDF_CF_ARROFFSET, offset,
				    XDF_NOF))
			goto error;
		if (arr == 0) {
			offset += sizeof(double);
			*ncht += 1;
		}
		i++;
	}
	st[0] = (unsigned int)offset;
	if (xdf_define_arrays(xdft, 1, st) || xdf_prepare_transfer(xdft))
		goto error;
	
	return 0;

error:
	fprintf(stderr, "\terror during file preparation\n");
	return -1;
}


int move_to_offset(off_t offset)
{
	ssize_t ns;
	size_t reqns, nstot = 0;
	
	while (nstot < (size_t)offset) {
		reqns = (offset - nstot < CHUNK_NS) ?
				(offset - nstot) : CHUNK_NS;
		ns = xdf_read(xdfr, reqns, buffr);
		if (ns <= 0)
			goto error;

		nstot += ns;
	}

	if (xdf_seek(xdft, offset, SEEK_SET) < 0)
		goto error;

	return 0;
error:
	fprintf(stderr, "\terror while seeking\n");
	return -1;
}


static int diffdata(int nchskip, unsigned int nchr, 
                    unsigned int ncht, size_t ns)
{
	unsigned int ichr, icht, is;

	for (is=0; is<ns; is++) {
		icht = 0;
		for (ichr=0; ichr<nchr; ichr++) {
			if (!keepch(ichr, nchskip))
				continue;

			if (buffr[is*nchr + ichr] != bufft[is*ncht+ icht])
				return -1;
			icht++;
		}
	}

	return 0;
}

int test_seek_skip(const char* filename, off_t offset, int nchskip)
{
	int nchr, ncht, retcode = -1;
	ssize_t nsr, nst;
	buffr = NULL;
	bufft = NULL;

	if (setup_files(filename, nchskip, &nchr, &ncht))
		goto exit;
	
	buffr = malloc(nchr*CHUNK_NS*sizeof(*buffr));
	bufft = malloc(ncht*CHUNK_NS*sizeof(*bufft));
	if (!buffr || !bufft)
		goto exit;

	if (move_to_offset(offset))
		goto exit;

	while (1) {
		nsr = xdf_read(xdfr, CHUNK_NS, buffr);
		nst = xdf_read(xdft, CHUNK_NS, bufft);
		if ((nsr < 0) || (nst < 0) || (nsr != nst)) {
			fprintf(stderr, "\tfailure during reading\n");
			goto exit;
		}

		if (diffdata(nchskip, nchr, ncht, nsr)) {
			fprintf(stderr, "\tData differ\n");
			retcode = -2;
			goto exit;
		}

		if (nsr == 0)
			break;
	}

	retcode = 0;
	
exit:
	if (retcode == -1)
		fprintf(stderr, "\terror caught: (%i) %s\n", 
					errno, strerror(errno));
	free(buffr);
	free(bufft);
	xdf_close(xdfr);
	xdf_close(xdft);
	xdfr = xdft = NULL;
	return retcode;
}


int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	char reffilename[512];
	sprintf(reffilename, "%s/%s", getenv("srcdir"), FILENAME);

	fprintf(stderr, "\tfirst test\n");
	if (test_seek_skip(reffilename, 0, 0))
		return 1;

	fprintf(stderr, "\tsecond test\n");
	if (test_seek_skip(reffilename, 50, 0))
		return 1;

	fprintf(stderr, "\tthird test\n");
	if (test_seek_skip(reffilename, 50, 4))
		return 1;
	
	return 0;
}


