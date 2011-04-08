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
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "streamops.h"


LOCAL_FN
int dup_cloexec(int fd)
{
	int newfd = dup(fd);
#if HAVE_DECL_FD_CLOEXEC
	int flags = fcntl(newfd, F_GETFD);
	fcntl(newfd, F_SETFD, flags|FD_CLOEXEC);
#endif
	return newfd;
}


LOCAL_FN int read16bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint16_t), num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int write16bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint16_t), num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int read24bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, 3, num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int write24bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, 3, num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int read32bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint32_t), num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int write32bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint32_t), num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int read64bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint64_t), num, file)==0)
		return -1;
	return 0;
}


LOCAL_FN int write64bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint64_t), num, file)==0)
		return -1;
	return 0;
}

/* Parse the file (field of nch characters) and assign to the integer val.
 * Advance the file pointer of exactly nch byte.
 */
LOCAL_FN int read_int_field(FILE* file, int* val, unsigned int nch)
{
	char format[8];
	long pos = ftell(file);
	sprintf(format, "%%%ui", nch);
	
	if ((fscanf(file, format, val) <= 0)
	    || fseek(file, pos+nch, SEEK_SET)) 
		return -1;
	
	return 0;
}


/* Parse the file (field of nch characters) and assign to the string val.
 * Advance the file pointer of exactly nch byte.
 * It also removes trailing space characters from the string
 */
LOCAL_FN int read_string_field(FILE* file, char* val, unsigned int nch)
{
	int pos;

	val[nch] = '\0';
	if (fread(val, nch, 1, file) < 1)
		return -1;

	// Remove trailing spaces
	pos = strlen(val);
	while (pos && (val[pos-1]==' '))
		pos--;
	val[pos] = '\0';

	return 0;
}


