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
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define NSAMPLE	32
const char filetype_args[XDF_NUM_FILE_TYPES][8] = {
	[XDF_ANY] = "same",
	[XDF_EDF] = "EDF",
	[XDF_BDF] = "BDF"
};

int copy_xdf(const char* genfilename, const char* reffilename, int dstfmt)
{
	struct xdf *dst = NULL, *src = NULL;
	struct xdfch *dstch, *srcch;
	int nch, retcode = -1;
	void* buffer = NULL;
	ssize_t nssrc, nsdst, samplesize;
	size_t nstot, stride[1];
	int step;

	/* Open the source file*/
	step = 0;
	src = xdf_open(reffilename, XDF_READ, XDF_ANY);
	if (src == NULL)
		goto exit;

	/* Create the destination file */
	step++;
	if (dstfmt == XDF_ANY)
		xdf_get_conf(src, XDF_F_FILEFMT, &dstfmt, XDF_NOF);

	dst = xdf_open(genfilename, XDF_WRITE, dstfmt);
	if ((dst == NULL) || (src == NULL))
		goto exit;

	// Copy header and configuration
	step++;
	xdf_copy_conf(dst, src);
	nch = 0;
	while ((srcch = xdf_get_channel(src, nch++))) {
		dstch = xdf_add_channel(dst, NULL);
		if (xdf_copy_chconf(dstch, srcch)) 
			goto exit;
	}

	samplesize = nch*sizeof(double); /* double is the biggest ytype */
	buffer = malloc(samplesize*NSAMPLE);
	stride[0] = samplesize;
	xdf_define_arrays(src, 1, stride);
	xdf_define_arrays(dst, 1, stride);
	xdf_prepare_transfer(src);
	xdf_prepare_transfer(dst);

	/* Data copy loop */
	nstot = 0;
	while (1) {
		nssrc = xdf_read(src, NSAMPLE, buffer);
		if (nssrc < 0) 
			goto exit;

		/* if nssrc == 0, the end of file has been reached */
		if (nssrc == 0)
			break;

		nsdst = xdf_write(dst, nssrc, buffer);
		if (nsdst < 0)
			goto exit;

		if (nsdst != nssrc) {
			fprintf(stderr, "Failed to copy the whole data\n");
			break;
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


static int interpret_type(const char* req)
{
	int i;

	for (i=0; i<XDF_NUM_FILE_TYPES; i++) {
		if (strcmp(filetype_args[i], req)==0)
			return i;
	}
	fprintf(stderr,"Unknown filetype,"
	               " use same filetype than the source\n");
	return XDF_ANY;
}

void print_usage(FILE* f, const char* execname)
{
	fprintf(f, 
	        "Syntax:\n"
		"\t%s [-t filetype] srcfilename dstfilename\n"
		"\t%s -h\n"
		"The first forms copy srcfilename into dstfilename"
		"\tfiletype is the file format of the destination and"
		"can be one of these types:\n"
		"\t\t- same \tuse the  of the file format source\n"
		"\t\t- EDF \tEuropean Data Format\n"
		"\t\t- BDF \tBiosemi Data Format\n"
		"The second forms displays this help\n",
		execname, execname);
}


int main(int argc, char* argv[])
{
	int dstfmt, opt, ret;
	dstfmt = XDF_ANY;

	while((opt = getopt(argc, argv, "t:h")) != -1) {
		switch (opt) {
		case 't':
			dstfmt = interpret_type(optarg);	
			break;

		case 'h':
			print_usage(stdout, argv[0]);
			return EXIT_SUCCESS;

		default:
			print_usage(stderr, argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (argc - optind != 2) {
		print_usage(stderr, argv[0]);
		return EXIT_FAILURE;
	}

	
	ret = copy_xdf(argv[optind+1], argv[optind], dstfmt);

	return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
