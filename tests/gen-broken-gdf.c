/*
 * Copyright (C) 2019 MindMaze
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xdfio.h>
#include <mmsysio.h>

#include "refsignal.h"

static
void gen_test_gdf2(const char * filename)
{
	int i, rv, nevt;
	struct xdf *xdf = NULL;
	size_t stride[1] = {sizeof(float)};
	struct xdfch* ch;
	float data;
	struct event evt;

	xdf = xdf_open(filename, XDF_WRITE, XDF_GDF2);
	assert(xdf != NULL);
	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, FS, XDF_NOF);

	ch = xdf_add_channel(xdf, "test channel");
	xdf_set_chconf(ch, XDF_CF_ARRINDEX, 0,
	                   XDF_CF_ARRTYPE, XDFFLOAT,
	                   XDF_CF_STOTYPE, XDFINT32,
	                   XDF_CF_PMIN, -2.0,
	                   XDF_CF_PMAX, 2.0,
	                   XDF_NOF);

	xdf_define_arrays(xdf, 1, stride);
	rv = xdf_prepare_transfer(xdf);
	assert(rv == 0);

	for (i = 0; i < NEVTTYPE; i++)
		xdf_add_evttype(xdf, get_event_code(i), NULL);

	nevt = 0;
	for (i = 0; i < NS; i++) {
		data = get_signal(i);
		xdf_write(xdf, 1, &data);

		if (sample_has_event(i)) {
			get_event(nevt++, &evt);
			xdf_add_event(xdf, evt.type, evt.onset, evt.dur);
		}
	}
}


int main(void)
{
	mm_unlink("broken.gdf");
	mm_unlink("broken.gdf.code");
	mm_unlink("broken.gdf.event");
	gen_test_gdf2("broken.gdf");

	return EXIT_SUCCESS;
}

