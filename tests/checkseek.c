/*
    Copyright (C) 2013 Mindmaze SA
    Nicolas Bourdaud <nicolas.bourdaud@mindmaze.ch>

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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "checkseek.h"

#define NS_PER_REC	64
#define NREC		10
#define NCH		7
#define INC		20

static
void gensignal(int index, int32_t data[])
{
	int i;

	for (i=0; i<NCH; i++)
		data[i] = NS_PER_REC*NREC*i + index;
}


static
int genfile(int fd, int type)
{
	int32_t data[NCH];
	int retcode = -1;
	struct xdf* xdf = NULL;
	int i;
	char tmpstr[16];
	size_t strides[1] = {
		sizeof(data),
	};


	xdf = xdf_fdopen(fd, XDF_WRITE, type);
	if (!xdf) 
		goto exit;
	xdf_set_conf(xdf, XDF_F_REC_NSAMPLE, NS_PER_REC,
	                  XDF_CF_ARRTYPE, XDFINT32,
		          XDF_CF_ARRDIGITAL, 1,
	                  XDF_CF_ARRINDEX, 0,
	                  XDF_CF_ARROFFSET, 0,
	                  XDF_CF_PMIN, 0.0,
		          XDF_CF_PMAX, (double)(NS_PER_REC*NCH*NREC),
	                  XDF_NOF);

	for (i=0; i<NCH; i++) {
		sprintf(tmpstr, "channel:%i", i);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	// Make the the file ready for accepting samples
	xdf_define_arrays(xdf, 1, strides);
	if (xdf_prepare_transfer(xdf) < 0)
		goto exit;
	
	// Feed with samples
	for (i=0; i<NS_PER_REC*NREC; i++) {
		gensignal(i, data);
		if (xdf_write(xdf, 1, data) < 0)
			goto exit;
	}
	retcode = 0;

exit:
	// if phase is non zero, a problem occured
	if (retcode) 
		perror("error: ");

	// Clean the structures and ressources
	xdf_close(xdf);

	return retcode;
}


static
struct xdf* setup_read(int fd)
{
	struct xdf* xdf;
	int offset, i;
	size_t st[1];

	xdf = xdf_fdopen(fd, XDF_READ, XDF_ANY);
	if (!xdf) 
		goto error;

	offset = 0;
	for (i = 0; i < NCH; i++) {
		if (xdf_set_chconf(xdf_get_channel(xdf, i),
		                   XDF_CF_ARRTYPE, XDFINT32,
		                   XDF_CF_ARRDIGITAL, 1,
		                   XDF_CF_ARRINDEX, 0,
		                   XDF_CF_ARROFFSET, offset,
		                   XDF_NOF))
			goto error;
		offset += sizeof(float);
	}

	st[0] = (size_t)offset;
	if (xdf_define_arrays(xdf, 1, st) || xdf_prepare_transfer(xdf))
		goto error;

	return xdf;

error:
	if (xdf)
		xdf_close(xdf);
	perror("error setup for read: ");
	return NULL;
}


static
int seek_and_readcmp(struct xdf* xdf, int offset)
{
	int i;
	int32_t dataread[NCH], dataref[NCH];

	if (xdf_seek(xdf, offset, SEEK_SET) != offset)
		return -1;

	for (i=offset; i<NS_PER_REC*NREC; i++) {
		gensignal(i, dataref);
		if ( xdf_read(xdf, 1, dataread) != 1
		  || memcmp(dataread, dataref, sizeof(dataref)) )
			return -1;
		
	}

	return 0;
}


static
int open_tmpfd(char path[16])
{
	int fd;

	// Set the pattern used to generate temporary filename
	strcpy(path, "xdf-XXXXXX");

	// mkstemp() modifies array containing template to the actual path used
	if ((fd = mkstemp(path)) == -1) {
		fprintf(stderr, "mkstemp(%s) failed\n", path);
		return -1;
	}

	return fd;
}


int test_seek(int type)
{
	int offset, fd, ret = -1;
	struct xdf* xdf;
	char path[16];

	if ( (fd = open_tmpfd(path)) == -1
	  || genfile(fd, type)
	  || lseek(fd, 0, SEEK_SET) == -1
	  || !(xdf = setup_read(fd)) )
	  	goto exit;
	
	for (offset = 0; offset < NS_PER_REC*NREC; offset += INC)
		if (seek_and_readcmp(xdf, offset)) {
			fprintf(stderr, "failed at offset %i\n", offset);
			goto exit;
		}
	
	ret = 0;

exit:
	if (fd >= 0) {
		close(fd);
		unlink(path);
	}
	if (ret)
		perror("Failed in testseek: ");
	return ret;
}

