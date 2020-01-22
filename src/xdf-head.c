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
int get_xdf_filetype_str(int type, char * type_str)
{
    switch (type) {
    case XDF_EDF:
        strcpy(type_str, "edf");
        return 0;
    case XDF_EDFP:
        strcpy(type_str, "edfp");
        return 0;
    case XDF_BDF:
        strcpy(type_str, "bdf");
        return 0;
    case XDF_GDF1:
        strcpy(type_str, "gdf1");
        return 0;
    case XDF_GDF2:
        strcpy(type_str, "gdf");
        return 0;

    case XDF_ANY: /* should not be asked */
    default:
        *type_str = '\0';
        return -1;
    }
}

/* timestamp is a double in the specs ... */
static
void dump_timestamp(char const * key, double timestamp)
{
	time_t tmp = (time_t) timestamp;
	printf("%s: (%ld) %s", key, (long int)  tmp, ctime(&tmp));
}

static
int get_stotype_str(enum xdftype type, char * type_str)
{
	switch (type) {
		case XDFINT8:
			strcpy(type_str, "int8");
			return 0;
		case XDFUINT8:
			strcpy(type_str, "uint8");
			return 0;
		case XDFINT16:
			strcpy(type_str, "int16");
			return 0;
		case XDFUINT16:
			strcpy(type_str, "uint16");
			return 0;
		case XDFINT24:
			strcpy(type_str, "int24");
			return 0;
		case XDFUINT24:
			strcpy(type_str, "uint24");
			return 0;
		case XDFINT32:
			strcpy(type_str, "int32");
			return 0;
		case XDFUINT32:
			strcpy(type_str, "uint32");
			return 0;
		case XDFFLOAT:
			strcpy(type_str, "float");
			return 0;
		case XDFDOUBLE:
			strcpy(type_str, "double");
			return 0;
		case XDFINT64:
			strcpy(type_str, "int64");
			return 0;
		case XDFUINT64:
			strcpy(type_str, "uint64");
			return 0;
		default:
			return -1;
	}
}

static
int dump_channel(struct xdfch const * ch, int index, int ident)
{
	int rv;
	char * name, * unit_label;
	double phy_min, phy_max, dig_min, dig_max;
	enum xdftype tmp_sto_type;
	char sto_type_str[10];

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

	if (get_stotype_str(tmp_sto_type, sto_type_str) != 0)
		return -1;

	printf("%.*s%15d, %15s, %15s, %15s, %+15d, %+15d, %+15d, %+15d,\n",
	       ident, " ",
	       index,
	       name,
	       unit_label,
	       sto_type_str,
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
	char type_str[8];
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

	if (get_xdf_filetype_str(tmp_type, type_str) != 0)
		return -1;
	printf(" (%s)\n", type_str);

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
	return rv == 0 ? 0 : 1;
}
