/*
    Copyright (C) 2010,2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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
//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "filecmp.h"

#define MAXFSIZE	60000	
#define NSAMPLE		17

static off_t offskip[] = {168, 184, 236, 244};
static int nskip = sizeof(offskip)/(2*sizeof(offskip[0]));

int trycopy_xdffile(const char* genfilename, const char* reffilename, unsigned int fsize)
{
	unsigned int samwarn = 0, currns = 0;
	struct xdf *dst = NULL, *src = NULL;
	struct xdfch *dstch, *srcch;
	unsigned int ich = 0, samplesize;
	size_t stride[1];
	int nch, retcode = -1;
	int recns;
	void* buffer = NULL;
	ssize_t nssrc, nsdst;
	int offset;

	src = xdf_open(reffilename, XDF_READ, XDF_ANY);
	if (!src) {
		fprintf(stderr, 
		       "\tFailed opening reference file: (%i) %s\n",
		       errno, strerror(errno));
		goto exit;
	}
		
	dst = xdf_open(genfilename, XDF_WRITE, XDF_BDF);
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
		dstch = xdf_add_channel(dst, NULL);
		if (xdf_copy_chconf(dstch, srcch)) {
			fprintf(stderr, "\tFailed copying channel %i: (%i) %s\n",
			        ich, errno, strerror(errno));
			goto exit;
		}
		ich++;
	}
	xdf_get_conf(src, XDF_F_NCHANNEL, &nch,
	                  XDF_F_REC_NSAMPLE, &recns, XDF_NOF);
	if (nch != (int)ich) {
		fprintf(stderr, "\tich=%u, nch=%i\n", ich, nch);
		goto exit;
	}

	samwarn = (fsize - 256*(nch+1)) / (nch*3);
	samplesize = nch*4;
	buffer = malloc(samplesize*NSAMPLE);
	stride[0] = samplesize;
	xdf_define_arrays(src, 1, stride);
	xdf_define_arrays(dst, 1, stride);
	xdf_prepare_transfer(src);
	xdf_prepare_transfer(dst);

	currns = 0;
	while (1) {
		nssrc = xdf_read(src, NSAMPLE, buffer);
		if (nssrc < 0) 
			goto exit;
		
		if (nssrc == 0)
			break;
		nsdst = xdf_write(dst, nssrc, buffer);
		if (nsdst != nssrc) { 
			retcode = 1;
			goto exit;
		}
		
		currns += nsdst;
	}

exit:
	if ( (currns > samwarn) 
	    && (currns < samwarn+2*recns) 
	    && (retcode == 1)
	    && (errno == EFBIG) )
		retcode = 0;

	fprintf(stderr, "\tError caught: %s\n"
	                "\t\twhen adding %i samples after %u samples\n"
	                "\t\tsample lost: %u (record length: %i samples)\n"
			"\t\tsamwarn=%u, retcode=%i\n", 
	                   strerror(errno), NSAMPLE, currns,
			   currns+NSAMPLE - samwarn, recns,
			   samwarn, retcode);

	// Clean the structures and ressources
	xdf_close(src);
	xdf_close(dst);
	free(buffer);

	return retcode;
}

int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt;
	off_t pos = 0;
	char genfilename[] = "essaiw.bdf";
	char basename[] = "ref128-13-97-50-11-7-1.bdf";
	char reffilename[128];
	struct rlimit lim;

	while ((opt = getopt(argc, argv, "k")) != -1) {
		switch (opt) {
		case 'k':
			keep_file = 1;
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-k]\n",
				argv[0]);
			exit(EXIT_FAILURE);
		}
	}


	fprintf(stderr, "\tVersion : %s\n", xdf_get_string());

	// Create the filename for the reference
	snprintf(reffilename, sizeof(reffilename),"%s/%s",
		 getenv("srcdir"),basename);


	signal(SIGXFSZ, SIG_IGN);
	getrlimit(RLIMIT_FSIZE, &lim);
	lim.rlim_cur = MAXFSIZE; 
	if (setrlimit(RLIMIT_FSIZE, &lim)) {
		fprintf(stderr, "limit could not have been set (error:%s)\n"
		                "Assume success without actual test\n",
				strerror(errno));
		return EXIT_SUCCESS;
	}

	unlink(genfilename);

	retcode = trycopy_xdffile(genfilename, reffilename, MAXFSIZE);
	if (!retcode) {
		if (!cmp_files(genfilename, reffilename, nskip, offskip, &pos) 
		    || (pos != MAXFSIZE) )
			retcode = -1;
	}

	if (!keep_file)
		unlink(genfilename);

	return retcode;
}

