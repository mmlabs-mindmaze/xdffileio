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

#include "refsignal.h"

#define check(expr) \
	do { if (!(expr)) { \
		fprintf(stderr, "\"" #expr "\" failed."); \
		fflush(stderr); \
		abort(); \
	} } while (0)

struct evttype {
	int code;
	const char* desc;
};

static
void check_fixed_gdf2(const char * filename)
{
	int i, nevent, nevent_expected, code, rv;
	unsigned int type;
	struct xdf *xdf = NULL;
	size_t stride[1] = {sizeof(float)};
	struct xdfch* ch;
	float data, data_ref;
	struct event evt_ref;
	struct evttype types[NEVTTYPE];
	double onset, dur;

	xdf = xdf_open(filename, XDF_READ, XDF_GDF2);
	assert(xdf != NULL);

	ch = xdf_get_channel(xdf, 0);
	xdf_set_chconf(ch, XDF_CF_ARRINDEX, 0,
	                   XDF_CF_ARRTYPE, XDFFLOAT,
	                   XDF_CF_ARRDIGITAL, 0,
	                   XDF_NOF);


	xdf_define_arrays(xdf, 1, stride);
	rv = xdf_prepare_transfer(xdf);
	assert(rv == 0);

	// Verify data signal match up the end minus 2 records
	nevent_expected = 0;
	for (i = 0; i < NS - 2*FS; i++) {
		data_ref = get_signal(i);
		xdf_read(xdf, 1, &data);
		check(fabsf(data - data_ref) < 1e-6f);
		if (sample_has_event(i))
			nevent_expected++;
	}

	// Get registered event types
	for (i = 0; i < NEVTTYPE; i++)
		xdf_get_evttype(xdf, i, &types[i].code, &types[i].desc);

	// Check events
	xdf_get_conf(xdf, XDF_F_NEVENT, &nevent, XDF_NOF);
	check(nevent > nevent_expected);
	for (i = 0; i < nevent; i++) {
		xdf_get_event(xdf, i, &type, &onset, &dur);
		code = types[type].code;

		get_event(i, &evt_ref);
		check((onset - evt_ref.onset) < 1. / FS);
		check(code == get_event_code(evt_ref.type));
	}

	xdf_close(xdf);
}


int main(void)
{
	check_fixed_gdf2("fixed.gdf");

	return EXIT_SUCCESS;
}

