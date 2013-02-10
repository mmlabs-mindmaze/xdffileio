/*
    Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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


/*  Example : copy_datafile.c
 *
 * This program demonstrates how to copy a data file supported by the
 * xdffileio library into another file format.
 * This shows how to:
 *	- Read a file
 *	- Use the events
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
	[XDF_BDF] = "BDF",
	[XDF_GDF1] = "GDF1",
	[XDF_GDF2] = "GDF2"
};


int copy_configuration(struct xdf* dst, struct xdf* src)
{
	struct xdfch *dstch, *srcch;
	int i, nch, nevtcode, evtcode;
	const char* desc;

	// Copy header and channels
	xdf_copy_conf(dst, src);
	nch = 0;
	while ((srcch = xdf_get_channel(src, nch++))) {
		dstch = xdf_add_channel(dst, NULL);
		if (xdf_copy_chconf(dstch, srcch)) 
			return -1;
	}
	
	// Copy event code table
	xdf_get_conf(src, XDF_F_NEVTTYPE, &nevtcode, XDF_NOF);
	for (i=0; i<nevtcode; i++) {
		if (xdf_get_evttype(src, i, &evtcode, &desc) < 0
		  || xdf_add_evttype(dst, evtcode, desc) < 0)
			return -1;
	}

	return 0;
}


int copy_datastream(struct xdf* dst, struct xdf* src)
{
	void* buffer = NULL;
	int nch;
	ssize_t nssrc, nsdst = 0;
	size_t samplesize, stride[1];

	xdf_get_conf(src, XDF_F_NCHANNEL, &nch, XDF_NOF);
	samplesize = nch*sizeof(double); /* double is the biggest supported type */
	buffer = malloc(samplesize*NSAMPLE);

	/* Prepare the data transfer */
	stride[0] = samplesize;
	xdf_define_arrays(src, 1, stride);
	xdf_define_arrays(dst, 1, stride);
	xdf_prepare_transfer(src);
	xdf_prepare_transfer(dst);


	/* Data copy loop */
	while (1) {
		nssrc = xdf_read(src, NSAMPLE, buffer);
		if (nssrc <= 0) 
			break;

		nsdst = xdf_write(dst, nssrc, buffer);
		if (nsdst < 0)
			break;
	}

	free(buffer);
	return (nssrc < 0 || nsdst < 0) ? -1 : 0;
}


int copy_eventtable(struct xdf* dst, struct xdf* src)
{
	int nevent, i;
	unsigned int evttype;
	double onset, duration;

	/* Copy the event table */
	xdf_get_conf(src, XDF_F_NEVENT, &nevent, XDF_NOF);
	for (i=0; i<nevent; i++) {
		if (xdf_get_event(src, i, &evttype, &onset, &duration) < 0
		  || xdf_add_event(dst, evttype, onset, duration) < 0)
			return -1;
	}

	return 0;
}


int copy_xdf(const char* genfilename, const char* reffilename, int dstfmt)
{
	struct xdf *dst = NULL, *src = NULL;
	int srcfmt, retcode = -1;

	/* Open the source file*/
	src = xdf_open(reffilename, XDF_READ, XDF_ANY);
	if (src == NULL)
		goto exit;
	xdf_get_conf(src, XDF_F_FILEFMT, &srcfmt, XDF_NOF);

	/* Create the destination file */
	if (dstfmt == XDF_ANY)
		dstfmt = srcfmt;
	dst = xdf_open(genfilename, XDF_WRITE, dstfmt);
	if (dst == NULL)
		goto exit;

	/* Copy the metadata (time, location, channel layout, event types) */
	if (copy_configuration(dst, src))
		goto exit;

	/* Copy channel data */
	if (copy_datastream(dst, src))
		goto exit;

	/* Copy event table */
	if (copy_eventtable(dst, src))
		goto exit;

	retcode = 0;

exit:
	xdf_close(src);
	xdf_close(dst);

	return retcode;
}


int interpret_type(const char* req)
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
		"The first forms copy srcfilename into dstfilename\n"
		"filetype is the file format of the destination and "
		"can be one of these types:\n"
		"\t\t- same \tuse the same type of the source\n"
		"\t\t- EDF \tEuropean Data Format\n"
		"\t\t- BDF \tBiosemi Data Format\n"
		"\t\t- GDF1 \tGeneral Data Format version 1\n"
		"\t\t- GDF2 \tGeneral Data Format version 2\n"
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

		default:
			print_usage(stdout, argv[0]);
			return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

	if (argc - optind != 2) {
		print_usage(stdout, argv[0]);
		return EXIT_FAILURE;
	}

	
	ret = copy_xdf(argv[optind+1], argv[optind], dstfmt);

	return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

