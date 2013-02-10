/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include "xdffile.h"
#include "ebdf.h"
#include "gdf1.h"
#include "gdf2.h"

struct dataformat_entry {
	enum xdffiletype type;
	struct xdf* (*alloc_file)(void);
	int (*is_same_type)(const unsigned char*);
};

// Declaration array
static struct dataformat_entry support_datafmt[] = {
	{
		.type=XDF_BDF,
		.alloc_file = xdf_alloc_bdffile,
		.is_same_type = xdf_is_bdffile
	},
	{
		.type=XDF_EDF,
		.alloc_file = xdf_alloc_edffile,
		.is_same_type = xdf_is_edffile
	},
	{
		.type=XDF_GDF1,
		.alloc_file = xdf_alloc_gdf1file,
		.is_same_type = xdf_is_gdf1file
	},
	{
		.type=XDF_GDF2,
		.alloc_file = xdf_alloc_gdf2file,
		.is_same_type = xdf_is_gdf2file
	}
};

static unsigned int num_support_datafmt = sizeof(support_datafmt) 
				/ sizeof(support_datafmt[0]);


LOCAL_FN enum xdffiletype xdf_guess_filetype(const unsigned char* magickey)
{
	unsigned int i;
	enum xdffiletype type = XDF_ANY;

	for (i=0; i<num_support_datafmt; i++) {
		if (support_datafmt[i].is_same_type(magickey)) {
			type = support_datafmt[i].type;
			break;
		}
	}
	return type;
}

LOCAL_FN struct xdf* xdf_alloc_file(enum xdffiletype type)
{
	unsigned int i;
	for (i=0; i<num_support_datafmt; i++) {
		if (type == support_datafmt[i].type)
			return support_datafmt[i].alloc_file();
	}

	return NULL;
}
