/*
 * Copyright © 2020 MindMaze
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

#include <errno.h>
#include <mmsysio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "streamops.h"
#include "xdfevent.h"
#include "xdfevent.h"
#include "xdffile.h"
#include "xdfio.h"
#include "xdftypes.h"
#include "common.h"

#define IDENT 4

static
void usage(char const * progname)
{
	printf("%s <filename>\n", progname);
}

static
char const * get_xdf_filetype_str(int type)
{
	static char const * filetype_names[] = {
		[XDF_ANY] = "any",  // should not be asked
		[XDF_EDF] = "edf",
		[XDF_EDFP] = "edfp",
		[XDF_BDF] = "bdf",
		[XDF_GDF1] = "gdf1",
		[XDF_GDF2] = "gdf2",
	};

	if (type <= 0 || type >= XDF_NUM_FILE_TYPES)
		return "unknown";

	return filetype_names[type];
}

/* timestamp is a double in the specs ... */
static
void dump_timestamp(char const * key, double timestamp)
{
	time_t tmp = (time_t) timestamp;
	printf("%s: %s", key, ctime(&tmp));
}

static
char const * get_stotype_str(enum xdftype type)
{
	static char const * type_names[] = {
		[XDFINT8] = "int8",
		[XDFUINT8] = "uint8",
		[XDFINT16] = "int16",
		[XDFUINT16] = "uint16",
		[XDFINT24] = "int24",
		[XDFUINT24] = "uint24",
		[XDFINT32] = "int32",
		[XDFUINT32] = "uint32",
		[XDFFLOAT] = "float",
		[XDFDOUBLE] = "double",
		[XDFINT64] = "int64",
		[XDFUINT64] = "uint64",
	};

	if (type  < 0 || type >= XDF_NUM_DATA_TYPES)
		return "unknown";

	return type_names[type];
}

static
int dump_channel(struct xdfch const * ch, int index, int ident)
{
	int rv;
	char * name, * unit_label;
	double phy_min, phy_max, dig_min, dig_max;
	enum xdftype tmp_sto_type;

	rv = xdf_get_chconf(ch,
	                    XDF_CF_LABEL, &name,
	                    XDF_CF_UNIT, &unit_label,
	                    XDF_CF_PMIN, &phy_min,
	                    XDF_CF_PMAX, &phy_max,
	                    XDF_CF_DMIN, &dig_min,
	                    XDF_CF_DMAX, &dig_max,
	                    XDF_CF_STOTYPE, &tmp_sto_type,
	                    XDF_NOF);

	if (rv != 0)
		return -1;

	printf("%.*s%15d, %15s, %15s, %15s, %+15d, %+15d, %+15d, %+15d,\n",
	       ident, " ",
	       index,
	       name,
	       unit_label,
	       get_stotype_str(tmp_sto_type),
	       (int) phy_min,
	       (int) phy_max,
	       (int) dig_min,
	       (int) dig_max);
	return 0;
}

static
int dump_header(struct xdf const * f)
{
	int i;
	int rec_ns, nrec, nch;
	int fs;
	struct xdfch const * ch;
	int tmp_type;
	char * subject_str, * sess_str;
	double record_time;

	if (xdf_get_conf(f,
	                 XDF_F_FILEFMT, &tmp_type,
	                 XDF_F_REC_NSAMPLE, &rec_ns,
	                 XDF_F_NREC, &nrec,
	                 XDF_F_SAMPLING_FREQ, &fs,
	                 XDF_F_NCHANNEL, &nch,
	                 XDF_F_RECTIME, &record_time,
	                 XDF_F_SUBJ_DESC, &subject_str,
	                 XDF_F_SESS_DESC, &sess_str,
	                 XDF_NOF) != 0) {
		return -1;
	}

	printf(" (%s)\n", get_xdf_filetype_str(tmp_type));
	printf("ns: %d\n", rec_ns);
	printf("nrec: %d\n", nrec);
	printf("sampling frequency: %d\n", fs);
	dump_timestamp("record time", record_time);
	if (strlen(subject_str) > 0)
		printf("subject description: %s\n", subject_str);
	if (strlen(sess_str) > 0)
	printf("session description: %s\n", sess_str);

	i = 0;
	printf("%d channels:\n", nch);
	printf("%.*s%15s, %15s, %15s, %15s, %15s, %15s, %15s, %15s,\n",
	       IDENT, " ", "index",
	       "name", "unit", "stored-type",
	       "physical-min", "physical-max",
	       "digital-min", "digital-max");
	while ((ch = xdf_get_channel(f, i)) != NULL) {
		if (dump_channel(ch, i, IDENT) != 0)
			return -1;
		i++;
	}

	return 0;
}


int main(int argc, char ** argv)
{
	int rv;
	struct xdf * f;

	if (argc != 2) {
		usage(argv[0]);
		return 1;
	}

	f = xdf_open(argv[1], XDF_READ, XDF_ANY);
	if (f == NULL) {
		fprintf(stderr, "Cannot load %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	printf("%s", argv[1]);
	rv = dump_header(f);
	if (rv != 0) {
		fprintf(stderr, "*** failed to process header,"
		                "output may be truncated ***\n");
	}

	xdf_close(f);
	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
