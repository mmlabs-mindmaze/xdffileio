/*
 * Copyright © 2010-2011 EPFL (Ecole Polytechnique Fédérale de Lausanne)
 * Copyright © 2019 MindMaze
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <binary-io.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "streamops.h"
#include "xdfevent.h"
#include "xdfevent.h"
#include "xdffile.h"
#include "xdfio.h"
#include "xdftypes.h"

int setup_read_xdf(struct xdf* xdf, int fd);

static
int copy_configuration(struct xdf* dstfile, struct xdf* srcfile, FILE * codefile)
{
	struct xdfch *dstch, *srcch;
	int nch;
	int evttype, code;
	unsigned int desc_len;
	char* desc = NULL;

	/* Copy header and channels */
	xdf_copy_conf(dstfile, srcfile);
	nch = 0;
	while ((srcch = xdf_get_channel(srcfile, nch++))) {
		xdf_set_chconf(srcch, XDF_CF_ARRINDEX, 0, XDF_NOF);
		dstch = xdf_add_channel(dstfile, NULL);
		if (xdf_copy_chconf(dstch, srcch))
			return -1;
	}

	/* Copy event code table */
	while (1) {
		if (fread(&evttype, sizeof(evttype), 1, codefile) != 1
		    || fread(&code, sizeof(code), 1, codefile) != 1
		    || fread(&desc_len, sizeof(desc_len), 1, codefile) != 1)
			break;  /* could not read a full entry -> stop */

#if WORDS_BIGENDIAN
		evttype = bswap_32(evttype);
		code = bswap_32(code);
		desc_len = bswap_32(desc_len);
#endif

		/* read code description if one is given */
		if (desc_len > 0) {
			void * tmp = realloc(desc, desc_len + 1);
			if (tmp == NULL)
				goto error;
			desc = tmp;

		    if (fread(desc, sizeof(*desc), desc_len, codefile) != desc_len)
				break;  /* could not read a full entry -> stop */
			desc[desc_len] = '\0';
		}

		if (xdf_add_evttype(dstfile, code, desc) < 0)
			goto error;
	}

	free(desc);
	return 0;

error:
	free(desc);
	return -1;
}


#define NSAMPLE 32
static
int copy_datastream(struct xdf* dstfile, struct xdf* srcfile)
{
	void* buffer = NULL;
	int nch;
	ssize_t nssrc, nsdst = 0;
	size_t samplesize, stride[1];

	xdf_get_conf(srcfile, XDF_F_NCHANNEL, &nch, XDF_NOF);
	samplesize = nch*sizeof(double); /* double is the biggest supported type */
	buffer = malloc(samplesize * NSAMPLE);

	/* Prepare the data transfer */
	stride[0] = samplesize;
	xdf_define_arrays(srcfile, 1, stride);
	xdf_define_arrays(dstfile, 1, stride);
	xdf_prepare_transfer(srcfile);
	xdf_prepare_transfer(dstfile);


	/* Data copy loop */
	while (1) {
		nssrc = xdf_read(srcfile, NSAMPLE, buffer);
		if (nssrc <= 0)
			break;

		nsdst = xdf_write(dstfile, nssrc, buffer);
		if (nsdst < 0)
			break;
	}

	free(buffer);
	return (nssrc < 0 || nsdst < 0) ? -1 : 0;
}


static
int copy_eventtable(struct xdf* dstfile, FILE * eventfile)
{
	struct xdfevent evt;

	/* Copy the event table */
	while (1) {
		if (fread(&evt, sizeof(evt), 1, eventfile) != 1)
			break; /* could not read a full event -> stop */
#if WORDS_BIGENDIAN
		struct xdfevent be_evt = {
			.evttype = bswap_32(evt->evttype),
			.onset = bswap_64(evt->onset),
			.duration = bswap_64(evt->duration),
		};
		evt = &be_evt;
#endif
		if (xdf_add_event(dstfile, evt.evttype, evt.onset, evt.duration) < 0)
			return -1;
	}

	return 0;
}


static
int recompose_gdf(struct xdf * srcfile, FILE * eventfile, FILE * codefile,
                  struct xdf * dstfile)
{
	return (copy_configuration(dstfile, srcfile, codefile) != 0
	        || copy_datastream(dstfile, srcfile) != 0
	        || copy_eventtable(dstfile, eventfile) != 0);
}


int main(int argc, char ** argv)
{
	char * tmp;
	size_t tmp_len;
	int srcfmt;
	int exitcode = -1;
	struct xdf * srcfile = NULL;
	int dst_fd = -1;
	struct xdf * dstfile = NULL;
	FILE * eventfile = NULL;
	FILE * codefile = NULL;

	if (argc != 3) {
		fprintf(stderr,"usage: %s <in-file> <out-file>\n", argv[0]);
		return -1;
	}

	tmp_len = strlen(argv[1]);
	tmp = malloc(tmp_len + 8);
	if (tmp == NULL) {
		fprintf(stderr, "%s failed: %s\n", argv[0], strerror(errno));
		return -1;
	}
	strcpy(tmp, argv[1]);
	srcfile = xdf_open(argv[1], XDF_READ, XDF_ANY);
	if (!srcfile) {
		fprintf(stderr, "Cannot load %s: %s\n", argv[1], strerror(errno));
		free(tmp);
		return -1;
	}

	strcat(tmp, ".event");
	eventfile = fopen(tmp, "rb");

	tmp[tmp_len] = '\0';
	strcat(tmp, ".code");
	codefile = fopen(tmp, "rb");

	/* only support gdf format */
	xdf_get_conf(srcfile, XDF_F_FILEFMT, &srcfmt, XDF_NOF);
	if (srcfmt != XDF_GDF1 && srcfmt != XDF_GDF2) {
		fprintf(stderr, "%s only works with gdf files\n", argv[0]);
		goto exit;
	}
	dst_fd = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, S_IRUSR|S_IWUSR);
	if (dst_fd < 0) {
		fprintf(stderr, "%s failed to open %s for writing\n", argv[0], argv[2]);
		goto exit;
	}
	dstfile = xdf_fdopen(dst_fd, XDF_WRITE|XDF_CLOSEFD, srcfmt);

	if (!srcfile || !eventfile || !codefile || !dstfile) {
		fprintf(stderr, "%s failed to open the required files\n", argv[0]);
		goto exit;
	}

	exitcode = recompose_gdf(srcfile, eventfile, codefile, dstfile);

	if (exitcode != 0)
		fprintf(stderr, "%s failed: %s\n", argv[0], strerror(errno));

exit:
	xdf_close(dstfile);
	xdf_close(srcfile);
	if (eventfile) fclose(eventfile);
	if (codefile) fclose(codefile);
	free(tmp);
	return exitcode;
}
